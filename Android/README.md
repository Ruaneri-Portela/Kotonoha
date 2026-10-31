# Kotonoha Android

English | [Portugues (Brasil)](README.pt-BR.md)

Open this directory in Android Studio. Requirements: JDK 17 or newer
compatible with Gradle 8.14.5 (Android Studio's bundled JDK works), SDK 36,
CMake 3.22.1, NDK 28.2.13676358, Git and a vcpkg checkout.

```sh
export VCPKG_ROOT=/path/to/vcpkg
export ANDROID_HOME=/path/to/Android/sdk
export ANDROID_NDK_HOME="$ANDROID_HOME/ndk/28.2.13676358"
export JAVA_HOME="/Applications/Android Studio.app/Contents/jbr/Contents/Home"
./gradlew :app:assembleExampleDebug
./gradlew :app:assembleZeroNoTsukaimaDebug
./gradlew :app:assembleSnowRadishDebug
./gradlew :app:assembleDokiDokiDebug
./gradlew :app:assembleSchoolDaysDebug
```

Each flavor has its own application ID and APK under
`app/build/outputs/apk/`. The default ABI is `arm64-v8a`; pass
`-Pabis=x86_64` for an emulator. Build DDLC for one ABI per APK.
Run Gradle invocations sequentially: concurrent builds share native `.cxx`
configuration directories. Multiple assemble tasks may be passed to one invocation.
Dependencies are installed into `build/vcpkg`; DDLC enables the manifest's
`android-python` feature. No macOS/iOS Python library is used by Android.

SDL is a remote Git dependency, pinned to
`4bb5020be2a3e3f7a963b84c95f49fe7ef5d35a4` in [app/build.gradle](app/build.gradle).
`prepareSDL` checks out the official SDL repository into ignored `.deps/SDL`.
Both SDLActivity Java sources and CMake use this checkout. SDL_ttf and ImGui
use the existing repository submodules; FFmpeg, json-c, libass, iconv, PlutoSVG and
optional CPython come from vcpkg.
DDLC uses a separate `build/vcpkg-python` install tree so other game flavors
cannot remove its optional CPython dependency during configuration.

Each APK contains engine `assets/` and its game's `game-assets/`. Gradle
stages resources, computes a content fingerprint and includes only the selected
game. BootstrapActivity extracts the APK on a worker thread before starting
SDL, displays preparation failures, and leaves settings/saves outside resources.
`-PbundleGameAssets=false` excludes local commercial game data.
Supply your own legal game data in `../games/<game>/game-assets/`; do not commit it.

Preferred data is `Android/obb/<applicationId>`, while preferred documents
are `Android/data/<applicationId>/files`, resolved through platform APIs.
Put external data in `OBB/game-assets/` (or directly in the OBB directory);
game defaults check these paths before packaged data. Settings, saves and
persistent state use the documents directory. Android's scoped-storage rules
may restrict access from file managers; this project requests no broad storage
permission and does not invent an `Android/obb-data` directory.

The extracted resources are private, fingerprinted by APK contents.
Old resource versions remain private to the app; uninstalling removes them.
Native dialogs may be unavailable on Android; the MPV inspector also offers a
bundled-shader selection list.
