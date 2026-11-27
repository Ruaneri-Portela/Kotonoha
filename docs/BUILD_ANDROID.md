# Android build

**English (US)** | [Português (Brasil)](BUILD_ANDROID.pt-BR.md)

Android uses `buildsystems/android/`, with one Gradle product flavor per game. Requirements: Android Studio (or SDK command-line tools), **JDK 17**, **SDK 36**, **NDK 30.0.16248370**, **CMake 3.22.1**, Git, and vcpkg.

```sh
export VCPKG_ROOT=/path/to/vcpkg
cd buildsystems/android
./gradlew :app:assembleExampleDebug
./gradlew :app:assembleRealLiveDebug -Pabis=armeabi-v7a
```

On Windows, use `gradlew.bat` and set `VCPKG_ROOT` in the environment. Gradle reads the SDK location from Android Studio/`local.properties`. The default ABIs are `arm64-v8a,armeabi-v7a,x86_64`. Select ARMv7 NEON with `-Pabis=armeabi-v7a`, ARM64 with `-Pabis=arm64-v8a`, or both ARM ABIs with `-Pabis=arm64-v8a,armeabi-v7a`. The NDK uses `ANDROID_ARM_NEON=ON` and vcpkg uses `arm-neon-android` for ARMv7; the 32-bit device must support NEON.

Flavor examples: `Launcher` (Unified Hub), `Example`, `Amagami`, `ZeroNoTsukaima`, `SnowRadish`, `SummerRadish`, `DokiDoki`, `SchoolDays`, `RealLive`, `Kirikiri`, `LucaSystem`, `Pmail`, `Ponscripter`, and `N2System`. Install a variant using `:app:install<Flavor>Debug`.

By default Gradle **does not** package the selected flavor's retail `game-assets` (the free Example remains bundled). For private testing with properly authorized data, explicitly pass `-PbundleGameAssets=true`; the `dist` build type always excludes retail assets. Never redistribute proprietary game files without permission or commit them. See [Android project details](../buildsystems/android/README.md).
