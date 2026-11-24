# Native dependency providers

**English (US)** | [Português (Brasil)](DEPENDENCIES.pt-BR.md)

Kotonoha resolves its native libraries (SDL3, SDL3_ttf, FFmpeg, libass,
json-c, FreeType, HarfBuzz, fontconfig, zlib) from one of four master
providers, with optional per-library overrides. Not every library accepts
every provider:

| Library | Override option | Allowed providers |
| --- | --- | --- |
| SDL3 | `KOTONOHA_SDL_PROVIDER` | `VCPKG`, `SUBMODULES`, `SYSTEM` |
| SDL3_ttf | `KOTONOHA_SDL_PROVIDER` | `VCPKG`, `SUBMODULES`, `SYSTEM` |
| Dear ImGui | `KOTONOHA_IMGUI_PROVIDER` | `SUBMODULES`, `VCPKG` — **never** `SYSTEM` |
| FFmpeg / libass / json-c / FreeType / HarfBuzz / fontconfig / zlib | — | `VCPKG` or `SYSTEM` |

`KOTONOHA_DEPS_PROVIDER` is the **master** option. The default value for both
overrides is `AUTO`, which follows the master option.

| `KOTONOHA_DEPS_PROVIDER` | SDL3 / SDL3_ttf | Dear ImGui | Media libraries |
| --- | --- | --- | --- |
| `AUTO` (default) | vcpkg if its toolchain is active, otherwise system packages | pinned submodule by default; vcpkg when its toolchain and the `imgui-vcpkg` feature are active | vcpkg or system, matching the toolchain |
| `VCPKG` | vcpkg only (toolchain required on the **first** configure) | vcpkg only (`imgui-vcpkg` feature) | vcpkg |
| `SYSTEM` | system packages only (Homebrew / apt / MSYS2 UCRT) | pinned submodule in `lib/submodules/imgui`; vcpkg only if requested explicitly | system |
| `SUBMODULES` | pinned SDL3/SDL3_ttf submodules | pinned submodule in `lib/submodules/imgui` | system |

`KOTONOHA_SDL_PROVIDER` (`AUTO`, `VCPKG`, `SYSTEM`, `SUBMODULES`) pins
SDL3/SDL3_ttf when it is not `AUTO`. `KOTONOHA_IMGUI_PROVIDER` (`AUTO`,
`SUBMODULES`, `VCPKG`) pins Dear ImGui when it is not `AUTO`.

**SDL3/SDL3_ttf must come from a single provider.** When `SYSTEM` or `VCPKG`
is selected (explicitly or through `AUTO`), configuration fails with a
`FATAL_ERROR` if either library is missing or if only one of the two is found.
The pinned SDL submodules are initialized **only** when `SUBMODULES` is
requested (Android always forces this mode); a system installation is never
mixed with a submodule build.

**Dear ImGui is never resolved from system packages.** Passing
`-DKOTONOHA_IMGUI_PROVIDER=SYSTEM` fails the configure with a `FATAL_ERROR`
explaining the rule: there is no `find_package(imgui)` or `pkg-config imgui`
fallback anywhere in the native dependency path. The engine needs the SDL3
and SDL3 renderer backends compiled against a pinned ImGui version; use the
submodule or the vcpkg package (`imgui` with `sdl3-binding` and
`sdl3-renderer-binding`) instead. An explicit `VCPKG` master always requires
the vcpkg ImGui package; in `AUTO` mode ImGui falls back to the pinned
submodule whenever the vcpkg package is not installed.

## Install commands

### macOS — Homebrew

```sh
brew install cmake ninja pkgconf \
  sdl3 sdl3_ttf ffmpeg libass json-c \
  freetype harfbuzz fontconfig libunibreak plutosvg zlib
```

Homebrew's prefixes (`/opt/homebrew` on Apple Silicon, `/usr/local` on Intel)
are added automatically to `CMAKE_PREFIX_PATH` and `PKG_CONFIG_PATH` for
`SYSTEM` builds.

### Linux — apt (Debian/Ubuntu)

```sh
sudo apt update
sudo apt install -y build-essential cmake ninja-build pkg-config \
  libsdl3-dev libsdl3-ttf-dev \
  libass-dev libavformat-dev libavcodec-dev libavutil-dev \
  libswscale-dev libswresample-dev libjson-c-dev \
  libfreetype-dev libharfbuzz-dev libfontconfig1-dev \
  libgl-dev zlib1g-dev fonts-dejavu-core
```

### Windows — MSYS2 UCRT64 (pacman)

Start an **MSYS2 UCRT64** shell (not MINGW64, not MSYS):

```powershell
pacman -Syu
pacman -S mingw-w64-ucrt-x86_64-toolchain \
  mingw-w64-ucrt-x86_64-cmake mingw-w64-ucrt-x86_64-ninja \
  mingw-w64-ucrt-x86_64-pkgconf \
  mingw-w64-ucrt-x86_64-sdl3 mingw-w64-ucrt-x86_64-sdl3-ttf \
  mingw-w64-ucrt-x86_64-ffmpeg mingw-w64-ucrt-x86_64-libass \
  mingw-w64-ucrt-x86_64-json-c mingw-w64-ucrt-x86_64-freetype \
  mingw-w64-ucrt-x86_64-harfbuzz mingw-w64-ucrt-x86_64-fontconfig \
  mingw-w64-ucrt-x86_64-libunibreak mingw-w64-ucrt-x86_64-plutosvg \
  mingw-w64-ucrt-x86_64-zlib
```

`CMAKE_PREFIX_PATH` and `PKG_CONFIG_PATH` pick up `$MSYSTEM_PREFIX`
(e.g. `C:/msys64/ucrt64`) automatically. Use the canonical
`windows-x64-mingw-system-{debug,release}` presets (or the compatibility alias
`windows-ucrt-system`) or a fresh build directory. Cross MinGW builds from
macOS/Linux are different: **vcpkg is mandatory** there because apt/Homebrew
cannot provide Windows target libraries.

## Configuration examples

```sh
# macOS arm64, Homebrew only (preset already selects SYSTEM + SUBMODULES ImGui)
cmake --preset macos-arm64-native-system-release
cmake --build --preset macos-arm64-native-system-release --target Kotonoha

# Linux, apt SDL3/SDL3_ttf + pinned ImGui submodule
cmake -S . -B build-system -G Ninja -DCMAKE_BUILD_TYPE=Debug \
  -DKOTONOHA_DEPS_PROVIDER=SYSTEM \
  -DKOTONOHA_SDL_PROVIDER=SYSTEM \
  -DKOTONOHA_IMGUI_PROVIDER=SUBMODULES \
  -DKOTONOHA_BUILD_EXAMPLE=ON
cmake --build build-system --target Kotonoha

# MSYS2 UCRT64, pacman only (from the UCRT64 shell)
cmake --preset windows-x64-mingw-system-release
# alias: cmake --preset windows-ucrt-system

# vcpkg for everything (SDL3, SDL3_ttf and ImGui)
cmake --preset macos-arm64-native-vcpkg-release
# or:
cmake -S . -B build-vcpkg -G Ninja \
  -DCMAKE_TOOLCHAIN_FILE="$VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake" \
  '-DVCPKG_MANIFEST_FEATURES=sdl-vcpkg;imgui-vcpkg' \
  -DKOTONOHA_DEPS_PROVIDER=VCPKG \
  -DKOTONOHA_SDL_PROVIDER=VCPKG \
  -DKOTONOHA_IMGUI_PROVIDER=VCPKG

# Pinned submodules for SDL3/SDL3_ttf and ImGui, system media libraries
git submodule update --init --recursive
cmake -S . -B build-submodules -G Ninja \
  -DKOTONOHA_DEPS_PROVIDER=SUBMODULES \
  -DKOTONOHA_SDL_PROVIDER=SUBMODULES \
  -DKOTONOHA_IMGUI_PROVIDER=SUBMODULES \
  -DKOTONOHA_BUILD_EXAMPLE=ON

# Rejected explicitly: system ImGui packages are never used.
cmake -S . -B build-bad -G Ninja -DKOTONOHA_IMGUI_PROVIDER=SYSTEM
# CMake Error: Dear ImGui cannot be resolved from SYSTEM packages.
```

Check the resolved providers in the cache:

```sh
grep -E 'KOTONOHA_(DEPS_PROVIDER|MEDIA_PROVIDER|SDL_PROVIDER|IMGUI_PROVIDER)_RESOLVED' \
  build-system/CMakeCache.txt
```

Use a **fresh build directory** when switching providers: cached
`*_DIR`, pkg-config and vcpkg entries would otherwise keep pointing at the
previous installation.

## Provider × platform matrix

| Platform | `SYSTEM` | `VCPKG` | `SUBMODULES` | `AUTO` |
| --- | --- | --- | --- | --- |
| macOS | Homebrew | vcpkg triplet (`*-osx*`) | SDL from submodule, media from Homebrew | vcpkg if toolchain active, else Homebrew |
| Linux | apt | vcpkg triplet (`x64-linux*`) | SDL from submodule, media from apt | vcpkg if toolchain active, else apt |
| Windows MSYS2 UCRT64 (native) | pacman | vcpkg triplet (`*-mingw-dynamic`) | SDL from submodule, media from pacman | vcpkg if toolchain active, else pacman |
| Windows MinGW cross (macOS/Linux host) | **forbidden (FATAL_ERROR)** | `*-mingw-dynamic` vcpkg only | not used | vcpkg |
| Windows MSVC | allowed with an ABI-compatible native/pkg-config install (warning emitted) | native MSVC triplet (`x64-windows`, ...) | SDL from submodule, media from a native install | vcpkg if toolchain active, else system |
| Android | SDL forced to submodule (media from vcpkg) | per-ABI vcpkg | SDL submodule (default) | vcpkg media + SDL submodule |
| WebAssembly | system Emscripten packages | `wasm32-emscripten` vcpkg (`sdl-vcpkg;imgui-vcpkg`) | not used by presets | vcpkg if toolchain active, else system |

In every row, Dear ImGui follows its own rule: `SUBMODULES` for native/`SYSTEM`
builds; `VCPKG` for a vcpkg master build (or when
`KOTONOHA_IMGUI_PROVIDER=VCPKG` is requested explicitly). The checked-in
presets set this explicitly: `-system-*` presets use `SUBMODULES`, `-vcpkg-*`
presets use `VCPKG` with the `imgui-vcpkg` manifest feature (including
`wasm32-emscripten`, whose vcpkg port installs the SDL3 bindings). Android
keeps the pinned submodule. `SYSTEM` is never a valid ImGui provider.

## ABI and MSYS2 notes

- **Never mix UCRT64, MINGW64, MSVCRT and MSVC binaries.** A `cl.exe` build
  cannot link MSYS2 `.dll.a`/`.a` libraries, and a GCC/UCRT build cannot link
  MSVC `.lib` files. Keep one compiler, one C runtime and one package set per
  build directory.
- On MSVC, `SYSTEM` is permitted but emits a warning: install native
  (MSVC-built) packages or prefer `VCPKG` with an MSVC triplet. The previous
  configure-time hard block was removed so system-package setups can be
  attempted; link errors from incompatible binaries are the caller's
  responsibility.
- `VCPKG` searches only `${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}`,
  so a stray Homebrew/MSYS2 installation cannot leak into a vcpkg build.
- Static SDL3_ttf builds from Homebrew may need `plutosvg`/`harfbuzz` at link
  time; install the list above to avoid unresolved symbols.

## Where this lives

- `CMakeLists.txt` — provider option init, vcpkg triplet default, resolution call.
- `buildsystems/cmake/dependencies/KotonohaDepsProvider.cmake` — master option, resolution, system prefixes.
- `buildsystems/cmake/dependencies/KotonohaSDL.cmake` — SDL3/SDL3_ttf discovery.
- `buildsystems/cmake/dependencies/KotonohaImGui.cmake` — ImGui submodule/vcpkg resolution; rejects `SYSTEM`.
- `buildsystems/cmake/dependencies/KotonohaMedia.cmake` — FFmpeg/libass/json-c discovery.
- `vcpkg.json` — `sdl-vcpkg` and `imgui-vcpkg` manifest features.
- `CMakePresets.json` — `*-native` presets select `SYSTEM` + `KOTONOHA_IMGUI_PROVIDER=SUBMODULES`; `*-vcpkg` presets select `VCPKG` + `KOTONOHA_IMGUI_PROVIDER=VCPKG`.
- `.github/workflows/linux-system-deps.yml` — apt/`SYSTEM` CI coverage.
