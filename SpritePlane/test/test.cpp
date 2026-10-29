// The plugin on the desktop: UE's rotator axes, the head between two canted eyes, pairing by frame, finding ViewForward
// among the uniform parameters without being fooled by a matrix, and what happens to one view -- including one at the
// very end of readable memory, where a read past it under a lock holds the render thread forever and the game presents
// nothing. Built and run by test.cmd.

#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <memory>
#include <thread>
#include <vector>

#include <windows.h>

#include "../src/Basis.hpp"
#include "../src/Memory.hpp"
#include "../src/Offsets.hpp"
#include "../src/View.hpp"

namespace {
// HOW MANY CHECKS THIS FILE MUST RUN: a check that quietly stops existing leaves "bad=0" behind it.
constexpr int EXPECTED = 23;

int g_checks = 0, g_bad = 0;

void check(const char* what, bool ok) {
    ++g_checks;

    if (!ok) ++g_bad;

    std::printf("  %s %s\n", ok ? "ok  " : "BAD ", what);
}

using basis::Axes;
using basis::Vec3;

bool nearly(const Vec3& a, const Vec3& b, double tolerance = 1e-6) {
    return std::abs(a.x - b.x) < tolerance && std::abs(a.y - b.y) < tolerance && std::abs(a.z - b.z) < tolerance;
}

bool nearly(const Axes& a, const Axes& b, double tolerance = 1e-6) {
    return nearly(a.forward, b.forward, tolerance) && nearly(a.right, b.right, tolerance) && nearly(a.up, b.up, tolerance);
}

Vec3 cross(const Vec3& a, const Vec3& b) {
    return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}

Vec3 scale(const Vec3& v, double s) {
    return {v.x * s, v.y * s, v.z * s};
}

Vec3 add(const Vec3& a, const Vec3& b) {
    return {a.x + b.x, a.y + b.y, a.z + b.z};
}

// Turned by `degrees` about the head's own up axis, as UEVR's cant turns an eye.
Axes cant(const Axes& head, double degrees) {
    const double c = std::cos(degrees * 3.14159265358979323846 / 180.0), s = std::sin(degrees * 3.14159265358979323846 / 180.0);
    return {add(scale(head.forward, c), scale(head.right, s)), add(scale(head.right, c), scale(head.forward, -s)), head.up};
}

void to_floats(const Vec3& v, float* out) {
    out[0] = (float)v.x;
    out[1] = (float)v.y;
    out[2] = (float)v.z;
}

bool head_of(const Axes& eye, Axes& head) {
    float f[3], u[3];
    to_floats(eye.forward, f);
    to_floats(eye.up, u);
    return basis::head_for(f, u, head);
}

bool is_eye(const float* forward, const float* up) {
    Axes head{};
    return basis::head_for(forward, up, head);
}

// A fake FViewUniformShaderParameters: ViewToTranslatedWorld first (its rows are the view's right, up, forward), then
// the five vectors at `at`, one every `stride` bytes.
std::vector<uint8_t> fake_params(const Axes& eye, size_t at, size_t stride) {
    std::vector<uint8_t> params(0x1800, 0);
    auto* m = (float*)params.data();
    to_floats(eye.right, m + 0);
    to_floats(eye.up, m + 4);
    to_floats(eye.forward, m + 8);
    m[15] = 1.0f;

    const Vec3 vectors[5] = {eye.forward, eye.up, eye.right, eye.up, eye.right};

    for (int i = 0; i < 5; ++i) to_floats(vectors[i], (float*)(params.data() + at + i * stride));

    return params;
}

bool holds(const uint8_t* params, size_t at, const Vec3& v) {
    float f[3];
    std::memcpy(f, params + at, sizeof(f));
    return std::abs(f[0] - v.x) < 1e-5 && std::abs(f[1] - v.y) < 1e-5 && std::abs(f[2] - v.z) < 1e-5;
}

// Runs `f` on a thread of its own and says whether it returned within 5 s: a view that waits for a lock forever is a
// failed check, not a hung test.
template <typename F>
bool returns(F f) {
    const auto done = std::make_shared<std::atomic<bool>>(false);

    std::thread([f, done] {
        f();
        *done = true;
    }).detach();

    for (int i = 0; i < 500 && !*done; ++i) Sleep(10);

    return *done;
}
}

int main() {
    // ---- UE's rotator axes ------------------------------------------------------------------------------------
    check("the zero rotator is X forward, Y right, Z up",
          nearly(basis::from_rotator(0, 0, 0), Axes{{1, 0, 0}, {0, 1, 0}, {0, 0, 1}}));
    check("yaw 90 looks along +Y with -X on the right", nearly(basis::from_rotator(0, 90, 0), Axes{{0, 1, 0}, {-1, 0, 0}, {0, 0, 1}}));

    const double s30 = 0.5, c30 = std::sqrt(3.0) / 2.0;
    check("pitch 30 raises the forward and tips the up back",
          nearly(basis::from_rotator(30, 0, 0), Axes{{c30, 0, s30}, {0, 1, 0}, {-s30, 0, c30}}));

    bool handed = true;

    for (double p = -80; p <= 80; p += 37) {
        for (double y = -170; y <= 170; y += 53) {
            for (double r = -60; r <= 60; r += 41) {
                const Axes a = basis::from_rotator(p, y, r);
                handed = handed && nearly(cross(a.up, a.forward), a.right, 1e-9) && nearly(cross(a.forward, a.right), a.up, 1e-9);
            }
        }
    }

    check("for any rotator right = up x forward and up = forward x right, as the head is built", handed);

    // ---- the head between two canted eyes -------------------------------------------------------------------------
    const Axes head1 = basis::from_rotator(10, 30, 5);
    const Axes left1 = cant(head1, -7), right1 = cant(head1, 7);
    Axes found{};

    basis::next_frame();
    basis::record(0, left1);
    basis::record(1, right1);

    check("the left eye's view gets the head between the eyes", head_of(left1, found) && nearly(found, head1, 1e-5));
    check("the right eye's view gets the same head", head_of(right1, found) && nearly(found, head1, 1e-5));

    const Axes head2 = basis::from_rotator(-5, 60, -3);
    const Axes left2 = cant(head2, -7), right2 = cant(head2, 7);

    basis::next_frame();
    basis::record(0, left2);
    basis::record(0, left2);
    basis::record(1, right2);

    check("an earlier frame's eye still pairs with its own frame's other eye", head_of(left1, found) && nearly(found, head1, 1e-5));
    check("and the newer frame with its own, an eye recorded twice", head_of(right2, found) && nearly(found, head2, 1e-5));
    check("a view that is no eye's is left alone", !head_of(basis::from_rotator(45, -120, 0), found));

    const Axes head3 = basis::from_rotator(0, -90, 0);
    basis::next_frame();
    basis::record(0, cant(head3, -7));

    check("an eye whose frame's other eye is not recorded is left alone", !head_of(cant(head3, -7), found));

    const Axes head4 = basis::from_rotator(20, 140, 0);
    basis::next_frame();
    basis::record(0, head4);
    basis::record(1, head4);

    check("with no cant the head is the eye itself: nothing changes", head_of(head4, found) && nearly(found, head4, 1e-6));

    // ---- finding ViewForward --------------------------------------------------------------------------------------
    const auto padded = fake_params(left1, 0x2c0, 16);
    const auto layout16 = offsets::find(padded.data(), padded.size(), &is_eye);
    check("ViewForward found past a ViewToTranslatedWorld whose rows are the same axes, padded float3s",
          layout16 && layout16->forward == 0x2c0 && layout16->stride == 16);

    const auto packed = fake_params(right2, 0x31c, 12);
    const auto layout12 = offsets::find(packed.data(), packed.size(), &is_eye);
    check("and with packed float3s", layout12 && layout12->forward == 0x31c && layout12->stride == 12);

    const auto stranger = fake_params(basis::from_rotator(-33, 77, 12), 0x2c0, 16);
    check("a view that is no eye's gives no layout", !offsets::find(stranger.data(), stranger.size(), &is_eye));

    float axis[3] = {0.6f, 0.8f, 0.0f}, longer[3] = {0.7f, 0.8f, 0.0f};
    check("a unit vector reads as one", offsets::unit(axis));
    check("a longer one does not", !offsets::unit(longer));

    // ---- one view, at the end of readable memory and in the middle of it ------------------------------------------
    SYSTEM_INFO system{};
    GetSystemInfo(&system);
    const size_t page = system.dwPageSize;
    auto* const pages = (uint8_t*)VirtualAlloc(nullptr, 2 * page, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
    DWORD old = 0;
    VirtualProtect(pages + page, page, PAGE_NOACCESS, &old);

    // A view's parameters 0x900 bytes long, the last thing before a page nobody may read, holding a stranger's axes.
    uint8_t* const tail = pages + page - 0x900;
    const auto stranger_tail = fake_params(basis::from_rotator(-33, 77, 12), 0x2c0, 16);
    std::memcpy(tail, stranger_tail.data(), 0x900);

    uint8_t scratch[16];
    check("readable memory ends where the unreadable page begins", memory::readable(tail, 0x1000) == 0x900);
    check("a copy from the unreadable page says so instead of raising the fault", !memory::copy(scratch, pages + page, sizeof(scratch)));

    const bool tail_returned = returns([tail] {
        view::after_setup(tail);
        view::after_setup(tail);
    });
    check("two such views in a row both return, searched in vain, nothing read past the end",
          tail_returned && view::stats().unfound == 2 && view::stats().faults == 0);

    uint8_t* const unreadable = pages + page;
    const bool unreadable_returned = returns([unreadable] { view::after_setup(unreadable); });
    check("a view that cannot be read at all is counted and left", unreadable_returned && view::stats().faults == 1);

    auto eye_view = fake_params(left1, 0x2c0, 16);
    uint8_t* const eye = eye_view.data();
    const bool eye_returned = returns([eye] { view::after_setup(eye); });
    check("the next eye's view still goes: its ViewRight and ViewUp become the head's, its ViewForward stays the eye's",
          eye_returned && view::stats().patched == 1 && holds(eye, 0x2c0, left1.forward) && holds(eye, 0x2d0, head1.up) &&
              holds(eye, 0x2e0, head1.right));
    check("and the roll-free pair after them too", holds(eye, 0x2f0, head1.up) && holds(eye, 0x300, head1.right));

    auto other_view = fake_params(basis::from_rotator(-33, 77, 12), 0x2c0, 16);
    const auto before = other_view;
    uint8_t* const other = other_view.data();
    const bool other_returned = returns([other] { view::after_setup(other); });
    check("with ViewForward known, a view that is no eye's is read there and left as it was",
          other_returned && view::stats().not_eye == 1 && other_view == before);

    if (g_checks != EXPECTED) {
        ++g_bad;
        std::printf("  BAD  the count: %d ran, %d expected\n", g_checks, EXPECTED);
    }

    std::printf("RESULT bad=%d of %d\n", g_bad, g_checks);
    return g_bad == 0 ? 0 : 1;
}
