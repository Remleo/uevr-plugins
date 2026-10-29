// Why the middle of the two eyes. UEVR's canted projection turns each eye about the head's up axis, one way for each eye
// (R_left = H * Y(c), R_right = H * Y(-c)), so the head's forward and right are the sums of the eyes' normalised, and the
// up is shared. Without a cant the two eyes are parallel and the middle is each eye's own axes: nothing changes.
//
// Why matched by content and paired by frame. The callback runs on the game thread for frame N while the render thread
// may still set up frame N-1, and UEVR may compute an eye's offset more than once a frame. A view's own forward and up
// name the eye exactly; the other eye has to be the one of the SAME engine frame, or during a head turn the two eyes
// would get heads a frame apart and their sprites would part again.

#include "Basis.hpp"

#include <algorithm>
#include <cmath>
#include <mutex>

namespace basis {
namespace {
constexpr double PI = 3.14159265358979323846;

// Records kept: a few frames of both eyes, with UEVR's repeated computations among them.
constexpr int RING = 64;

// Two unit vectors this close are the same direction: float storage of a double rotation stays within 1e-6.
constexpr double SAME = 1e-4;

struct Record {
    int view_index;
    Axes eye;
    uint64_t frame;
    uint64_t sequence;
};

std::mutex g_mutex{};
Record g_ring[RING]{};
uint64_t g_sequence = 0;
uint64_t g_frame = 0;

double dot(const Vec3& a, const Vec3& b) {
    return a.x * b.x + a.y * b.y + a.z * b.z;
}

Vec3 cross(const Vec3& a, const Vec3& b) {
    return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}

Vec3 normalize(const Vec3& v) {
    const double length = std::sqrt(dot(v, v));
    return length > 0.0 ? Vec3{v.x / length, v.y / length, v.z / length} : v;
}

Vec3 add(const Vec3& a, const Vec3& b) {
    return {a.x + b.x, a.y + b.y, a.z + b.z};
}

bool same(const Vec3& a, const float b[3]) {
    return std::abs(a.x - b[0]) < SAME && std::abs(a.y - b[1]) < SAME && std::abs(a.z - b[2]) < SAME;
}
}

Axes from_rotator(double pitch, double yaw, double roll) {
    const double p = pitch * PI / 180.0, y = yaw * PI / 180.0, r = roll * PI / 180.0;
    const double sp = std::sin(p), cp = std::cos(p), sy = std::sin(y), cy = std::cos(y), sr = std::sin(r), cr = std::cos(r);

    return {
        {cp * cy, cp * sy, sp},
        {sr * sp * cy - cr * sy, sr * sp * sy + cr * cy, -sr * cp},
        {-(cr * sp * cy + sr * sy), cy * sr - cr * sp * sy, cr * cp},
    };
}

void next_frame() {
    std::scoped_lock _{g_mutex};
    ++g_frame;
}

void record(int view_index, const Axes& eye) {
    std::scoped_lock _{g_mutex};
    ++g_sequence;
    g_ring[g_sequence % RING] = {view_index, eye, g_frame, g_sequence};
}

bool head_for(const float forward[3], const float up[3], Axes& head) {
    std::scoped_lock _{g_mutex};
    const Record* mine = nullptr;

    // The newest record of this eye: a view set up late still finds its own frame, the ring holds several.
    for (const auto& r : g_ring) {
        if (r.sequence != 0 && same(r.eye.forward, forward) && same(r.eye.up, up) && (mine == nullptr || r.sequence > mine->sequence)) {
            mine = &r;
        }
    }

    if (mine == nullptr) return false;

    const Record* other = nullptr;

    for (const auto& r : g_ring) {
        if (r.sequence != 0 && r.view_index != mine->view_index && r.frame == mine->frame &&
            (other == nullptr || r.sequence > other->sequence)) {
            other = &r;
        }
    }

    if (other == nullptr) return false;

    const Vec3 f = normalize(add(mine->eye.forward, other->eye.forward));
    const Vec3 r = normalize(cross(normalize(add(mine->eye.up, other->eye.up)), f));

    head = {f, r, cross(f, r)};
    return true;
}

double distance(const float forward[3], const float up[3]) {
    std::scoped_lock _{g_mutex};
    double best = INFINITY;

    for (const auto& r : g_ring) {
        if (r.sequence == 0) continue;

        const double d = std::max({std::abs(r.eye.forward.x - forward[0]), std::abs(r.eye.forward.y - forward[1]),
                                   std::abs(r.eye.forward.z - forward[2]), std::abs(r.eye.up.x - up[0]), std::abs(r.eye.up.y - up[1]),
                                   std::abs(r.eye.up.z - up[2])});
        best = std::min(best, d);
    }

    return best;
}

int recorded() {
    std::scoped_lock _{g_mutex};
    return (int)(g_sequence < RING ? g_sequence : RING);
}
}
