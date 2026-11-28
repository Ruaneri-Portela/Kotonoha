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

### Windows: vcpkg build-python name mismatch

The pinned vcpkg `python3` port builds a host CPython into
`buildsystems/android/build/vcpkg-python/<abi>/x64-windows/tools/python3/`, but
its Android `configure` step passes `--with-build-python=.../python3.14`. On
Windows the port installs `python.exe` (plus `python314.dll`), not `python3.14`,
so the first build fails with `invalid or missing build python binary`. After
that first attempt, create the alias and run the build again:

```powershell
Get-ChildItem buildsystems/android/build/vcpkg-python -Directory | ForEach-Object {
  $p = Join-Path $_.FullName 'x64-windows/tools/python3'
  if (Test-Path "$p/python.exe") { Copy-Item "$p/python.exe" "$p/python3.14.exe" -Force }
}
```

The alias lives in the gitignored build tree; recreate it after removing
`build/vcpkg-python/<abi>`.

A second upstream CPython issue can appear in `make altinstall`:
`Lib/sysconfig/__main__.py` looks for `PC/pyconfig.h` in the cross-build
directory, while CPython writes `pyconfig.h` at that directory's root. Copying
it to `<abi-build-dir>/PC/pyconfig.h` satisfies configure, but vcpkg
re-extracts sources on every retry, so the copy must be repeated after a failed
attempt. Until the pinned vcpkg port is fixed, flavors that need CPython
(`DokiDoki`, `KatawaShoujo`, `Launcher`) may still fail on a Windows host;
non-Python flavors such as `Example` build for all ABIs with
`:app:assembleExampleDebug`.

All 17 product flavors: `Launcher` (Unified Hub), `Example`, `Amagami`, `ZeroNoTsukaima`, `SnowRadish`, `SummerRadish`, `MEO`, `KatawaShoujo`, `DokiDoki`, `SchoolDays`, `RealLive`, `Kirikiri`, `LucaSystem`, `Pmail`, `Ponscripter`, `N2System`, and `Artemis`. Install a variant using `:app:install<Flavor>Debug`.

By default Gradle **does not** package the selected flavor's retail `game-assets` (the free Example remains bundled). For private testing with properly authorized data, explicitly pass `-PbundleGameAssets=true`; the `dist` build type always excludes retail assets. Never redistribute proprietary game files without permission or commit them. See [Android project details](../buildsystems/android/README.md).
