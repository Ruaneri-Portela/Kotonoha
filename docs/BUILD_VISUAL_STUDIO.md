# Visual Studio build (native solution)

**English (US)** | [Português (Brasil)](BUILD_VISUAL_STUDIO.pt-BR.md)

Kotonoha ships a second, independent Windows build path in addition to CMake:
a checked-in Visual Studio solution at
`buildsystems/visualstudio/Kotonoha.sln`. Like the native Xcode project, it
references the source tree directly and does not invoke CMake.

## Solution map

| Configuration | Platforms | Toolset | Dependencies |
|---|---|---|---|
| Debug, Release | Win32 (x86), x64 (amd64), ARM64 | MSVC v145 / Windows SDK 10.0 | vcpkg, manifest mode |

The 75 projects are divided into 1 engine DLL (`KotonohaEngine`), 39 module
static libraries (12 VMs, 14 formats, 13 providers), 16 game static libraries
plus 16 `main.cpp` executables, and 3 launcher projects (`KotonohaLauncherCore`
DLL, detached `KotonohaGameHost` executable, and `KotonohaLauncher` executable).
SDL3, SDL3_ttf and Dear ImGui come entirely from vcpkg. Game libraries each
have their own intermediate directory, so identically named sources in
different ports do not collide in MSBuild. See
[`buildsystems/visualstudio/README.md`](../buildsystems/visualstudio/README.md)
for details.

## Prepare and build

Windows scripts are PowerShell only (`.ps1`); the cross-platform validators
remain Python (`.py`). From the repository root:

```powershell
$env:VCPKG_ROOT = 'C:\Program Files\Microsoft Visual Studio\18\Community\VC\vcpkg'
powershell -ExecutionPolicy Bypass -File tools\PrepareVisualStudio.ps1 -Mode Generate
powershell -ExecutionPolicy Bypass -File tools\PrepareVisualStudio.ps1 -Mode Version
powershell -ExecutionPolicy Bypass -File tools\PrepareVisualStudio.ps1 -Mode Install
powershell -ExecutionPolicy Bypass -File tools\PrepareVisualStudio.ps1 -Mode Build
```

`-Mode Install` runs `vcpkg install` for `x86-windows`, `x64-windows` and
`arm64-windows` with `--x-feature=sdl-vcpkg --x-feature=imgui-vcpkg` and the
ffmpeg overlay ports. `-Mode Build` compiles all six Debug/Release ×
platform combinations; restrict it with `-Architectures x64 -Configuration Release`
when iterating. Artifacts go to `buildsystems/visualstudio/out/<Platform>/<Configuration>/`. The launcher core
also compiles `launcher/bridge/java/JavaBridge.cpp` when a JDK is available, so
the same DLL serves JNI clients; the Java example adds that directory to `PATH`
so its dependent DLLs are found.

Opening the `.sln` in Visual Studio works as well — vcpkg include/library paths
are resolved from `out/build/windows-<arch>-msvc-vcpkg/vcpkg_installed/<triplet>/`
(shared directly with the MSVC CMake presets, just like Xcode on macOS), so no
global `vcpkg integrate install` is required.

For the full Win32/x64/ARM64 × Debug/Release matrix plus the matching MSVC CMake
presets, use the shared driver described in
[BUILD_WINDOWS.md](BUILD_WINDOWS.md#windows-build-driver):
`powershell -ExecutionPolicy Bypass -File tools\BuildWindows.ps1`.

## Verification

```sh
python3 tools/ValidateBuildMetadata.py
python3 tools/check_visual_studio_sources.py
```

Both are read-only. The first checks CMake/Xcode/Visual Studio metadata
consistency; the second checks source coverage and the configuration matrix of
the Visual Studio solution.

## Launcher, GameHost and Avalonia manager

`KotonohaLauncherCore.dll` spawns the universal detached helper
`KotonohaGameHost.exe` (`launcher/host/GameHost.cpp`) to execute each game on
its own main thread (an SDL3 requirement) across all desktop frontends. The
managed game manager in `launcher/desktop/` invokes the C ABI
bridge, receives the child PID, captures stdout/stderr and keeps the game card
green until the process exits. The manager switches between Portuguese and
English at runtime and copies `assets/` to its build output so the engine can
open the bundled fonts.

Commercial game assets remain in local, untracked `games/<slug>/game-assets/`
directories and are never copied into the solution or packaged by it.
