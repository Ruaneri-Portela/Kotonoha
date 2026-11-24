# Build guides

**English (US)** | [Português (Brasil)](BUILD_GUIDES.pt-BR.md)

Use a C++17 compiler and CMake. Native dependencies can come from system
packages (Homebrew, apt, MSYS2 UCRT), vcpkg or the pinned SDL submodules — see
[native dependency providers](DEPENDENCIES.md). Run the following commands
from the repository root unless specified otherwise.

| Target | Guide |
| --- | --- |
| Linux / Windows desktop | [Desktop section below](#desktop) |
| Windows (MinGW cross from macOS/Linux) | [MinGW cross-build](BUILD_WINDOWS.md) |
| macOS | [macOS app bundles](BUILD_MACOS.md) |
| iOS | [Native Xcode project](BUILD_IOS.md) |
| Android | [Gradle flavors](BUILD_ANDROID.md) |
| WebAssembly | [Emscripten](BUILD_WEB.md) |
| Tests | [Build validation](BUILD_VALIDATION.md) |

## CMake presets

`CMakePresets.json` covers 52 visible configure/build preset pairs with the
canonical name `<platform>-<arch>-<toolchain>-<provider>-<config>`:

| Family | Name pattern | Providers |
| --- | --- | --- |
| macOS native | `macos-{arm64,amd64}-native-{vcpkg,system}-{debug,release}` | Homebrew or vcpkg |
| Linux native | `linux-{x64,arm64}-native-{vcpkg,system}-{debug,release}` | apt or vcpkg |
| Windows MSVC | `windows-{x64,x86,arm64}-msvc-{vcpkg,system}-{debug,release}` | MSVC/native packages or vcpkg |
| Windows MinGW native | `windows-{x64,x86,arm64}-mingw-{vcpkg,system}-{debug,release}` | MSYS2 UCRT/pacman or vcpkg |
| Windows MinGW cross | `windows-{x64,x86,arm64}-mingw-cross-vcpkg-{debug,release}` | **vcpkg only** |
| WebAssembly | `wasm-vcpkg-{debug,release}` | **vcpkg only** |

All presets use Ninja (single-config: `CMAKE_BUILD_TYPE` is authoritative and
the build preset `configuration` field is informational) and
`out/build/<preset>` as the build directory. Compatibility aliases
`mingw-{x64,x86,arm64}` (cross vcpkg Release) and `windows-ucrt-system` (native
UCRT Release) keep historical names working. Native dependency providers are
selected by `KOTONOHA_DEPS_PROVIDER`, `KOTONOHA_SDL_PROVIDER` and
`KOTONOHA_IMGUI_PROVIDER`; see [dependency providers](DEPENDENCIES.md).

## Desktop

```sh
cmake -S . -B build -DKOTONOHA_BUILD_EXAMPLE=ON
cmake --build build --target Kotonoha
ctest --test-dir build --output-on-failure
```

Set `CMAKE_TOOLCHAIN_FILE` for vcpkg if needed. To compile individual ports use `-DKOTONOHA_BUILD_<OPTION>=ON`; options include `AMAGAMI`, `ZERO`, `SNOW`, `SUMMER`, `MEO`, `DDLC`, `KATAWA`, `SCHOOL_DAYS`, `REALLIVE`, `KIRIKIRI`, `LUCA_SYSTEM`, `PMAIL`, `PONSCRIPTER`, `N2SYSTEM`, and `ARTEMIS`. `-DKOTONOHA_BUILD_ALL=ON` requests all desktop ports and may require additional game-specific dependencies. See the [game index](../games/README.md). All 16 applications (including Example) have 32 entries in the Xcode target manifest; device/simulator build success still needs individual verification.

The desktop `KotonohaLauncher` target exists **only** with `-DKOTONOHA_BUILD_ALL=ON`; it links all 15 game-port runtime libraries except the independent Example demo and runs games in the same process. See [launcher guide](../launcher/README.md).

For GitHub Actions release artifacts, Linux and WASM run sequentially on one runner without incremental build caches; see [CI/vcpkg details](CI_VCPKG.md).

Commercial assets are never downloaded by CMake. Build success does not establish gameplay completeness.
