# PipelineRefresh

A UEVR plugin for D3D12 games. Before the first draw after compute work on a command list, it switches the list's
pipeline state to a dummy state and straight back.

## Why

In STALKER 2 under the native stereo fix with DLSS, the right eye sometimes showed the whole picture squeezed into the
pre-upscale rectangle. The pass that draws it is `ComposeSeparateTranslucency`. It is the first draw after the DLSS
evaluation, and it then sampled with the constants of the draw made before it with the same pipeline state: the same
pass at the pre-upscale size. The game sets that state once, before the earlier draw, and only rebinds the root CBV
for the second one.

Measured right before that draw, with frames rotated between four variants:

| Variant | Squeezed frames |
|---|---|
| Untouched | 132 of 511 |
| Same state set again | 173 of 511 |
| Switched to another state and back | 6 of 512 |

Setting the same state again presumably gets dropped as redundant by the driver.

That the compute work in between is the trigger is presumed from where the pass sits, not measured.

## How

The plugin patches the direct command list vtable, which every direct list of the runtime shares. It tracks, per list:

- the pipeline state and the graphics root signature set on it;
- the state the last draw used;
- whether a dispatch came since the last draw.

At the next draw, if the list still draws with the same state as before the dispatch, the plugin sets a dummy state and
then sets the list's own state again. The dummy is built once per root signature and draws nothing.

If the game set a different state in between, the driver binds everything anew by itself, so that draw is left alone.

`ExecuteIndirect` is not counted as compute work. UE draws most of a frame through it, and counting it put the switch
before nearly every draw: about 600 000 a second, with the frame rate down to 33.

Nothing in the plugin is specific to the game or to the engine.

A state left behind by a bundle is not known, so the next draw after a bundle is not switched.

## Build and install

`build.cmd` builds `build\PipelineRefresh.dll`. It needs the MSVC toolchain in `D:\BuildTools` and UEVR's plugin API
headers from `D:\Game Dev\UEVR\include`.

To install, copy the DLL into the game profile's `plugins` folder. The DLL being there is what turns it on.

For A/B comparisons, send the custom event `pipeline_refresh` with `"0"` or `"1"` to stop and resume the switching.
From Lua: `uevr.api:dispatch_custom_event("pipeline_refresh", "0")`.

## Log

Every 5 s, while anything changed, the plugin logs a line to UEVR's log. It gives:

- how many draws after compute work got the switch;
- how many it skipped because the list's state or root signature was never seen being set;
- how many it skipped because no dummy state could be made for their root signature.
