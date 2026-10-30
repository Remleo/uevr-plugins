# UEVR plugins

Native UEVR plugins, one folder each. Each folder has a `build.cmd` that builds `build\<Name>.dll`; a built DLL of each
plugin's latest release is linked in the table below. To use a plugin in a game, copy its DLL into that game
profile's `plugins` folder; UEVR loads it at injection.

Do not use `UnrealVRMod\UEVR\plugins` for this: UEVR loads whatever is there into every game.

| plugin | what it does | used by | download |
|---|---|---|---|
| `PipelineRefresh` | switches the pipeline state away and back before a draw that follows compute work with the same state; fixes STALKER 2's right eye being squeezed into the pre-upscale rectangle | STALKER 2 | [PipelineRefresh.dll](https://github.com/remleo/uevr-plugins/releases/download/PipelineRefresh-v1.0/PipelineRefresh.dll) ([v1.0](https://github.com/remleo/uevr-plugins/releases/tag/PipelineRefresh-v1.0)) |
| `SpritePlane` | gives both eyes' camera-facing sprites the head's plane under UEVR's canted projection; fixes smoke thinning in one eye as the head turns | The Outer Worlds 2 | [SpritePlane.dll](https://github.com/remleo/uevr-plugins/releases/download/SpritePlane-v1.0/SpritePlane.dll) ([v1.0](https://github.com/remleo/uevr-plugins/releases/tag/SpritePlane-v1.0)) |
