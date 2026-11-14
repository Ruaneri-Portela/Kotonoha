# Build validation

**English (US)** | [Português (Brasil)](BUILD_VALIDATION.pt-BR.md)

Build and test the specific targets you changed. **A successful compilation is not proof of complete game parity.** Tests depending on original game data must be configured locally; do not upload those assets or test results containing them.

## Desktop

```sh
cmake -S . -B build -DKOTONOHA_BUILD_EXAMPLE=ON -DBUILD_TESTING=ON
cmake --build build
ctest --test-dir build --output-on-failure
```

For a game, pass its `KOTONOHA_BUILD_*` option and build its native target; filter tests with `ctest --test-dir build -R '<game>' --output-on-failure`.

## Android

```sh
cd buildsystems/android
./gradlew :app:assembleExampleDebug -Pabis=arm64-v8a
```

For other flavors, replace `Example` with the flavor name. Run Gradle builds sequentially when they share a native build directory.

## Apple

```sh
xcodebuild -list -project buildsystems/xcode/Kotonoha.xcodeproj
xcodebuild -project buildsystems/xcode/Kotonoha.xcodeproj \
  -scheme 'KotonohaExample (macOS)' -configuration Debug build
```

Use the `(iOS)` scheme with a simulator/device destination to build iOS. The Xcode project is checked in; the older Ruby generator/workspace paths are obsolete.

## Scope of verification

Record **platform, compiler/SDK, configuration, target, test command, and result** for every validation run. Historical claims in investigation notes describe the machine and revision tested, not a standing guarantee for the current tree.
