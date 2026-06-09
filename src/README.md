# Source Layout

BoxStudio is being pulled out of its prototype Win32 monolith without breaking
the working editor. The current split uses implementation fragments so the
existing single translation unit keeps compiling while contributors get clear
places to move code into proper `.cpp` and `.h` modules.

```text
src/
  app/          App state, save/write commands, and lifecycle-adjacent editor logic.
  core/         Shared utilities that should stay platform-neutral.
  decomps/      Per-decomp workspace detection and game-specific compatibility rules.
  editor/       Level loading, object parsing, and editing workflow code.
  project/      Project manifests, copied workspace layout, and script writers.
  renderer/     Level geometry, textures, render data, and renderer backends.
  ui/           ImGui windows, panels, and editor presentation.
  platform/     OS/windowing entrypoints. Win32 owns DX11 today; portable owns CI.
```

When continuing the cleanup, prefer moving one subsystem at a time from an
`.inc.cpp` fragment into a normal header/source pair. Keep platform code calling
shared editor, project, renderer, and decomp interfaces instead of owning those
rules directly.
