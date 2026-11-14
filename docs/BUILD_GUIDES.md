# Build guides

**English (US)** | [Português (Brasil)](BUILD_GUIDES.pt-BR.md)

Use a C++17 compiler, CMake, repository submodules, and dependencies from `vcpkg.json`. Run the following commands from the repository root unless specified otherwise.

| Target | Guide |
| --- | --- |
| Linux / Windows desktop | [Desktop section below](#desktop) |
| macOS | [macOS app bundles](BUILD_MACOS.md) |
| iOS | [Native Xcode project](BUILD_IOS.md) |
| Android | [Gradle flavors](BUILD_ANDROID.md) |
| WebAssembly | [Emscripten](BUILD_WEB.md) |
| Tests | [Build validation](BUILD_VALIDATION.md) |

## Desktop

```sh
cmake -S . -B build -DKOTONOHA_BUILD_EXAMPLE=ON
cmake --build build --target Kotonoha
ctest --test-dir build --output-on-failure
```

Set `CMAKE_TOOLCHAIN_FILE` for vcpkg if needed. To compile individual ports use `-DKOTONOHA_BUILD_<OPTION>=ON`; options include `AMAGAMI`, `ZERO`, `SNOW`, `SUMMER`, `DDLC`, `SCHOOL_DAYS`, `REALLIVE`, `KIRIKIRI`, `LUCA_SYSTEM`, and `PMAIL`. `-DKOTONOHA_BUILD_ALL=ON` requests all desktop ports and may require additional game-specific dependencies. See the [game index](../games/README.md).

Commercial assets are never downloaded by CMake. Build success does not establish gameplay completeness.
