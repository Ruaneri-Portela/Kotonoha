# Kotonoha Android

**English (US)** | [Português (Brasil)](README.pt-BR.md)

Open this folder in Android Studio. Required: **JDK 17**, Android **SDK 36**, **NDK 30.0.16248370**, **CMake 3.22.1**, Git, and a vcpkg checkout. Use the Gradle wrapper.

```sh
export VCPKG_ROOT=/path/to/vcpkg
./gradlew :app:assembleExampleDebug
./gradlew :app:assembleDokiDokiDebug -Pabis=arm64-v8a
./gradlew :app:assembleSchoolDaysDebug -Pabis=armeabi-v7a -PbundleGameAssets=false
```

Run commands from `buildsystems/android/` (PowerShell: set `$env:VCPKG_ROOT` and use `.\gradlew.bat`). All 17 product flavors: `Launcher` (unified hub), `Example`, `Amagami`, `ZeroNoTsukaima`, `SnowRadish`, `SummerRadish`, `MEO`, `KatawaShoujo`, `DokiDoki`, `SchoolDays`, `RealLive`, `Kirikiri`, `LucaSystem`, `Pmail`, `Ponscripter`, `N2System`, and `Artemis`.

- APKs are written under `app/build/outputs/apk/`. Default ABIs: `arm64-v8a,armeabi-v7a,x86_64`; use `-Pabis=armeabi-v7a` for ARMv7 only. ARMv7 is built with NEON (`ANDROID_ARM_NEON=ON`, vcpkg `arm-neon-android`), so a 32-bit device must support NEON.
- Each flavor has its own application ID; the root CMake selects only that game's native target (`KotonohaLauncherCore` for `Launcher`).
- Gradle uses the SDL submodule and vcpkg for native dependencies. DDLC's CPython dependencies have an ABI-specific installation directory (`armeabi-v7a` uses `arm-neon-android`); prefer a single ABI for DDLC builds.
- Gradle bundles engine `assets/`, but **excludes retail game `game-assets/` by default**. Only Example demo assets are always bundled. Explicit `-PbundleGameAssets=true` is for authorized private testing; Android `dist` always excludes retail game assets. Packaged resources are extracted on startup and saves stay separate.
- User data and saves are scoped under `<userData>/Kotonoha/<app-id>/` on desktop and mobile (`Android/data/<app-id>/files/Kotonoha/<app-id>/` on Android), while the multi-game `UnifiedLauncher` isolates saves per game slug.
- Folder and ZIP imports on Android use robust directory copying and SAF permission persistence.
- School Days always loads the 29 original retail packs via SAF;
  -PbundleGameAssets never embeds them. For its private portrait launcher,
  -PincludeLauncherArt=false can disable local staging of the optional art.

For external assets, overlays, OBB, and saved files, see the [directory policy](../../docs/DIRECTORY_POLICY.md). See also [Android build guide](../../docs/BUILD_ANDROID.md).
