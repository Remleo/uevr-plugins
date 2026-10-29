// What happens to one view's uniform parameters. The game's memory is only ever read through memory::copy into a buffer
// of ours, and no lock is held while it is: a fault in a read made under a C++ lock skips the lock's destructor, the next
// view on that thread waits for it forever, and the game stops presenting while its sound goes on.

#include "View.hpp"

#include <atomic>
#include <chrono>
#include <cstring>
#include <mutex>

#include "Basis.hpp"
#include "Log.hpp"
#include "Memory.hpp"
#include "Offsets.hpp"

namespace view {
namespace {
// How far into the parameters ViewForward is looked for. It follows the view's matrices and ViewOriginHigh: +0x490 in
// UE 5.5's source (18 matrices), +0x3d0 found in The Outer Worlds 2 (15). No further: that game's struct is at least
// 0x1084 long (the furthest field its setup writes), and a search past a struct's end reads whatever memory follows it.
constexpr size_t SEARCH = 0x1000;

// Diagnosis lines while ViewForward is not found: at most this many, one a second.
constexpr int DIAGNOSES = 5;

std::mutex g_layout_mutex{};
std::atomic<int> g_forward{-1};
std::atomic<int> g_stride{0};

std::atomic<uint64_t> g_patched{0}, g_not_eye{0}, g_unfound{0}, g_faults{0};
std::atomic<bool> g_fault_said{false};

std::mutex g_diagnosis_mutex{};
int g_diagnoses = 0;
std::chrono::steady_clock::time_point g_last_diagnosis{};

bool is_eye(const float* forward, const float* up) {
    basis::Axes head{};
    return basis::head_for(forward, up, head);
}

void fault(const uint8_t* at, size_t size) {
    ++g_faults;

    if (!g_fault_said.exchange(true) && plugin_log::error != nullptr) {
        plugin_log::error("[SpritePlane] %zu bytes of a view's uniform parameters at %p could not be read; such a view is left as it was",
                          size, at);
    }
}

// Why no ViewForward: the orthonormal triple nearest a recorded eye, and how near. A view that is no eye's (a shadow's)
// is far from every eye; an eye's view a hair off says the comparison is too strict.
void diagnose(const uint8_t* copy, size_t size) {
    if (plugin_log::info == nullptr) return;

    std::scoped_lock _{g_diagnosis_mutex};
    const auto now = std::chrono::steady_clock::now();

    if (g_diagnoses >= DIAGNOSES || now - g_last_diagnosis < std::chrono::seconds(1)) return;

    ++g_diagnoses;
    g_last_diagnosis = now;
    const auto best = offsets::nearest(copy, size, &basis::distance);

    if (!best) {
        plugin_log::info("[SpritePlane] no ViewForward yet: a view's first %zu bytes hold no orthonormal triple; %d eye rotation(s) recorded",
                         size, basis::recorded());
        return;
    }

    plugin_log::info("[SpritePlane] no ViewForward yet: a view's triple nearest an eye is at +%zx (a float3 every %zu bytes), %.6f off; %d eye "
                     "rotation(s) recorded", best->layout.forward, best->layout.stride, best->distance, basis::recorded());
}

bool find_layout(const uint8_t* params) {
    alignas(16) uint8_t copy[SEARCH];
    const size_t size = memory::readable(params, SEARCH);

    if (size < 0x100 || !memory::copy(copy, params, size)) {
        fault(params, size);
        return false;
    }

    const auto layout = offsets::find(copy, size, &is_eye);

    if (!layout) {
        ++g_unfound;
        diagnose(copy, size);
        return false;
    }

    std::scoped_lock _{g_layout_mutex};

    if (g_forward.load() < 0) {
        g_stride = (int)layout->stride;
        g_forward = (int)layout->forward;

        if (plugin_log::info != nullptr) {
            plugin_log::info("[SpritePlane] ViewForward at +%zx in the view's uniform parameters, a float3 every %zu bytes", layout->forward,
                             layout->stride);
        }
    }

    return true;
}

void put(uint8_t* to, const basis::Vec3& v) {
    const float f[3] = {(float)v.x, (float)v.y, (float)v.z};
    std::memcpy(to, f, sizeof(f));
}
}

void after_setup(uint8_t* params) {
    if (g_forward.load() < 0 && !find_layout(params)) return;

    const size_t p = (size_t)g_forward.load(), s = (size_t)g_stride.load();

    // ViewForward, ViewUp, ViewRight, HMDViewNoRollUp, HMDViewNoRollRight.
    alignas(16) uint8_t five[5 * 16]{};

    if (!memory::copy(five, params + p, 5 * s)) {
        fault(params + p, 5 * s);
        return;
    }

    float v[5][3];

    for (size_t i = 0; i < 5; ++i) std::memcpy(v[i], five + i * s, sizeof(v[i]));

    basis::Axes head{};

    if (!basis::head_for(v[0], v[1], head)) {
        ++g_not_eye;
        return;
    }

    // Written where the copy was read from, which the setup has just written itself.
    put(params + p + 2 * s, head.right);
    put(params + p + s, head.up);

    // The roll-free pair only where it holds axes at all: a layout without it leaves whatever follows untouched.
    if (offsets::unit(v[3]) && offsets::unit(v[4])) {
        put(params + p + 4 * s, head.right);
        put(params + p + 3 * s, head.up);
    }

    ++g_patched;
}

Stats stats() {
    return {g_patched.load(), g_not_eye.load(), g_unfound.load(), g_faults.load()};
}
}
