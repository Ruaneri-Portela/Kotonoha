# Windows builds (MSVC, MinGW and MinGW cross)

**English (US)** | [Português (Brasil)](BUILD_WINDOWS.pt-BR.md)

For the native Visual Studio solution (no CMake; Win32/x64/ARM64, Debug and
Release, toolset v145 / VS 18), see
[BUILD_VISUAL_STUDIO.md](BUILD_VISUAL_STUDIO.md).

CMake is configured directly with `cmake -S . -B <build-dir> -G Ninja`; no preset
file is checked in. These are single-config builds, so `CMAKE_BUILD_TYPE`
selects Debug or Release.

## MSVC (Windows host)

Use the Visual Studio developer prompt matching the target architecture
(`vcvarsall.bat x64`, `x86` or `arm64`) so `cl.exe` and the SDK are visible. With
vcpkg, point the toolchain at `$VCPKG_ROOT` and select `x64-windows`,
`x86-windows` or `arm64-windows` as `VCPKG_TARGET_TRIPLET`. MSVC + `SYSTEM`
dependencies require an ABI-compatible installation: never mix MSYS2 `*.dll.a`
with MSVC `*.lib`. CMake warns about that combination.

```sh
cmake -S . -B build-msvc -G Ninja -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_TOOLCHAIN_FILE="$VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake" \
  -DVCPKG_TARGET_TRIPLET=x64-windows \
  -DKOTONOHA_BUILD_EXAMPLE=ON
cmake --build build-msvc --target Kotonoha
```

For a multi-config Visual Studio build, select a Visual Studio generator
instead of Ninja and build with `cmake --build <dir> --config Release`; in that
case `CMAKE_BUILD_TYPE` is ignored.

## MinGW native (Windows host)

Run from an MSYS2 shell. `SYSTEM` uses pacman/UCRT packages:
`KotonohaDepsProvider.cmake` adds `$MSYSTEM_PREFIX` (for example
`C:/msys64/ucrt64`) to `CMAKE_PREFIX_PATH` and `PKG_CONFIG_PATH`. `VCPKG` uses
the `*-mingw-dynamic` triplets and sets `VCPKG_HOST_TRIPLET` to the same triplet.

```sh
cmake -S . -B build-mingw -G Ninja -DCMAKE_BUILD_TYPE=Release \
  -DKOTONOHA_DEPS_PROVIDER=SYSTEM \
  -DKOTONOHA_BUILD_EXAMPLE=ON
cmake --build build-mingw --target Kotonoha
```

## MinGW cross (macOS / Linux host)

**vcpkg is mandatory here.** apt/Homebrew packages describe the host, not the
Windows target, so `KOTONOHA_DEPS_PROVIDER=SYSTEM` is rejected with:

```
[Kotonoha] MinGW cross-compilation requires VCPKG: system packages (apt/brew)
cannot provide Windows target libraries.
```

Prerequisites: CMake 3.21+, Ninja, `VCPKG_ROOT` and a mingw-w64 toolchain on
`PATH`. The chainload toolchain
`$VCPKG_ROOT/scripts/toolchains/mingw.cmake` locates the compilers by their
standard names on both hosts:

- `x64-mingw-dynamic`: `x86_64-w64-mingw32-g++`
- `x86-mingw-dynamic`: `i686-w64-mingw32-g++`
- `arm64-mingw-dynamic`: `aarch64-w64-mingw32-g++`

On Linux install `mingw-w64` / `gcc-mingw-w64-*` (plus `binutils-mingw-w64-*`);
on macOS use `brew install mingw-w64`. Do not pin bare compiler names in the
CMake cache: let the chainload toolchain resolve them.

```sh
cmake -S . -B build-mingw-cross -G Ninja -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_TOOLCHAIN_FILE="$VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake" \
  -DVCPKG_CHAINLOAD_TOOLCHAIN_FILE="$VCPKG_ROOT/scripts/toolchains/mingw.cmake" \
  -DVCPKG_TARGET_TRIPLET=x64-mingw-dynamic \
  -DVCPKG_OVERLAY_PORTS=vcpkg-overlays/ports \
  -DVCPKG_OVERLAY_TRIPLETS=vcpkg-overlays/triplets \
  '-DVCPKG_MANIFEST_FEATURES=sdl-vcpkg;imgui-vcpkg' \
  -DKOTONOHA_DEPS_PROVIDER=VCPKG \
  -DKOTONOHA_BUILD_EXAMPLE=ON
cmake --build build-mingw-cross --target Kotonoha
```

Replace `x64` with `x86` or `arm64` for the other targets.

## Runtime DLL deployment

`kotonoha_is_mingw_toolchain()` in
`buildsystems/cmake/packaging/KotonohaAppPackaging.cmake` detects these builds
via `WIN32` plus `MINGW` / compiler / `-mingw` triplet checks. There is no
`APPLE AND MINGW` gate: macOS-only code paths additionally require
`CMAKE_SYSTEM_NAME STREQUAL "Darwin"`, which is false for a MinGW cross-build
(`CMAKE_SYSTEM_NAME` is `Windows`). The MinGW runtime DLL search handles both
Homebrew symlinked compilers and Linux mingw-w64 installations.
