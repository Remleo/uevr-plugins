#pragma once

#include <cstdint>

// The two eyes' rotations as UEVR leaves them each frame, and the head's axes between them.
namespace basis {
struct Vec3 {
    double x, y, z;
};

// UE's axes of a rotation: X forward, Y right, Z up (FRotationMatrix's rows), in world space.
struct Axes {
    Vec3 forward, right, up;
};

// UE's FRotator (degrees) as axes.
Axes from_rotator(double pitch, double yaw, double roll);

// A new engine frame begins (game thread): eyes recorded from here on belong to it.
void next_frame();

// An eye's final rotation, from UEVR's post stereo view offset callback (game thread).
void record(int view_index, const Axes& eye);

// The head's axes for the frame one of whose eyes looks along `forward` with `up` (a view's own vectors, render
// thread): the middle of that frame's two eyes. False when no recorded eye matches, so the view is not an eye's, or
// when that frame's other eye is not recorded.
bool head_for(const float forward[3], const float up[3], Axes& head);

// For the log: how far `forward` and `up` are from the nearest recorded eye, the largest component's difference of
// either; infinity with nothing recorded.
double distance(const float forward[3], const float up[3]);

// How many eye rotations are recorded.
int recorded();
}
