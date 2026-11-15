# Kotonoha Android

**English (US)** | [Português (Brasil)](README.pt-BR.md)

Open this folder in Android Studio. Required: **JDK 17**, Android **SDK 36**, **NDK 28.2.13676358**, **CMake 3.22.1**, Git, and a vcpkg checkout. Use the Gradle wrapper.

```sh
export VCPKG_ROOT=/path/to/vcpkg
./gradlew :app:assembleExampleDebug
./gradlew :app:assembleDokiDokiDebug -Pabis=arm64-v8a
./gradlew :app:assembleSchoolDaysDebug -Pabis=arm64-v8a
```

Run commands from `buildsystems/android/` (PowerShell: set `$env:VCPKG_ROOT` and use `.\gradlew.bat`). Other flavors include `Amagami`, `ZeroNoTsukaima`, `SnowRadish`, `SummerRadish`, `RealLive`, `Kirikiri`, `LucaSystem`, and `Pmail`.

- APKs are written under `app/build/outputs/apk/`. Default ABIs: `arm64-v8a,x86_64`; use `-Pabis=...` to restrict them.
- Each flavor has its own application ID; the root CMake selects only that game's native target.
- Gradle uses the SDL submodule and vcpkg for native dependencies. DDLC's CPython dependencies have an ABI-specific installation directory; prefer a single ABI for DDLC builds.
- Gradle bundles engine `assets/` and the selected game's `game-assets/`; `-PbundleGameAssets=false` excludes commercial game data. Packaged resources are extracted on startup and saves stay separate.
- Do not launch concurrent Gradle invocations that compete for the same `.cxx` directory.

For external assets, overlays, OBB, and saved files, see the [directory policy](../../docs/DIRECTORY_POLICY.md). See also [Android build guide](../../docs/BUILD_ANDROID.md).
