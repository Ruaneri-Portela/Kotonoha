# macOS build

**English (US)** | [Português (Brasil)](BUILD_MACOS.pt-BR.md)

Choose **CMake** for desktop builds or the checked-in **Xcode project** for native schemes. Install Xcode Command Line Tools, CMake, and project dependencies (prefer the repository's vcpkg manifest).

## CMake

```sh
cmake -S . -B build/macos -G Ninja \
  -DCMAKE_BUILD_TYPE=Debug \
  -DKOTONOHA_BUILD_EXAMPLE=ON \
  -DKOTONOHA_BUILD_MACOS_BUNDLES=ON
cmake --build build/macos --target Kotonoha
```

Use the appropriate game option/target for other ports. When vcpkg is not auto-detected, set `-DCMAKE_TOOLCHAIN_FILE="$VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake"`.

## CMake presets (macOS 15.0+)

The checked-in `CMakePresets.json` covers the full macOS matrix
(`<platform>-<arch>-native-<provider>-<config>`): `arm64` and `amd64` (the
`x86_64` spelling is kept in the triplet and compiler flags), each with
`vcpkg` or `system` (Homebrew) dependencies and `debug`/`release`
configurations. All presets use Ninja, set `KOTONOHA_BUILD_ALL=ON`,
`KOTONOHA_BUILD_EXAMPLE=ON` and `KOTONOHA_BUILD_MACOS_BUNDLES=ON`, and use
`out/build/<preset>` so the existing `build` tree is untouched.

| Arch | vcpkg | system (Homebrew) |
| --- | --- | --- |
| arm64 | `macos-arm64-native-vcpkg-{debug,release}` | `macos-arm64-native-system-{debug,release}` |
| amd64 | `macos-amd64-native-vcpkg-{debug,release}` | `macos-amd64-native-system-{debug,release}` |

```sh
# Apple Silicon, system packages, Release
cmake --preset macos-arm64-native-system-release
cmake --build --preset macos-arm64-native-system-release --target Kotonoha

# Apple Silicon, vcpkg dynamic triplet, Debug
cmake --preset macos-arm64-native-vcpkg-debug
cmake --build --preset macos-arm64-native-vcpkg-debug --target Kotonoha

# Intel (native or cross-compiled with the x86_64 SDK), vcpkg Release
cmake --preset macos-amd64-native-vcpkg-release
cmake --build --preset macos-amd64-native-vcpkg-release --target Kotonoha
```

## Dynamic vcpkg triplets (macOS 15.0)

Two dedicated overlay triplets in `vcpkg-overlays/triplets/` build all vcpkg
dependencies with dynamic library linkage and a macOS 15.0 deployment target:

- `macos-arm64-vcpkg.cmake` — Apple Silicon (`arm64`)
- `macos-amd64-vcpkg.cmake` — Intel (`x86_64`)

The `amd64` spelling is only a name: the triplet, preset and build directory
all use it, while the compiler and vcpkg still target `x86_64` internally.

The `amd64` arch layer pins `Python3_EXECUTABLE=/usr/bin/python3`,
`Python3_ROOT_DIR` and `Python3_FIND_FRAMEWORK=FIRST` so FindPython3 selects
Xcode's universal `Python3.framework` (3.9, x86_64 + arm64). The matching
`CMAKE_EXE_LINKER_FLAGS` rpath lets `fixup_bundle` resolve the embedded runtime
inside the x86_64 bundle instead of picking Homebrew's arm64-only
`Python.framework`, which made `verify_app` fail.

Both set `VCPKG_LIBRARY_LINKAGE dynamic`, `VCPKG_CRT_LINKAGE dynamic`,
`VCPKG_CMAKE_SYSTEM_NAME Darwin`, `CMAKE_OSX_DEPLOYMENT_TARGET 15.0` and
`-mmacosx-version-min=15.0` in C/CXX/linker flags. They use unique triplet
names so their binary-cache entries never collide with stock `arm64-osx` /
`x64-osx` static triplets. Always pass the overlay triplet and port directories
on the **first** configure.

```sh
# Apple Silicon (arm64), all game ports, Release, dynamic vcpkg libraries
cmake -S . -B out/build/macos-arm64-native-vcpkg-release -G Ninja \
  -DCMAKE_TOOLCHAIN_FILE="$VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake" \
  -DVCPKG_TARGET_TRIPLET=macos-arm64-vcpkg \
  -DVCPKG_OVERLAY_TRIPLETS=/Users/nero/Codes/Kotonoha/vcpkg-overlays/triplets \
  -DVCPKG_OVERLAY_PORTS=/Users/nero/Codes/Kotonoha/vcpkg-overlays/ports \
  '-DVCPKG_MANIFEST_FEATURES=sdl-vcpkg;imgui-vcpkg' \
  -DKOTONOHA_SDL_PROVIDER=VCPKG \
  -DKOTONOHA_IMGUI_PROVIDER=VCPKG \
  -DKOTONOHA_BUILD_ALL=ON -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_OSX_ARCHITECTURES=arm64 -DCMAKE_OSX_DEPLOYMENT_TARGET=15.0
cmake --build out/build/macos-arm64-native-vcpkg-release --parallel

# Intel (x86_64), all game ports, Release, dynamic vcpkg libraries.
# The Python3_* and CMAKE_EXE_LINKER_FLAGS options below are already set by
# `cmake --preset macos-amd64-native-vcpkg-release`; keep them only for manual configures.
# The Xcode Python3.framework is universal (x86_64 + arm64); Homebrew's
# Python and libunibreak are arm64-only and cannot be used for x86_64.
cmake -S . -B out/build/macos-amd64-native-vcpkg-release -G Ninja \
  -DCMAKE_TOOLCHAIN_FILE="$VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake" \
  -DVCPKG_TARGET_TRIPLET=macos-amd64-vcpkg \
  -DVCPKG_OVERLAY_TRIPLETS=/Users/nero/Codes/Kotonoha/vcpkg-overlays/triplets \
  -DVCPKG_OVERLAY_PORTS=/Users/nero/Codes/Kotonoha/vcpkg-overlays/ports \
  '-DVCPKG_MANIFEST_FEATURES=sdl-vcpkg;imgui-vcpkg' \
  -DKOTONOHA_SDL_PROVIDER=VCPKG \
  -DKOTONOHA_IMGUI_PROVIDER=VCPKG \
  -DKOTONOHA_BUILD_ALL=ON -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_OSX_ARCHITECTURES=x86_64 -DCMAKE_OSX_DEPLOYMENT_TARGET=15.0 \
  -DPython3_EXECUTABLE=/usr/bin/python3 \
  -DPython3_ROOT_DIR=/Applications/Xcode.app/Contents/Developer/Library/Frameworks \
  -DPython3_FIND_FRAMEWORK=FIRST \
  '-DCMAKE_EXE_LINKER_FLAGS=-Wl,-rpath,/Applications/Xcode.app/Contents/Developer/Library/Frameworks'
cmake --build out/build/macos-amd64-native-vcpkg-release --parallel
```

Run the headless test suite for either tree with
`SDL_VIDEO_DRIVER=dummy SDL_AUDIO_DRIVER=dummy ctest --test-dir <build-dir> --output-on-failure`.
The known pre-existing failures are `Kotonoha.Infrastructure`,
`Kotonoha.Directories` and `Kotonoha.MediaFormats`. On Apple Silicon the x86_64
binaries run through Rosetta, so add `--timeout 120` (and optionally `-j 8`) to
avoid the default 20-second timeout of the port help tests.

If the very first CMake configure fails inside the overlay `ffmpeg` port with
`x_vcpkg_pkgconfig_get_modules ... no such file or directory` (a vcpkg host-tool
bootstrapping quirk), pre-populate the install tree with `vcpkg install` and
then re-run the same configure command:

```sh
vcpkg install --triplet macos-arm64-vcpkg \
  --overlay-triplets=/Users/nero/Codes/Kotonoha/vcpkg-overlays/triplets \
  --overlay-ports=/Users/nero/Codes/Kotonoha/vcpkg-overlays/ports \
  --x-feature=sdl-vcpkg \
  --x-install-root=/Users/nero/Codes/Kotonoha/out/build/macos-arm64-native-vcpkg-release/vcpkg_installed

# On an arm64 host, keep Homebrew's arm64-only libunibreak out of the x86_64
# pkg-config search path so libass does not link the wrong architecture
# (ld: ignoring file .../libunibreak.dylib: found architecture 'arm64').
# The macos-amd64-native-vcpkg-release preset exports PKG_CONFIG_LIBDIR automatically and the
# triplet passes it through to meson-based ports, so `cmake --preset
# macos-amd64-native-vcpkg-release` needs no manual pre-install for this. Set it explicitly
# only when invoking vcpkg directly:
VCPKG_LIBDIR=/Users/nero/Codes/Kotonoha/out/build/macos-amd64-native-vcpkg-release/vcpkg_installed/macos-amd64-vcpkg/lib/pkgconfig
PKG_CONFIG_LIBDIR="$VCPKG_LIBDIR" \
vcpkg install --triplet macos-amd64-vcpkg \
  --overlay-triplets=/Users/nero/Codes/Kotonoha/vcpkg-overlays/triplets \
  --overlay-ports=/Users/nero/Codes/Kotonoha/vcpkg-overlays/ports \
  --x-feature=sdl-vcpkg \
  --x-install-root=/Users/nero/Codes/Kotonoha/out/build/macos-amd64-native-vcpkg-release/vcpkg_installed
```

The `macos-amd64-native-vcpkg-release` preset sets `PKG_CONFIG_LIBDIR` to that triplet's
`lib/pkgconfig` directory in its `environment` block, and the triplet declares
`VCPKG_ENV_PASSTHROUGH_UNTRACKED` so vcpkg forwards the value to meson-based
ports without adding the absolute path to the ABI hash. This keeps Homebrew's
arm64-only `libunibreak.pc` out of the x86_64 libass build even when vcpkg is
invoked automatically by the CMake toolchain during `cmake --preset
macos-amd64-native-vcpkg-release`.

`BundleApp.cmake` stages both `Python.framework` and `Python3.framework` as real
framework bundles, prunes the nested `Python.app` helper, and rewrites the
embedded runtime dependency to `@executable_path/../Frameworks/...`. If a stale
build still fails with `verify_app failed` or `Could not ad-hoc sign`, delete
only `out/build/macos-amd64-native-vcpkg-release/outputs/<App>.app` and rebuild.

## Xcode

```sh
open buildsystems/xcode/Kotonoha.xcodeproj
xcodebuild -list -project buildsystems/xcode/Kotonoha.xcodeproj
```

Select a macOS scheme in Xcode; for example, `KotonohaExample (macOS)`. See [Xcode details](../buildsystems/xcode/README.md). Bundles and frameworks need runtime dependencies and appropriate resources; never add commercial game assets to Git.
