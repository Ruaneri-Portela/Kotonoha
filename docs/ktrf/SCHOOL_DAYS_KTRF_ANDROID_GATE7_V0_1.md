# School Days KTRF Android v0.1 — Gate 7

Gate 7 is the minimum Android boot proof after the desktop KTRF path was closed by Gate 6.

It does **not** attempt final Android packaging or gameplay fidelity. It proves that the existing arm64-v8a Android target can launch the production KTRF runtime, load the production `.ktnroute`, resolve New Game, and create the first physical School Days ORS on a real Android device.

## Scope

```text
Android / arm64-v8a
  -> KtrfGate7Activity (adb-only development entry point)
  -> app-private files/assets staging
  -> Kotonoha SDL activity
  -> -K <school-days-hq.ktnroute> <ors-root>
  -> SchoolDaysKtrfAppController
  -> SchoolDaysKtrfGameplayBridge
  -> SchoolDaysKtrfSession
  -> native KTRF router
  -> 00/00-00-A00.ENG.ORS
  -> Gameplay
```

The normal `MainActivity` launcher is deliberately not modified by this gate.

## Why Gate 7 has its own Activity

The current Android launcher is still being developed independently. Gate 7 therefore registers `KtrfGate7Activity`, which is not a launcher icon and is started only through adb. It obtains the app-private files directory at runtime and constructs absolute paths without hard-coding `/data/data/...`.

This also keeps local experiments in `MainActivity.java` and `app/build.gradle` out of the Gate 7 patch.

## Minimal asset staging

The runner does not package the complete School Days asset tree. It creates:

```text
build/ktrf/school-days-android-gate7-assets/
  school-days-hq.ktnroute
  styles.skot
  fonts/
  00/00-00-A00.ENG.ORS
  Movie00/00-00/00-00-A00/
  Se00/00-00/00-00-A00/
  Voice00/00-00/00-00-A00/
  BGM/SD_BGM/SDBGM07_LOOP.OGG
```

A temporary Gradle init script replaces the app's Android asset source set with this staging tree for the smoke build only. The tracked `app/build.gradle` is not rewritten.

## Run

Requirements:

- Java/JDK configured for the existing Android project;
- `VCPKG_ROOT` set;
- `ANDROID_NDK_HOME` set;
- Android SDK / `adb` available;
- one authorized arm64 Android device connected by adb;
- the desktop School Days asset root already populated.

From repository root:

```powershell
powershell -ExecutionPolicy Bypass -File `
  .\tools\ktrf\run_school_days_ktrf_android_gate7_v0_1.ps1
```

For multiple connected devices:

```powershell
powershell -ExecutionPolicy Bypass -File `
  .\tools\ktrf\run_school_days_ktrf_android_gate7_v0_1.ps1 `
  -DeviceSerial "<adb-serial>"
```

Useful rerun switches:

```text
-SkipRouteBuild
-SkipGradleBuild
-SkipInstall
-SkipLaunch
```

`-SkipLaunch` is build-only and cannot produce a Gate 7 PASS.

## Deterministic app-private staging

Before the smoke launch the runner clears the development app's private data after installation. This is deliberate: `ExtractAssets` does not overwrite files that already exist, so a clean app-private tree prevents a stale `.ktnroute` or ORS from satisfying the test.

## Mechanical PASS

The runner captures logcat and requires both markers:

```text
[KTRF-GATE7] launch route=...
[KTRF-APP] enabled scene=00/00-00-A00 ...
```

Expected banner:

```text
=========================================================
 SCHOOL DAYS KTRF ANDROID v0.1 / GATE 7: PASS
=========================================================
```

A PASS proves:

- Android APK build completed for the existing arm64 target;
- the adb-only Gate 7 activity launched;
- APK assets were materialized into app-private storage;
- the native KTRF document opened on Android;
- New Game resolved to `00/00-00-A00`;
- the first physical ORS reached `Gameplay`.

## Manual observation

Before closing the app, confirm that the first School Days scene reaches the screen. Missing visual fidelity, transitions, Event sprites, feelings HUD, save/load, touch polish, and full-game asset deployment are intentionally outside Gate 7.

Those belong to the later gameplay-fidelity work and Gate 8 Android end-to-end validation.
