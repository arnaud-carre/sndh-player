# Repository Guidelines

## User Instructions

- Never modify `README.md` without asking the user for explicit permission first. Preserve the user's reverted version.
- Never create commits. Leave local changes uncommitted; the user will create their branch and commit when everything works.
- Preserve the user's existing changes and reverts. In particular, preserve the complete current header of `SndhArchivePlayer/main.cpp`, including the author credit. Do not shorten or rewrite it.
- Use Windows CRLF line endings for repository text files, as configured in `.gitattributes`. Exception: `.yml` and `.yaml` files under `.github/` must retain their original line endings. Do not reconvert reverted workflows such as `release.yml`. CUI files under `SndhArchivePlayer/extern/cui/` retain their upstream LF convention.
- Preserve file encodings and author credits. Avoid unrelated reformatting, incidental IDE changes, generated binaries, exported audio and local playback preferences in source changes.

## Project Structure

The C++17 player supports Windows and macOS. Both platforms use GLFW windows, CUI for the interface, and OpenGL 3.3 for rendering. Native window title bars remain enabled on both platforms.

`SndhArchivePlayer/` contains the application:

- `main.cpp`: GLFW/OpenGL/CUI startup, frame loop and OS file drops.
- `CuiGlfw.h` / `CuiGlfw.cpp`: input, clipboard, cursor and DPI integration between GLFW and CUI.
- `SndhArchivePlayer.h` / `SndhArchivePlayer.cpp`: playback coordination, fixed panel layout, song information, Options and About.
- `SndhArchive.h` / `SndhArchive.cpp`: ZIP loading, metadata, filtering and the virtualized music table. `ArchiveFilter.h` preserves the previous ordered include/exclude filter syntax.
- `AudioStream`, `AsyncSndhStream` and `jobSystem`: audio output, background rendering and archive work. `WavWriter` writes WAV exports.
- `AppData.h` / `AppData.cpp`: native preferences, using an INI file on Windows and CoreFoundation preferences on macOS.
- `Ui.h`: small C++17 helpers for the explicit CUI C API.

`AtariAudio/` is a Git submodule providing SNDH/YM rendering and Atari hardware emulation. Do not modify or update it incidentally.

Vendored CUI, GLFW and ZIP sources live in `SndhArchivePlayer/extern/`. Dear ImGui sources remain there for reference but are no longer compiled or used. CUI is included at a fixed revision with local fixes documented in `SndhArchivePlayer/extern/cui/UPSTREAM.md`. Avoid unrelated dependency edits; document any necessary local CUI fixes there and preserve them when updating the dependency.

The root CMake project and Visual Studio solution build the player. The CMake project under `AtariAudio/example/AtariAudio2Wav/` is a separate converter. `docs/cui-preview.png` illustrates the current interface; `thumbnail.png` predates the CUI migration.

## Build Commands

Initialize the submodule if it is missing:

```powershell
git submodule update --init --recursive
```

The checked-in Visual Studio solution uses MSVC v143 and Windows SDK 10.0. Build from a Visual Studio Developer PowerShell:

```powershell
msbuild SndhArchivePlayer.sln /m /p:Configuration=Debug /p:Platform=x64
msbuild SndhArchivePlayer.sln /m /p:Configuration=Release /p:Platform=x64
.\SndhArchivePlayer_x64_debug.exe
```

These builds write executables to the repository root. CMake builds write executables under their build directory. Use an installed Visual Studio generator; for example:

```powershell
cmake -S . -B build/windows -G "Visual Studio 17 2022" -A x64
cmake --build build/windows --config Debug --parallel
cmake --build build/windows --config Release --parallel
```

For Visual Studio 2026, use `-G "Visual Studio 18 2026"` in a separate build directory. Do not reuse a CMake cache created with a different generator.

On macOS:

```sh
cmake -S . -B build/macos-debug -DCMAKE_BUILD_TYPE=Debug
cmake --build build/macos-debug --parallel
cmake -S . -B build/macos-release -DCMAKE_BUILD_TYPE=Release
cmake --build build/macos-release --parallel
```

Dependencies are built from the checked-out sources; normal CMake configuration does not download them.

## Testing

Build Debug and Release after functional code changes. The versioned CMake project, Visual Studio solution and GitHub workflows do not depend on `tests/`. Do not reintroduce application test targets or a test-build option into the versioned build configuration without the user's request.

The local `tests/` directory may remain uncommitted. It contains GUI smoke checks, isolated preference functions (`TestAppData.cpp`) and the fast-scroll regression test. Keep test-specific behavior in separate files; no test-only macro or preference implementation belongs in application code. Previous CMake test target definitions are retained locally in `build/local-test-targets.cmake` for reference, outside the versioned build.

The local GUI tests exclude MIX1 fixtures (`BrainDamage.ym` and `Cuddly Tcb fullscreen.ym`) because AtariAudio's current Debug loader asserts on their zero player tick rate; do not silently change the submodule to work around this.

Manually check OS file drops, DPI/Retina changes, actual OpenGL rendering, audible playback, seeking, subsong navigation, playback modes and export as relevant. Report the checks actually performed; Windows validation does not establish macOS compatibility. Record track, subsong, reproduction steps and expected behavior for regressions.

## UI and Code Conventions

Follow surrounding C++ style: tabs in application classes, braces on separate lines, PascalCase classes and methods, camelCase locals, and `m_` member prefixes. Keep paired declarations and definitions together. No repository-wide formatter or linter is configured.

Keep playback advancement and job completion independent of GUI visibility. Queue changes that invalidate audio or file buffers before constructing the next frame: CUI canvas painting occurs after layout.

The panel arrangement is fixed, with resizable separators and no docking. Only SNDH Archive and File Viewer have tabs; those tabs cannot be dragged, reordered or closed. Tracks play by clicking the table row; do not restore per-row play buttons.

Song information uses a left-aligned, uniformly styled `title by author` line (omit `by` when the author is empty), followed immediately by a discreet round outlined details button with a tooltip. Details appear above playback controls. Show the Sub-tune row only for tracks with at least two subsongs.

Keep About visible with Options beside it. Options contains playback modes in the order Continuous, Random, Loop (Continuous is the default), WAV export with the complete filename in its tooltip, default duration, and Dark/Light. Do not restore the Single mode, the redundant application banner, or the layout-shifting preparation label. About retains the original author and web-player credits, a centered title, and a standard close cross.

## Review Notes

Keep changes focused and explain the behavior changed. Include relevant build/test results and screenshots for visible UI changes. Call out submodule changes explicitly when requested. Do not commit: the user handles branch creation and commits.
