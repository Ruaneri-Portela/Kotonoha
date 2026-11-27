# Visual Studio solution

**English (US)** | [Português (Brasil)](README.pt-BR.md)

`Kotonoha.sln` is a checked-in, native Visual Studio solution, analogous to
`buildsystems/xcode/Kotonoha.xcodeproj`. It compiles the sources **directly**:
no CMake invocation, no generated project files at build time.

## Layout

| Area | Projects | Content |
|---|---|---|
| `lib` | `KotonohaEngine` (DLL) | `lib/src/**/*.cpp`; SDL3, SDL3_ttf and Dear ImGui come from vcpkg |
| `modules\vms` | 12 `VM_*` | one static library per `modules/vms/*` directory |
| `modules\formats` | 14 `Format_*` | one static library per `modules/formats/*/*` directory |
| `modules\providers` | 13 `Provider_*` | one static library per `modules/providers/*` directory |
| `games` | 16 `Game_*Lib` + 16 `Game_*` | one static library per port (`games/*/src`, except `main.cpp`) and one executable compiling only `main.cpp` |
| `launcher` | `KotonohaLauncherCore`, `KotonohaLauncher` | core sources plus links to all 16 game static libraries; single `main.cpp` |

Total: 74 projects. `KotonohaEngine` and `KotonohaLauncherCore` are DLLs;
`GenerateVisualStudioExports.ps1` reproduces CMake's
`WINDOWS_EXPORT_ALL_SYMBOLS` for them. Each game library has its own `IntDir`,
so identically named sources in different ports (`Source.cpp`, `Archive.cpp`,
`Game.cpp`, `Script.cpp`) never produce colliding object files. `Directory.Build.props` centralizes include roots,
preprocessor definitions, C++17/C++20 selection, output paths and vcpkg
library linking. `Directory.Build.targets` deploys the vcpkg runtime DLLs next
to each executable. `generated/Kotonoha/core/BuildVersion.hpp` mirrors the Git
version information used by CMake and Xcode.

## Requirements

- Visual Studio 18 (2026, v145 toolset, Windows SDK 10.0) with the **Desktop
  development with C++** workload, including the MSVC tools for x86, x64 and
  ARM64. Visual Studio 2022 users can retarget the solution to v143.
- vcpkg: `VCPKG_ROOT` must point to a checkout with `vcpkg.exe`. The manifest
  `vcpkg.json`, the overlay ports in `vcpkg-overlays/ports` and the features
  `sdl-vcpkg` and `imgui-vcpkg` are used for all three platforms.

## Build

From the repository root (PowerShell only; macOS/Linux use Python/sh):

```powershell
powershell -ExecutionPolicy Bypass -File tools\PrepareVisualStudio.ps1 -Mode All
```

Modes can be run individually:

```powershell
powershell -ExecutionPolicy Bypass -File tools\PrepareVisualStudio.ps1 -Mode Generate
powershell -ExecutionPolicy Bypass -File tools\PrepareVisualStudio.ps1 -Mode Version
powershell -ExecutionPolicy Bypass -File tools\PrepareVisualStudio.ps1 -Mode Install -Architectures x64
powershell -ExecutionPolicy Bypass -File tools\PrepareVisualStudio.ps1 -Mode Build -Architectures x86,x64,arm64 -Configuration Release
```

`Install` places dependencies in
`buildsystems/visualstudio/vcpkg/<arch>/<triplet>/` (ignored by Git); each
architecture has its own root because vcpkg manifest mode prunes other
triplets from the same install root. `Build` invokes `MSBuild.exe` with `Debug|Release` × `Win32|x64|ARM64`
and writes artifacts to `out/<Platform>/<Configuration>/`.

You can also open `Kotonoha.sln` directly in Visual Studio and select any of
the six solution platforms.

## Validation

```sh
python3 tools/ValidateBuildMetadata.py
python3 tools/check_visual_studio_sources.py
```

The second script checks, without MSBuild, that every source under `lib/src`,
`modules/*/src`, `games/*/src` and `launcher/core` is reachable from a project,
that every project GUID appears in the solution and that all six
configuration/platform combinations exist.

`tools/PrepareVisualStudio.ps1 -Mode Generate` regenerates the checked-in
solution and project files after sources are added or removed; commit the
resulting files together with the source change.
