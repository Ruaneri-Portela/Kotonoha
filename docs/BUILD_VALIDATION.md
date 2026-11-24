# Build validation

**English (US)** | [Português (Brasil)](BUILD_VALIDATION.pt-BR.md)

Validate the CMake game matrix and Xcode schemes after build-system changes:

```sh
python3 tools/ValidateBuildMetadata.py
```

The read-only check verifies **16 CMake game applications** (15 game ports plus Example), **34 Xcode schemes** (16 games plus `KotonohaLauncher` on macOS and iOS), no untracked `xcode_pending` gaps, target references, dependency-preparation actions, and `.xcconfig` includes. Android has 16 game flavors plus a separate `launcher` flavor (`KOTONOHA_BUILD_GAME=all`); run `python3 tools/check_release_matrix.py` to verify both matrices. It needs only Python 3 and does not compile anything.

For native build validation, use the platform commands in [build guides](BUILD_GUIDES.md), installed SDKs/vcpkg and local game data as needed. This metadata check is not a replacement for compilation, tests, or packaging verification.


## Build readiness commands

Run from the repository root after installing the SDKs, vcpkg dependencies,
and Git submodules listed in the platform build guides:

```sh
# Native CMake: all game ports plus the in-process hub
cmake -S . -B build -DKOTONOHA_BUILD_ALL=ON
cmake --build build --parallel
ctest --test-dir build --output-on-failure

# Android Studio / Gradle: dedicated launcher application flavor
cd buildsystems/android
./gradlew :app:assembleLauncherDebug -Pabis=arm64-v8a
cd ../..

# Xcode: native macOS and iOS Simulator launchers
xcodebuild -project buildsystems/xcode/Kotonoha.xcodeproj \
  -scheme 'KotonohaLauncher (macOS)' -configuration Debug build
xcodebuild -project buildsystems/xcode/Kotonoha.xcodeproj \
  -scheme 'KotonohaLauncher (iOS)' -configuration Debug \
  -sdk iphonesimulator -destination 'generic/platform=iOS Simulator' build
```

These commands are a validation checklist, **not evidence that every platform
build has passed**. Xcode still needs the matching prepared SDL frameworks and
Apple SDK; Gradle requires its SDK/NDK and Android vcpkg dependencies.
Source-level matrix validation cannot substitute for actual builds and APK
inspection. The shared library-scan regression runs in
`Kotonoha.FilesystemV2` and `Kotonoha.UnifiedLauncher`.
