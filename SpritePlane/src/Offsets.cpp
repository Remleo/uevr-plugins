// ViewForward, ViewUp and ViewRight stand in this order in FViewUniformShaderParameters (SceneView.h), with
// HMDViewNoRollUp and HMDViewNoRollRight right after; their offset changes with every engine version and build, their
// values do not lie: three unit axes, at right angles, that are an eye's.

#include "Offsets.hpp"

#include <cmath>

namespace offsets {
namespace {
// Two of the axes within this of orthogonal.
constexpr float ORTHO = 1e-3f;

float dot(const float* a, const float* b) {
    return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
}

bool orthonormal(const float* f, const float* u, const float* r) {
    return unit(f) && unit(u) && unit(r) && std::abs(dot(f, u)) <= ORTHO && std::abs(dot(f, r)) <= ORTHO && std::abs(dot(u, r)) <= ORTHO;
}

// Calls `visit(offset, stride)` for each orthonormal triple until it returns true.
template <typename Visit>
void triples(const uint8_t* params, size_t size, Visit&& visit) {
    for (const size_t stride : {size_t{16}, size_t{12}}) {
        for (size_t p = 0; p + 5 * stride <= size; p += 4) {
            const auto* f = (const float*)(params + p);

            if (orthonormal(f, f + stride / 4, f + stride / 2) && visit(p, stride)) return;
        }
    }
}
}

bool unit(const float* v) {
    return std::abs(std::sqrt(dot(v, v)) - 1.0f) < 1e-3f;
}

std::optional<Layout> find(const uint8_t* params, size_t size, bool (*is_eye)(const float* forward, const float* up)) {
    std::optional<Layout> found{};

    triples(params, size, [&](size_t p, size_t stride) {
        const auto* f = (const float*)(params + p);

        if (!is_eye(f, f + stride / 4)) return false;

        found = Layout{p, stride};
        return true;
    });

    return found;
}

std::optional<Nearest> nearest(const uint8_t* params, size_t size, double (*distance)(const float* forward, const float* up)) {
    std::optional<Nearest> best{};

    triples(params, size, [&](size_t p, size_t stride) {
        const auto* f = (const float*)(params + p);
        const double d = distance(f, f + stride / 4);

        if (!best || d < best->distance) best = Nearest{{p, stride}, d};

        return false;
    });

    return best;
}
}
