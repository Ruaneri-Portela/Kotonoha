# Android build

**English (US)** | [Português (Brasil)](BUILD_ANDROID.pt-BR.md)

Android uses `buildsystems/android/`, with one Gradle product flavor per game. Requirements: Android Studio (or SDK command-line tools), **JDK 17**, **SDK 36**, **NDK 28.2.13676358**, **CMake 3.22.1**, Git, and vcpkg.

```sh
export VCPKG_ROOT=/path/to/vcpkg
cd buildsystems/android
./gradlew :app:assembleExampleDebug
./gradlew :app:assembleRealLiveDebug -Pabis=arm64-v8a
```

On Windows, use `gradlew.bat` and set `VCPKG_ROOT` in the environment. Gradle reads the SDK location from Android Studio/`local.properties`. The default ABIs are `arm64-v8a,x86_64`; select a subset with `-Pabis=arm64-v8a`.

Flavor examples: `Example`, `Amagami`, `ZeroNoTsukaima`, `SnowRadish`, `SummerRadish`, `DokiDoki`, `SchoolDays`, `RealLive`, `Kirikiri`, `LucaSystem`, and `Pmail`. Install a variant using `:app:install<Flavor>Debug`.

By default Gradle packages that flavor's `game-assets`. Set `-PbundleGameAssets=false` when using external data (the example remains bundled). Avoid committing licensed assets. See [Android project details](../buildsystems/android/README.md).
