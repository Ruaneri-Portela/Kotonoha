# Android build modes

The existing Android project remains a **multi-game development harness** by
default.  Running Gradle without extra properties keeps the Generic, School
Days, Zero no Tsukaima, Snow Radish Vacation and Doki Doki targets available:

```powershell
.\gradlew.bat :app:assembleDebug
```

School Days HQ also has an opt-in distributable/test package.  It uses a
separate application id, only ships the arm64 School Days native target, and
enables the Storage Access Framework importer for the 29 retail GPK files:

```powershell
.\gradlew.bat -PschoolDaysOnly=true :app:assembleRelease
```

The dedicated package expects `VCPKG_ROOT` and `ANDROID_NDK_HOME` to be set and
uses NDK `27.0.12077973`.  Retail GPKs are **not** bundled in the APK.  The user
selects either the original `Packs` directory or its parent, and the importer
copies/validates the files into app-specific storage before native startup.

The `KOTONOHA_ANDROID_SCHOOL_DAYS_ONLY` CMake option defaults to `OFF`.  It is
only switched on by the Gradle property above; desktop builds and the default
Android harness retain the upstream multi-project target graph.
