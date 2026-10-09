# UEVR plugins

Native UEVR plugins, one folder each. Each folder has a `build.cmd` that builds `build\<Name>.dll`. To use a plugin
in a game, copy its DLL into that game profile's `plugins` folder; UEVR loads it at injection.

Do not use `UnrealVRMod\UEVR\plugins` for this: UEVR loads whatever is there into every game.

| plugin | what it does | used by |
|---|---|---|
| `PipelineRefresh` | switches the pipeline state away and back before a draw that follows compute work with the same state; fixes STALKER 2's right eye being squeezed into the pre-upscale rectangle | STALKER 2 |
