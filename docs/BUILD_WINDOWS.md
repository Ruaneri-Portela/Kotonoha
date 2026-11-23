# Windows cross-build (MinGW)

**English (US)** | [Português (Brasil)](BUILD_WINDOWS.pt-BR.md)

Kotonoha can cross-compile Windows executables from **macOS (Darwin) and Linux**
hosts with mingw-w64 and vcpkg. The `mingw-*` CMake presets are enabled on both
hosts; no host-specific preset is required.

## Prerequisites

- CMake 3.21 or newer (presets schema version 3) and Ninja.
- vcpkg checked out, with `VCPKG_ROOT` pointing at its root directory.
- A mingw-w64 toolchain on `PATH`. The vcpkg chainload toolchain
  (`$VCPKG_ROOT/scripts/toolchains/mingw.cmake`) locates the compilers by their
  standard names on both hosts:
  - `x64-mingw-dynamic`: `x86_64-w64-mingw32-g++`
  - `x86-mingw-dynamic`: `i686-w64-mingw32-g++`
  - `arm64-mingw-dynamic`: `aarch64-w64-mingw32-g++`

  On Linux these packages are typically `mingw-w64` / `gcc-mingw-w64-*`
  (plus `binutils-mingw-w64-*`); on macOS they are commonly installed via
  Homebrew (`brew install mingw-w64`). The names above are identical on both
  hosts, so no preset changes are needed.

## Build

```sh
cmake --preset mingw-x64
cmake --build --preset mingw-x64
```

Replace `mingw-x64` with `mingw-x86` or `mingw-arm64` for the other
architectures. The presets use the `*-mingw-dynamic` vcpkg triplets, the
`vcpkg-overlays/ports` and `vcpkg-overlays/triplets` overlays, and the
`sdl-vcpkg` manifest feature. Build directories are
`out/build/<preset-name>`; Windows executables are emitted under
`out/build/<preset-name>/outputs/<target>/`, together with the MinGW and vcpkg
runtime DLLs.

`PKG_CONFIG_LIBDIR` is derived from `${sourceDir}` per preset, so it works on
either host without editing. No `/opt/homebrew` or other macOS-only path is
baked into these presets.

## Host gating

`mingw-cross-base` uses this condition (CMake preset schema version 3):

```json
"condition": { "type": "inList", "string": "${hostSystemName}", "list": ["Darwin", "Linux"] }
```

The `mingw-x64`, `mingw-x86` and `mingw-arm64` configure presets inherit it.
`cmake --list-presets` therefore lists them on macOS and Linux, and omits them
on other hosts. CMake's condition type is `inList` (with `string` / `list`);
the older `equals` form for a single host remains valid for the other presets.

## Runtime DLL deployment

The CMake helper `kotonoha_is_mingw_toolchain()` in
`buildsystems/cmake/packaging/KotonohaAppPackaging.cmake` detects these builds
via `WIN32` plus `MINGW` / compiler / `-mingw` triplet checks. There is no
`APPLE AND MINGW` gate: macOS-only code paths additionally require
`CMAKE_SYSTEM_NAME STREQUAL "Darwin"`, which is false for a MinGW cross-build
(`CMAKE_SYSTEM_NAME` is `Windows`). The MinGW runtime DLL search handles both
Homebrew symlinked compilers and Linux mingw-w64 installations.
