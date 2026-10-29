// The trail, with nothing taken from one build: UE's renderer reads the console variable r.MinRoughnessOverride in
// SetupCommonViewUniformBufferParameters and nowhere else (SceneView.cpp). So the variable's name leads to its
// registration (`lea rdx, name` beside `lea rcx, object`), the object to the code that loads its data pointer, and that
// code to its function through the module's exception directory. Anything other than one function at the end is refused:
// a wrong function hooked would write into a struct of some other shape.

#include "Find.hpp"

#include <algorithm>
#include <cstring>
#include <set>
#include <vector>

#include <windows.h>

#include "Log.hpp"

namespace find {
namespace {
struct Range {
    uint8_t* begin;
    uint8_t* end;
};

struct Sections {
    std::vector<Range> code;
    std::vector<Range> readable;
    std::vector<Range> writable;
};

// The parts of a range the page protection lets us read: a protector's guard or no-access page would end the game.
std::vector<Range> readable_parts(Range range) {
    std::vector<Range> out{};
    MEMORY_BASIC_INFORMATION info{};

    for (auto* p = range.begin; p < range.end; p = (uint8_t*)info.BaseAddress + info.RegionSize) {
        if (VirtualQuery(p, &info, sizeof(info)) == 0) break;

        const auto protect = info.Protect & 0xFF;
        const bool readable = info.State == MEM_COMMIT && (info.Protect & PAGE_GUARD) == 0 && protect != PAGE_NOACCESS &&
                              protect != PAGE_EXECUTE;
        auto* const end = (std::min)((uint8_t*)info.BaseAddress + info.RegionSize, range.end);

        if (readable) out.push_back({p, end});
    }

    return out;
}

Sections sections_of(HMODULE module) {
    Sections out{};
    auto* const base = (uint8_t*)module;
    auto* const nt = (IMAGE_NT_HEADERS*)(base + ((IMAGE_DOS_HEADER*)module)->e_lfanew);
    auto* section = IMAGE_FIRST_SECTION(nt);

    for (WORD i = 0; i < nt->FileHeader.NumberOfSections; ++i, ++section) {
        const Range range{base + section->VirtualAddress, base + section->VirtualAddress + section->Misc.VirtualSize};

        for (const auto& part : readable_parts(range)) {
            if (section->Characteristics & IMAGE_SCN_MEM_EXECUTE) out.code.push_back(part);
            if (section->Characteristics & IMAGE_SCN_MEM_READ) out.readable.push_back(part);
        }

        if (section->Characteristics & IMAGE_SCN_MEM_WRITE) out.writable.push_back(range);
    }

    return out;
}

bool inside(const std::vector<Range>& ranges, const uint8_t* p) {
    for (const auto& r : ranges) {
        if (p >= r.begin && p < r.end) return true;
    }

    return false;
}

// Where an instruction's RIP-relative disp32 at `disp` points, the instruction ending right after the displacement.
uint8_t* target_of(uint8_t* disp) {
    return disp + 4 + *(int32_t*)disp;
}

std::vector<uint8_t*> find_wide(const std::vector<Range>& ranges, const wchar_t* text) {
    const auto bytes = (wcslen(text) + 1) * sizeof(wchar_t);
    std::vector<uint8_t*> out{};

    for (const auto& r : ranges) {
        for (auto* p = r.begin; p + bytes <= r.end; p += 2) {
            if (p[0] == (uint8_t)text[0] && memcmp(p, text, bytes) == 0) out.push_back(p);
        }
    }

    return out;
}

// The start of the function holding `address`, its chained unwind entries followed back to the primary one.
uint8_t* function_of(uint8_t* address) {
    DWORD64 image_base{};
    auto* entry = RtlLookupFunctionEntry((DWORD64)address, &image_base, nullptr);

    for (int guard = 0; entry != nullptr && guard < 16; ++guard) {
        const auto* unwind = (const uint8_t*)(image_base + entry->UnwindData);

        if (((unwind[0] >> 3) & UNW_FLAG_CHAININFO) == 0) return (uint8_t*)(image_base + entry->BeginAddress);

        const uint8_t codes = unwind[2];
        entry = (PRUNTIME_FUNCTION)(unwind + 4 + ((codes + 1) & ~1) * 2);
    }

    return nullptr;
}
}

void* setup_common_view_uniform_buffer_parameters() {
    auto* const module = GetModuleHandleW(nullptr);
    const auto sections = sections_of(module);
    const auto rva = [module](const void* p) { return (unsigned long long)((const uint8_t*)p - (const uint8_t*)module); };

    const auto names = find_wide(sections.readable, L"r.MinRoughnessOverride");

    if (names.size() != 1) {
        plugin_log::error("[SpritePlane] the name r.MinRoughnessOverride found %zu times, not once", names.size());
        return nullptr;
    }

    // The registration: `lea rdx, [rip+name]` (48 8D 15), then within a few instructions `lea rcx, [rip+object]`
    // (48 8D 0D) into writable data, then the variable's constructor.
    std::set<uint8_t*> objects{};

    for (const auto& r : sections.code) {
        for (auto* p = r.begin; p + 7 <= r.end; ++p) {
            if (p[0] != 0x48 || p[1] != 0x8D || p[2] != 0x15 || target_of(p + 3) != names[0]) continue;

            for (auto* q = p + 7; q + 7 <= p + 64 && q + 7 <= r.end; ++q) {
                if (q[0] == 0x48 && q[1] == 0x8D && q[2] == 0x0D && inside(sections.writable, target_of(q + 3))) {
                    objects.insert(target_of(q + 3));
                    break;
                }
            }
        }
    }

    if (objects.size() != 1) {
        plugin_log::error("[SpritePlane] r.MinRoughnessOverride's registration gave %zu variable objects, not one", objects.size());
        return nullptr;
    }

    auto* const object = *objects.begin();

    // Its readers: a 64-bit `mov reg, [rip+d]` ((48|4C) 8B, ModRM mod 00 r/m 101) loading a pointer from inside the
    // object -- TAutoConsoleVariable keeps its data pointer after FAutoConsoleObject's vtable and target, at +0x10.
    std::set<uint8_t*> functions{};
    size_t loads = 0;

    for (const auto& r : sections.code) {
        for (auto* p = r.begin; p + 7 <= r.end; ++p) {
            if ((p[0] != 0x48 && p[0] != 0x4C) || p[1] != 0x8B || (p[2] & 0xC7) != 0x05) continue;

            auto* const target = target_of(p + 3);

            if (target < object + 0x08 || target > object + 0x18) continue;

            ++loads;

            if (auto* const fn = function_of(p); fn != nullptr) functions.insert(fn);
        }
    }

    if (functions.size() != 1) {
        plugin_log::error("[SpritePlane] r.MinRoughnessOverride (object at +%llx) is read by %zu functions (%zu loads), not one",
                          rva(object), functions.size(), loads);
        return nullptr;
    }

    plugin_log::info("[SpritePlane] SetupCommonViewUniformBufferParameters at +%llx (r.MinRoughnessOverride's object at +%llx)",
                     rva(*functions.begin()), rva(object));
    return *functions.begin();
}
}
