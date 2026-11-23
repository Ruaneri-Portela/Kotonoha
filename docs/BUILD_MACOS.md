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

The checked-in `CMakePresets.json` provides two Release/Ninja presets with
`KOTONOHA_BUILD_EXAMPLE=ON` and `KOTONOHA_BUILD_MACOS_BUNDLES=ON`. They use
`out/build/<preset>` as the build directory and leave the existing `build`
tree untouched. The `-vcpkg` presets use the dynamic macOS 15.0 triplets;
the `-native` presets compile directly against host dependencies without a
vcpkg toolchain.

```sh
# Native Apple Silicon (arm64)
# Build without vcpkg (host dependencies)
cmake --preset macos-arm64-native
cmake --build out/build/macos-arm64-native --target Kotonoha

cmake --preset macos-arm64-vcpkg
cmake --build out/build/macos-arm64-vcpkg --target Kotonoha

# Native Intel, or cross-compiled on Apple Silicon with the x86_64 SDK
cmake --preset macos-amd64-vcpkg
cmake --build out/build/macos-amd64-vcpkg --target Kotonoha
```

## Dynamic vcpkg triplets (macOS 15.0)

Two dedicated overlay triplets in `vcpkg-overlays/triplets/` build all vcpkg
dependencies with dynamic library linkage and a macOS 15.0 deployment target:

- `macos-arm64-vcpkg.cmake` — Apple Silicon (`arm64`)
- `macos-amd64-vcpkg.cmake` — Intel (`x86_64`)

The `amd64` spelling is only a name: the triplet, preset and build directory
all use it, while the compiler and vcpkg still target `x86_64` internally.

Both set `VCPKG_LIBRARY_LINKAGE dynamic`, `VCPKG_CRT_LINKAGE dynamic`,
`VCPKG_CMAKE_SYSTEM_NAME Darwin`, `CMAKE_OSX_DEPLOYMENT_TARGET 15.0` and
`-mmacosx-version-min=15.0` in C/CXX/linker flags. They use unique triplet
names so their binary-cache entries never collide with stock `arm64-osx` /
`x64-osx` static triplets. Always pass the overlay triplet and port directories
on the **first** configure.

```sh
# Apple Silicon (arm64), all game ports, Release, dynamic vcpkg libraries
cmake -S . -B out/build/macos-arm64-vcpkg -G Ninja \
  -DCMAKE_TOOLCHAIN_FILE="$VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake" \
  -DVCPKG_TARGET_TRIPLET=macos-arm64-vcpkg \
  -DVCPKG_OVERLAY_TRIPLETS=/Users/nero/Codes/Kotonoha/vcpkg-overlays/triplets \
  -DVCPKG_OVERLAY_PORTS=/Users/nero/Codes/Kotonoha/vcpkg-overlays/ports \
  '-DVCPKG_MANIFEST_FEATURES=sdl-vcpkg' \
  -DKOTONOHA_SDL_PROVIDER=VCPKG \
  -DKOTONOHA_BUILD_ALL=ON -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_OSX_ARCHITECTURES=arm64 -DCMAKE_OSX_DEPLOYMENT_TARGET=15.0
cmake --build out/build/macos-arm64-vcpkg --parallel

# Intel (x86_64), all game ports, Release, dynamic vcpkg libraries.
# The Xcode Python3.framework is universal (x86_64 + arm64); Homebrew's
# Python and libunibreak are arm64-only and cannot be used for x86_64.
XCODE_PY=/Applications/Xcode.app/Contents/Developer/Library/Frameworks/Python3.framework/Versions/3.9
cmake -S . -B out/build/macos-amd64-vcpkg -G Ninja \
  -DCMAKE_TOOLCHAIN_FILE="$VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake" \
  -DVCPKG_TARGET_TRIPLET=macos-amd64-vcpkg \
  -DVCPKG_OVERLAY_TRIPLETS=/Users/nero/Codes/Kotonoha/vcpkg-overlays/triplets \
  -DVCPKG_OVERLAY_PORTS=/Users/nero/Codes/Kotonoha/vcpkg-overlays/ports \
  '-DVCPKG_MANIFEST_FEATURES=sdl-vcpkg' \
  -DKOTONOHA_SDL_PROVIDER=VCPKG \
  -DKOTONOHA_BUILD_ALL=ON -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_OSX_ARCHITECTURES=x86_64 -DCMAKE_OSX_DEPLOYMENT_TARGET=15.0 \
  -DPython3_EXECUTABLE=/usr/bin/python3 \
  -DPython3_ROOT_DIR=/Applications/Xcode.app/Contents/Developer/Library/Frameworks \
  -DPython3_FIND_FRAMEWORK=FIRST \
  '-DCMAKE_EXE_LINKER_FLAGS=-Wl,-rpath,/Applications/Xcode.app/Contents/Developer/Library/Frameworks'
cmake --build out/build/macos-amd64-vcpkg --parallel
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
  --x-install-root=/Users/nero/Codes/Kotonoha/out/build/macos-arm64-vcpkg/vcpkg_installed

# On an arm64 host, keep Homebrew's arm64-only packages out of the x86_64
# pkg-config search path so the overlay libass port does not link them.
VCPKG_LIBDIR=/Users/nero/Codes/Kotonoha/out/build/macos-amd64-vcpkg/vcpkg_installed/macos-amd64-vcpkg/lib/pkgconfig
PKG_CONFIG_LIBDIR="$VCPKG_LIBDIR" \
vcpkg install --triplet macos-amd64-vcpkg \
  --overlay-triplets=/Users/nero/Codes/Kotonoha/vcpkg-overlays/triplets \
  --overlay-ports=/Users/nero/Codes/Kotonoha/vcpkg-overlays/ports \
  --x-feature=sdl-vcpkg \
  --x-install-root=/Users/nero/Codes/Kotonoha/out/build/macos-amd64-vcpkg/vcpkg_installed
```

## Xcode

```sh
open buildsystems/xcode/Kotonoha.xcodeproj
xcodebuild -list -project buildsystems/xcode/Kotonoha.xcodeproj
```

Select a macOS scheme in Xcode; for example, `KotonohaExample (macOS)`. See [Xcode details](../buildsystems/xcode/README.md). Bundles and frameworks need runtime dependencies and appropriate resources; never add commercial game assets to Git.
