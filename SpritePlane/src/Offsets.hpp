#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>

// Where ViewForward, ViewUp and ViewRight are in UE's view uniform parameters, found by their values.
namespace offsets {
struct Layout {
    size_t forward;  // ViewForward; ViewUp, ViewRight, HMDViewNoRollUp and HMDViewNoRollRight follow, one every `stride`
    size_t stride;   // 16 for a float3 padded, 12 for one packed
};

// The first orthonormal triple, a float3 every 16 or 12 bytes, whose forward and up `is_eye` accepts. A matrix's rows
// are orthonormal too, but they are the axes transposed and match no eye.
std::optional<Layout> find(const uint8_t* params, size_t size, bool (*is_eye)(const float* forward, const float* up));

// For the log when nothing is found: the orthonormal triple `distance` puts nearest an eye, and that distance.
struct Nearest {
    Layout layout;
    double distance;
};

std::optional<Nearest> nearest(const uint8_t* params, size_t size, double (*distance)(const float* forward, const float* up));

// A stored unit vector reads within this of length one.
bool unit(const float* v);
}
