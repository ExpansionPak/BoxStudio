# BoxStudio

BoxStudio is an early visual editor for N64 decomp-based ROM hacking projects, starting with [HackerSM64](https://github.com/HackerN64/HackerSM64). The goal is to make decomp ROM hacking feel closer to using a small game editor: create a project, open a level, inspect real geometry, place objects, save changes, rebuild the ROM, and test on accurate emulators or real hardware.

The long-term target is broader than SM64. BoxStudio is being designed around decomp bases such as HackerSM64, zeldaret's [OoT](https://github.com/zeldaret/oot), and zeldaret's [MM](https://github.com/zeldaret/mm), but the current implementation is focused on proving the HackerSM64 workflow first.

> BoxStudio does not use proprietary N64 SDKs or libraries.

## Current Status

BoxStudio is in active early development. Expect missing tools, changing project metadata, and rough editor workflows. The important pieces that are already taking shape are:

- Project creation from an existing built HackerSM64 decomp tree.
- BoxStudio project metadata under `.boxstudio/`.
- Default project storage under `Documents/BoxStudioProjects`.
- Existing level discovery from the copied HackerSM64 workspace.
- A 3D level editor viewport with orbit and fly controls.
- Rendered SM64 level geometry with texture-mode support.
- Object placement/editing for supported objects.
- Save/write support for safe level-script object edits.
- Guardrails for actor segment groups so unsupported objects are skipped instead of producing ROMs that crash at runtime.

## Why Decomp Bases?

BoxStudio was originally imagined as a visual ROM and raw assembly hacking tool. That direction was dropped because raw ROM hacking becomes painful very quickly, even with matching C code beside the disassembly. Decomp projects already expose source files, level scripts, assets, model declarations, and build systems, so BoxStudio now works with those structures instead of fighting the raw ROM directly.

The editor copies a decomp workspace into a BoxStudio project folder, then writes normal project files back into that copied tree. Rebuilding the ROM remains the job of the decomp base.

## Recommended Testing

BoxStudio targets real hardware behavior, so accurate emulation matters.

- Use [ares v147 or newer](https://ares-emu.net/) for emulator testing.
- For hardware testing, a flashcart such as [SummerCart64](https://summercart64.dev/) is recommended.
- Many hacks should be tested with the Expansion Pak enabled.

## Building BoxStudio

BoxStudio uses CMake and C++17.

### Windows GUI Build

The full editor currently uses Win32, Dear ImGui, and DirectX 11.

```powershell
cmake -S . -B build -DBOXSTUDIO_BUILD_GUI=ON
cmake --build build --config Release
```

The executable is written to the CMake build output directory. During development, Debug builds are also supported:

```powershell
cmake -S . -B build -DBOXSTUDIO_BUILD_GUI=ON -DCMAKE_BUILD_TYPE=Debug
cmake --build build --config Debug
```

### Linux/macOS Portable Build

The full GUI renderer is not ported to Linux/macOS yet. Non-Windows platforms currently build a portable command-line shell that keeps CI green and gives contributors a place to work on project parsing, metadata, and non-renderer systems.

```bash
cmake -S . -B build -DBOXSTUDIO_BUILD_GUI=OFF -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build
```

Portable commands available today:

```bash
BoxStudio --version
BoxStudio --check-project /path/to/BoxStudioProject
```

## Repository Layout

```text
src/
  core/                 Application-level systems and future shared services.
  editor/               Editor domain code as it is extracted from the prototype.
  platform/
    win32/              Current full Win32/DX11 editor implementation.
    portable/           Cross-platform CLI shell used by CI and non-Windows work.
  ui/                   Future UI panels and reusable editor widgets.
docs/                   Format notes and design notes.
third_party/imgui/      Dear ImGui submodule.
```

The current editor implementation is still larger than it should be. Public-facing cleanup is happening in stages:

1. Keep platform-specific code out of the root source folder.
2. Preserve a working Windows editor while Linux/macOS builds stay valid.
3. Extract HackerSM64 project, level-script, texture, and renderer systems into focused modules.
4. Add tests around project metadata, level-script writes, and actor-group compatibility.

## HackerSM64 Notes

HackerSM64 actor models depend on segment groups. BoxStudio must respect those groups when writing objects into a level. For example, Goombas are safe in levels that load the common0 model group, while Koopas require group14. If a level does not load the needed group, BoxStudio should warn or skip the object instead of writing a script that compiles but crashes in-game.

When rebuilding a HackerSM64 project, use the toolchain command expected by your local decomp setup. For example:

```bash
make CROSS=mips-linux-gnu- -j4
```

## Contributing

BoxStudio is not ready for broad feature work without coordination yet, but useful contributions include:

- Build fixes and CI improvements.
- Cross-platform platform-layer work.
- HackerSM64 level-script parsing fixes.
- Actor/model group metadata.
- Renderer and texture-mode accuracy fixes.
- Documentation for decomp workflows.

Please avoid editing third-party submodules directly. Dear ImGui lives under `third_party/imgui` as a dependency; BoxStudio-specific fixes should stay in BoxStudio source.

## License

Copyright (c) 2025-2026 ExpansionPak.

BoxStudio is licensed under the MIT License. See [LICENSE](LICENSE) for details.

Crediting BoxStudio in a ROM hack is not required, but credit through a name, logo, or boot/credits sequence is appreciated.
