# Building for Android

This guide provides instructions for building and running Kotonoha on Android devices and emulators.

## Prerequisites

- **Android Studio** (Koala or newer recommended).
- **Android SDK / NDK** (configured via Android Studio or Gradle).
- **Gradle** (included wrapper in `buildsystems/android`).

## Build Steps

### 1. Project Structure
The Android sub-project is located in [`buildsystems/android`](file:///Users/nero/Codes/Kotonoha/buildsystems/android).

### 2. Gradle Build
Open `buildsystems/android` in Android Studio, or build via command line:

```bash
cd buildsystems/android
./gradlew app:assembleDebug
```

### 3. Deployment
Deploy the APK to a connected Android device or emulator:

```bash
./gradlew app:installDebug
```

For detailed setup instructions in Portuguese, refer to [`buildsystems/android/README.pt-BR.md`](file:///Users/nero/Codes/Kotonoha/buildsystems/android/README.pt-BR.md).
