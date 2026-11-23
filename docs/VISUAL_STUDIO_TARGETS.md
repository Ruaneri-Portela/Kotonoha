# Visual Studio targets, platforms and architectures

**English (US)** | [Português (Brasil)](VISUAL_STUDIO_TARGETS.pt-BR.md)

This is the authoritative matrix for the checked-in Visual Studio solution at
`buildsystems/visualstudio/Kotonoha.sln`. The same data is declared in
`buildsystems/visualstudio/ProjectManifest.json` and verified by
`python3 tools/ValidateBuildMetadata.py`.

## Supported architectures

| Requested architecture | MSBuild platform | vcpkg triplet | SDL3 / SDL3_ttf provider |
|---|---|---|---|
| x86 (32-bit) | `Win32` | `x86-windows` | native `.vcxproj` projects |
| amd64 (64-bit) | `x64` | `x64-windows` | native `.vcxproj` projects |
| arm64 | `ARM64` | `arm64-windows` | vcpkg libraries |

The upstream SDL3 native project (`lib/submodules/SDL/VisualC/SDL/SDL.vcxproj`)
defines all three platforms. The upstream SDL3_ttf native project
(`lib/submodules/SDL_ttf/VisualC/SDL_ttf.vcxproj`) only defines `Win32` and
`x64`, so the solution maps its `ARM64` solution configurations to the `x64`
project configuration **without building it**; ARM64 links the vcpkg
`SDL3.lib` / `SDL3_ttf.lib` instead.

All platforms support the `Debug` and `Release` configurations (6
configuration/platform pairs in total).

## Project matrix

`Kind` is the MSBuild `ConfigurationType`. Application source counts include
the game port, its VMs, providers and format codecs compiled directly into the
executable (the same strategy as the checked-in Xcode project).

| Project | Kind | Sources | Game slug | Notes |
|---|---|---:|---|---|
The solution groups targets into **solution folders** `lib`, `games`,
`modules` (with `vms`, `providers`, `formats` subfolders), `launcher` and
`external`. Each project also has a `.vcxproj.filters` file mirroring the
repository directory tree in Solution Explorer.

The 28 module static libraries are listed below; application targets reference
them instead of recompiling those sources.

| `KotonohaEngine` | StaticLibrary | 85 | — | shared engine, `Kotonoha/core/BuildVersion.hpp` generated from Git |
| `KotonohaImGui` | StaticLibrary | 7 | — | Dear ImGui + SDL3 backends |
| `KotonohaExample` | Application | 96 | `example` | freely distributable sample |
| `Amagami` | Application | 108 | `amagami` | |
| `ZeroNoTsukaima` | Application | 111 | `zero-no-tsukaima` | |
| `SnowRadish` | Application | 105 | `snow-radish-vacation` | |
| `SummerRadish` | Application | 104 | `summer-radish-vacation` | |
| `MEO` | Application | 104 | `meo` | |
| `DokiDoki` | Application | 104 | `doki-doki-literature-club` | embeds CPython from vcpkg |
| `KatawaShoujo` | Application | 104 | `katawa-shoujo` | embeds CPython from vcpkg |
| `SchoolDays` | Application | 151 | `school-days` | |
| `RealLive` | Application | 107 | `reallive` | includes `libreallive/` vendor codecs |
| `Kirikiri` | Application | 104 | `kirikiri` | KAG + TJS2 VMs |
| `LucaSystem` | Application | 98 | `luca-system` | |
| `Pmail` | Application | 120 | `pmail` | |
| `PonscripterGame` | Application | 104 | `ponscripter` | |
| `N2SystemGame` | Application | 120 | `n2system` | |
| `ArtemisGame` | Application | 99 | `artemis` | |
| `KotonohaLauncherCore` | StaticLibrary | 302 | — | launcher detection/bridge logic, linked by WinUI |
| `KotonohaLauncher` | Application | 303 | — | all game runtimes in one process |

Imported native projects (referenced, not copied or modified):

| Project | Path | Platforms |
|---|---|---|
| `SDL3` | `lib/submodules/SDL/VisualC/SDL/SDL.vcxproj` | Win32, x64, ARM64 |
| `SDL3_ttf` | `lib/submodules/SDL_ttf/VisualC/SDL_ttf.vcxproj` | Win32, x64 |
| `KotonohaWinUI` | `buildsystems/visualstudio/WinUI/KotonohaWinUI.vcxproj` | x64, ARM64 |

## Build commands

```sh
# x86
msbuild buildsystems/visualstudio/Kotonoha.sln /t:Build /p:Configuration=Debug /p:Platform=Win32 ^
  /p:KotonohaVcpkgInstalled=<repo>/out/build/windows-x86/vcpkg_installed/

# amd64
msbuild buildsystems/visualstudio/Kotonoha.sln /t:Build /p:Configuration=Release /p:Platform=x64 ^
  /p:KotonohaVcpkgInstalled=<repo>/out/build/windows-x64/vcpkg_installed/

# arm64
msbuild buildsystems/visualstudio/Kotonoha.sln /t:Build /p:Configuration=Release /p:Platform=ARM64 ^
  /p:KotonohaVcpkgInstalled=<repo>/out/build/windows-arm64/vcpkg_installed/
```

`KotonohaVcpkgInstalled` must be a complete vcpkg tree for the selected
triplet. CMake presets `windows-x86`, `windows-x64` and `windows-arm64`
produce those trees under `out/build/<preset>/vcpkg_installed/`.

## Relationship with the other buildsystems

- CMake is the source of truth for source lists and dependency discovery.
  Refresh with `generate_visualstudio.py --export-from-cmake <build-dir>`.
- Xcode targets are the source of truth for the game target/slug names; the
  Visual Studio manifest is checked against them by `ValidateBuildMetadata.py`.
- No commercial game data is committed. Applications compile without retail
  assets, but need locally supplied assets to actually run.

The native WinUI launcher (`KotonohaWinUI`) is **being implemented** as an
imported x64/ARM64 project; see the [implementation plan](WINUI_NATIVE_APP_PLAN.md).
It links `KotonohaLauncherCore` and calls the C ABI bridge; it does not host SDL3
in a `SwapChainPanel`.

See the [Visual Studio guide](../buildsystems/visualstudio/README.md), the
[build guides](BUILD_GUIDES.md) and the [Windows cross-build](BUILD_WINDOWS.md).
