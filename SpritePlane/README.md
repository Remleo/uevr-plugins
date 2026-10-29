# SpritePlane

A UEVR plugin for games played under UEVR's canted projection. It gives both eyes' camera-facing sprites (smoke,
sparks, glow cards) one plane: the head's.

## Why

The canted projection turns each eye a few degrees outward (7 on a Quest). UE's sprites take their plane from the
view's own axes: Cascade's sprite vertex factory and Niagara's FaceCamera mode use the view uniform buffer's `ViewRight`
and `ViewUp`. So each eye's sprites stood in that eye's own plane, and cut differently into walls and into each other.

In The Outer Worlds 2, smoke matched straight ahead and thinned in one eye as the head turned, more with more turn. Read
back from both eyes, the smoke matched to NCC 0.64 under the cant and 0.96 without it. No console variable reached it.

## How

Right after UE's `FSceneView::SetupCommonViewUniformBufferParameters` fills a view's uniform parameters, the plugin
writes the head's axes into the view's `ViewRight` and `ViewUp`, and into `HMDViewNoRollRight` and `HMDViewNoRollUp`,
which sprites with "remove HMD roll" use. The head's axes are the middle of the two eyes of the same engine frame.

`ViewForward` and every matrix stay the eye's: view space, depth, fog and the translucency volume all depend on them.
Only views whose forward and up are an eye's are touched; shadow passes that borrow an eye's axes follow it.

Without a cant the two eyes are parallel, the middle is each eye's own axes, and nothing changes.

Nothing is taken from one build:

- **The function.** UE's renderer reads the console variable `r.MinRoughnessOverride` there and nowhere else. The
  variable's name leads to its registration, the registration to the variable's object, and the object to the one
  function that reads it, through the module's exception directory. Anything other than exactly one function is
  refused.
- **The vectors.** Their offset in the uniform parameters is found by value: the first orthonormal triple, a float3
  every 16 or 12 bytes, whose forward and up are a recorded eye's.
- **The eyes.** Each eye's final rotation comes from UEVR's post stereo view offset callback.

Not reached: Niagara's FaceCameraPlane mode, which also takes `ViewForward`.

## Build and install

`build.cmd` builds `build\SpritePlane.dll`. It needs the MSVC toolchain in `D:\BuildTools`, UEVR's plugin API headers
from `D:\Game Dev\UEVR\include`, and the safetyhook and Zydis libraries from UEVR's own build (`build\_deps`).

`test.cmd` builds and runs the desktop test: rotator axes, the head between two eyes, pairing by frame, finding the
vectors past a matrix whose rows are the same axes, and what happens to one view, including one that ends right before
a page nobody may read.

The game's memory is only read through a guarded copy, bounded by what `VirtualQuery` calls readable, with no lock held:
a fault under a C++ lock skips the lock's destructor, the render thread then waits for it forever, and the game stops
presenting while its sound goes on.

To install, copy the DLL into the game profile's `plugins` folder. The DLL being there is what turns it on.

For A/B comparisons, send the custom event `sprite_plane` with `"0"` or `"1"` to stop and resume the replacement.
From Lua: `uevr.api:dispatch_custom_event("sprite_plane", "0")`.

## Log

At start, the plugin logs where it found the function and the variable's object, and later where `ViewForward` is in
the uniform parameters. If the trail does not lead to exactly one function, it says where it stopped and does
nothing.

Every 5 s, while anything changed, it logs how many eye views got the head's axes, how many views were not an eye's,
how many were searched without finding `ViewForward`, and how many could not be read.

While `ViewForward` has not been found, up to five lines, one a second, name the orthonormal triple nearest a recorded
eye and how far off it is: a shadow's view is far from every eye, while an eye's view a hair off means the comparison is
too strict.
