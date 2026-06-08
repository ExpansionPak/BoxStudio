# BoxStudio

BoxStudio is an early visual editor for N64 decomp-based ROM hacking projects, starting with [HackerSM64](https://github.com/HackerN64/HackerSM64). The goal is to make decomp ROM hacking feel closer to using a small game editor: create a project, open a level, inspect real geometry, place objects, save changes, rebuild the ROM, and test on accurate emulators or real hardware.

The long-term target is broader than SM64. BoxStudio is being designed around decomp bases such as HackerSM64, zeldaret's [OoT](https://github.com/zeldaret/oot), and zeldaret's [MM](https://github.com/zeldaret/mm), but the current implementation is focused on proving the HackerSM64 workflow first.

> BoxStudio does not use proprietary N64 SDKs or libraries.

## Current Status

<img width="2559" height="1391" alt="Screenshot 2026-06-02 151228" src="https://github.com/user-attachments/assets/41caeb37-566f-4fed-abe9-6ba468a7f548" />
<img width="2559" height="1392" alt="Screenshot 2026-06-02 151417" src="https://github.com/user-attachments/assets/c08ab623-7af1-4c92-83ae-fc11026b73ef" />
<img width="2559" height="1391" alt="Screenshot 2026-06-02 151454" src="https://github.com/user-attachments/assets/c42cfd95-0168-46ed-8536-91cf360aa178" />
<img width="2559" height="1392" alt="Screenshot 2026-06-02 151512" src="https://github.com/user-attachments/assets/7dc29d6f-de01-4fd9-8aea-0ef77f457a5e" />
<img width="2559" height="1390" alt="Screenshot 2026-06-02 151544" src="https://github.com/user-attachments/assets/cd252576-4740-4155-a715-f3060297907f" />
<img width="2559" height="1392" alt="Screenshot 2026-06-02 151736" src="https://github.com/user-attachments/assets/5be2fd0c-f2f9-454b-8e37-d359aef328fd" />
<img width="2559" height="1391" alt="Screenshot 2026-06-02 151844" src="https://github.com/user-attachments/assets/b5db85ea-e8c3-4b44-99ff-50620c46efc6" />
<img width="2559" height="1390" alt="Screenshot 2026-06-02 151915" src="https://github.com/user-attachments/assets/966a3299-82dc-4aa3-8bef-cfd1a2c0a5e2" />


https://github.com/user-attachments/assets/88cc17d0-f7f8-481d-a324-a368031dd12b

**The Goomba, Coin, and Star were all added into the Castle Grounds using BoxStudio. This video was recorded using ares v148.** ***NOTE that the game crashes after collecting the star, and does not save the game, which is an issue in which we are hoping to come up with a solution soon.***

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
  main.cpp              Thin application entrypoint.
  BoxStudioApp.h        Shared platform app entrypoint contract.
  core/                 Application-level systems and future shared services.
  decomps/
    sm64/
      hackersm64/       HackerSM64 workspace and level-script compatibility rules.
  platform/
    win32/              Current full Win32/DX11 editor implementation.
    portable/           Cross-platform CLI shell used by CI and non-Windows work.
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

It's important to note that BoxStudio's codebase (as of now) is quite messy, because most of it is currently written by [Codex](https://openai.com/codex/) which does go against our "**No AI/LLM code**" rule in contributing to this project. See [CONTRIBUTING](CONTRIBUTING.md). This will change in the future and more human code will be present, which is why we are directly open to contributors!

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
