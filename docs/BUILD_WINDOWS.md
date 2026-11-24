# Windows builds (MSVC, MinGW and MinGW cross)

**English (US)** | [Português (Brasil)](BUILD_WINDOWS.pt-BR.md)

All CMake presets use Ninja and `out/build/<preset-name>`. Canonical names are
`<platform>-<arch>-<toolchain>-<provider>-<config>`, for example
`windows-x64-msvc-vcpkg-release`. `configuration` in a build preset is
informational for Ninja (single-config): `CMAKE_BUILD_TYPE` from the configure
preset is authoritative.

## Matrix

| Host | Toolchain | Arch | Provider | Presets |
| --- | --- | --- | --- | --- |
| Windows | MSVC | x64, x86, arm64 | vcpkg | `windows-<arch>-msvc-vcpkg-{debug,release}` |
| Windows | MSVC | x64, x86, arm64 | system | `windows-<arch>-msvc-system-{debug,release}` |
| Windows | MinGW native | x64, x86, arm64 | vcpkg | `windows-<arch>-mingw-vcpkg-{debug,release}` |
| Windows | MinGW native | x64, x86, arm64 | system (MSYS2 UCRT/pacman) | `windows-<arch>-mingw-system-{debug,release}` |
| macOS / Linux | MinGW cross | x64, x86, arm64 | **vcpkg only** | `windows-<arch>-mingw-cross-vcpkg-{debug,release}` |

Compatibility aliases (same settings, historical build directories):

| Alias | Equivalent |
| --- | --- |
| `mingw-x64` | `windows-x64-mingw-cross-vcpkg-release` |
| `mingw-x86` | `windows-x86-mingw-cross-vcpkg-release` |
| `mingw-arm64` | `windows-arm64-mingw-cross-vcpkg-release` |
| `windows-ucrt-system` | `windows-x64-mingw-system-release` (Example-only build selection) |

## MSVC (Windows host)

Use the Visual Studio developer prompt matching the target architecture
(`vcvarsall.bat x64`, `x86` or `arm64`) so `cl.exe` and the SDK are visible.
The presets set `VCPKG_TARGET_TRIPLET` (`x64-windows`, `x86-windows` or
`arm64-windows`) and, for vcpkg builds, `VCPKG_ROOT` must point to the vcpkg
checkout. MSVC + `SYSTEM` dependencies require an ABI-compatible installation
(never mix MSYS2 `*.dll.a` with MSVC `*.lib`); CMake prints a warning for
that combination.

```sh
cmake --preset windows-x64-msvc-vcpkg-release
cmake --build --preset windows-x64-msvc-vcpkg-release
cmake --preset windows-arm64-msvc-system-debug
cmake --build --preset windows-arm64-msvc-system-debug
```

With the **Visual Studio generator** instead of the presets' Ninja, configure
once with `-DCMAKE_CONFIGURATION_TYPES=Debug;Release` and build with
`cmake --build <dir> --config Debug` (or `--config Release`); in that mode
`CMAKE_BUILD_TYPE` is ignored and the build preset `configuration` field is the
one that matters. These checked-in presets intentionally use Ninja everywhere
for a uniform single-config matrix.

## MinGW native (Windows host)

Run from an MSYS2 shell. `SYSTEM` is the pacman/UCRT path: `$MSYSTEM_PREFIX`
(e.g. `C:/msys64/ucrt64`) is added to `CMAKE_PREFIX_PATH` and `PKG_CONFIG_PATH`
by `KotonohaDepsProvider.cmake`. `VCPKG` uses the `*-mingw-dynamic` triplets and
sets `VCPKG_HOST_TRIPLET` to the same triplet.

```sh
# MSYS2 UCRT64 shell, pacman packages only
cmake --preset windows-x64-mingw-system-release
cmake --build --preset windows-x64-mingw-system-release

# vcpkg triplet x64-mingw-dynamic
cmake --preset windows-x64-mingw-vcpkg-debug
cmake --build --preset windows-x64-mingw-vcpkg-debug
```

## MinGW cross (macOS / Linux host)

**vcpkg is mandatory here.** apt/Homebrew packages describe the host, not the
Windows target, so `KOTONOHA_DEPS_PROVIDER=SYSTEM` is rejected with:

```
[Kotonoha] MinGW cross-compilation requires VCPKG: system packages (apt/brew)
cannot provide Windows target libraries.
```

Prerequisites: CMake 3.21+ (preset schema version 3), Ninja, `VCPKG_ROOT`, and a
mingw-w64 toolchain on `PATH`. The vcpkg chainload toolchain
`$VCPKG_ROOT/scripts/toolchains/mingw.cmake` locates the compilers by their
standard names on both hosts:

- `x64-mingw-dynamic`: `x86_64-w64-mingw32-g++`
- `x86-mingw-dynamic`: `i686-w64-mingw32-g++`
- `arm64-mingw-dynamic`: `aarch64-w64-mingw32-g++`

On Linux install `mingw-w64` / `gcc-mingw-w64-*` (plus `binutils-mingw-w64-*`);
on macOS use `brew install mingw-w64`. Do **not** pin bare compiler names in
the preset cache: CMake would resolve them against the source directory. The
chainload toolchain handles them.

```sh
cmake --preset windows-x64-mingw-cross-vcpkg-release
cmake --build --preset windows-x64-mingw-cross-vcpkg-release

# historical alias, same configuration
cmake --preset mingw-x64
cmake --build --preset mingw-x64
```

Replace `x64` with `x86` or `arm64` for the other targets. The presets export
`PKG_CONFIG_LIBDIR` from their own build directory and use the
`vcpkg-overlays/ports` and `vcpkg-overlays/triplets` overlays plus the
`sdl-vcpkg;imgui-vcpkg` manifest features.

## Host gating

`windows-base` uses `{"type":"equals","lhs":"${hostSystemName}","rhs":"Windows"}`;
`mingw-cross-base` uses
`{"type":"inList","string":"${hostSystemName}","list":["Darwin","Linux"]}`.
`cmake --list-presets` therefore shows only the applicable family on each
host.

## Runtime DLL deployment

`kotonoha_is_mingw_toolchain()` in
`buildsystems/cmake/packaging/KotonohaAppPackaging.cmake` detects these builds
via `WIN32` plus `MINGW` / compiler / `-mingw` triplet checks. There is no
`APPLE AND MINGW` gate: macOS-only code paths additionally require
`CMAKE_SYSTEM_NAME STREQUAL "Darwin"`, which is false for a MinGW cross-build
(`CMAKE_SYSTEM_NAME` is `Windows`). The MinGW runtime DLL search handles both
Homebrew symlinked compilers and Linux mingw-w64 installations.
