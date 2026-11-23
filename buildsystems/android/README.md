# Kotonoha Android

**English (US)** | [Português (Brasil)](README.pt-BR.md)

Open this folder in Android Studio. Required: **JDK 17**, Android **SDK 36**, **NDK 28.2.13676358**, **CMake 3.22.1**, Git, and a vcpkg checkout. Use the Gradle wrapper.

```sh
export VCPKG_ROOT=/path/to/vcpkg
./gradlew :app:assembleExampleDebug
./gradlew :app:assembleDokiDokiDebug -Pabis=arm64-v8a
./gradlew :app:assembleSchoolDaysDebug -Pabis=armeabi-v7a -PbundleGameAssets=false
```

Run commands from `buildsystems/android/` (PowerShell: set `$env:VCPKG_ROOT` and use `.\gradlew.bat`). Other flavors include `Amagami`, `ZeroNoTsukaima`, `SnowRadish`, `SummerRadish`, `RealLive`, `Kirikiri`, `LucaSystem`, and `Pmail`.

- APKs are written under `app/build/outputs/apk/`. Default ABIs: `arm64-v8a,armeabi-v7a,x86_64`; use `-Pabis=armeabi-v7a` for ARMv7 only. ARMv7 is built with NEON (`ANDROID_ARM_NEON=ON`, vcpkg `arm-neon-android`), so a 32-bit device must support NEON.
- Each flavor has its own application ID; the root CMake selects only that game's native target.
- Gradle uses the SDL submodule and vcpkg for native dependencies. DDLC's CPython dependencies have an ABI-specific installation directory (`armeabi-v7a` uses `arm-neon-android`); prefer a single ABI for DDLC builds.
- Gradle bundles engine `assets/`, but **excludes retail game `game-assets/` by default**. Only Example demo assets are always bundled. Explicit `-PbundleGameAssets=true` is for authorized private testing; Android `dist` always excludes retail game assets. Packaged resources are extracted on startup and saves stay separate.
- On Android, iOS, and WebAssembly (WASM), user data and saves are stored directly in `pacote/**` (ignoring package/bundle name), while desktop standalone builds use `Kotonoha/pacote/**`. In unified launcher mode (`UnifiedLauncher`), package names are omitted for saves.
- Folder and ZIP imports on Android use robust directory copying and SAF permission persistence.
- School Days always loads the 29 original retail packs via SAF;
  -PbundleGameAssets never embeds them. For its private portrait launcher,
  -PincludeLauncherArt=false can disable local staging of the optional art.

For external assets, overlays, OBB, and saved files, see the [directory policy](../../docs/DIRECTORY_POLICY.md). See also [Android build guide](../../docs/BUILD_ANDROID.md).
