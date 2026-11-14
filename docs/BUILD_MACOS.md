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

## Xcode

```sh
open buildsystems/xcode/Kotonoha.xcodeproj
xcodebuild -list -project buildsystems/xcode/Kotonoha.xcodeproj
```

Select a macOS scheme in Xcode; for example, `KotonohaExample (macOS)`. See [Xcode details](../buildsystems/xcode/README.md). Bundles and frameworks need runtime dependencies and appropriate resources; never add commercial game assets to Git.
