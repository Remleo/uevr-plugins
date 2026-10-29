#include "Memory.hpp"

#include <cstdint>
#include <cstring>

#include <windows.h>

namespace memory {
size_t readable(const void* from, size_t want) {
    constexpr DWORD READ = PAGE_READONLY | PAGE_READWRITE | PAGE_WRITECOPY | PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE |
                           PAGE_EXECUTE_WRITECOPY;
    const auto start = (uintptr_t)from;
    auto at = start;

    while (at - start < want) {
        MEMORY_BASIC_INFORMATION info{};

        if (VirtualQuery((const void*)at, &info, sizeof(info)) == 0) break;

        if (info.State != MEM_COMMIT || (info.Protect & (PAGE_GUARD | PAGE_NOACCESS)) != 0 || (info.Protect & READ) == 0) break;

        at = (uintptr_t)info.BaseAddress + info.RegionSize;
    }

    return at - start < want ? at - start : want;
}

bool copy(void* to, const void* from, size_t size) {
    __try {
        std::memcpy(to, from, size);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}
}
