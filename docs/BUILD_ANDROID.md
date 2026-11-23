# Android build

**English (US)** | [Português (Brasil)](BUILD_ANDROID.pt-BR.md)

Android uses `buildsystems/android/`, with one Gradle product flavor per game. Requirements: Android Studio (or SDK command-line tools), **JDK 17**, **SDK 36**, **NDK 28.2.13676358**, **CMake 3.22.1**, Git, and vcpkg.

```sh
export VCPKG_ROOT=/path/to/vcpkg
cd buildsystems/android
./gradlew :app:assembleExampleDebug
./gradlew :app:assembleRealLiveDebug -Pabis=armeabi-v7a
```

On Windows, use `gradlew.bat` and set `VCPKG_ROOT` in the environment. Gradle reads the SDK location from Android Studio/`local.properties`. The default ABIs are `arm64-v8a,armeabi-v7a,x86_64`. Select ARMv7 NEON with `-Pabis=armeabi-v7a`, ARM64 with `-Pabis=arm64-v8a`, or both ARM ABIs with `-Pabis=arm64-v8a,armeabi-v7a`. The NDK uses `ANDROID_ARM_NEON=ON` and vcpkg uses `arm-neon-android` for ARMv7; the 32-bit device must support NEON.

Flavor examples: `Example`, `Amagami`, `ZeroNoTsukaima`, `SnowRadish`, `SummerRadish`, `DokiDoki`, `SchoolDays`, `RealLive`, `Kirikiri`, `LucaSystem`, and `Pmail`. Install a variant using `:app:install<Flavor>Debug`.

By default Gradle packages that flavor's `game-assets`. Set `-PbundleGameAssets=false` when using external data (the example remains bundled). Avoid committing licensed assets. See [Android project details](../buildsystems/android/README.md).
