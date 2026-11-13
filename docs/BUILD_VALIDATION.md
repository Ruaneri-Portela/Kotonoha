# Kotonoha Engine — Build Validation & Documentation

This document records the build validation and status of the Kotonoha Engine across Android, iOS, and macOS/Desktop, including the 11 supported game titles and the modular Xcode build architecture.

---

## Supported Games (11 Titles)
1. **Example** (`KotonohaExample`)
2. **Amagami** (`Amagami`)
3. **Zero No Tsukaima** (`ZeroNoTsukaima`)
4. **Snow Radish Vacation** (`SnowRadish`)
5. **Summer Radish Vacation** (`SummerRadish`)
6. **Doki Doki Literature Club** (`DokiDoki`)
7. **School Days** (`SchoolDays`)
8. **RealLive** (`RealLive`)
9. **Kirikiri** (`Kirikiri`)
10. **LucaSystem** (`LucaSystem`)
11. **Pmail** (`Pmail`)

---

## Xcode Build System & Multi-Project Architecture

The Xcode build system (`buildsystems/xcode/`) is structured into modular projects managed automatically by [UpdateGameTargets.rb](file:///Users/nero/Codes/Kotonoha/tools/UpdateGameTargets.rb):
- **[KotonohaEngine.xcodeproj](file:///Users/nero/Codes/Kotonoha/buildsystems/xcode/KotonohaEngine.xcodeproj)**: Compiles the core engine (`lib/src/`) and ImGui into shared frameworks (`KotonohaEngine.framework` and `KotonohaImGui.framework`).
- **[KotonohaDesktop.xcodeproj](file:///Users/nero/Codes/Kotonoha/buildsystems/xcode/KotonohaDesktop.xcodeproj)**: Contains targets and shared schemes for all 11 game apps on macOS.
- **[KotonohaIOS.xcodeproj](file:///Users/nero/Codes/Kotonoha/buildsystems/xcode/KotonohaIOS.xcodeproj)**: Configures all 11 game apps for iOS Simulator and devices.
- **[Kotonoha.xcworkspace](file:///Users/nero/Codes/Kotonoha/buildsystems/xcode/Kotonoha.xcworkspace)**: Unifies engine and app projects for seamless workspace builds.

### Asset Providers (`adv`, `snow-radish`, `rpa`, `pmail`, etc.)
- The ADV asset provider (`modules/providers/adv/src/SnowAssets.cpp`, `Pack.cpp`) provides shared asset loading routines (`SnowAssets`) used by game ports such as `pmail` and `snow-radish-vacation`.
- Module include paths and sources are integrated dynamically into game targets via the project generator.

---

## Build & Validation Commands

### 1. Xcode Projects Generation & Build (macOS / iOS)
```bash
# Regenerate projects from scratch using absolute paths
ruby tools/UpdateGameTargets.rb

# Build Engine and ImGui Frameworks
xcodebuild -workspace buildsystems/xcode/Kotonoha.xcworkspace -scheme KotonohaImGui -configuration Debug -arch arm64 -sdk macosx MACOSX_DEPLOYMENT_TARGET=12.0 CODE_SIGNING_ALLOWED=NO CODE_SIGN_IDENTITY="" CODE_SIGNING_REQUIRED=NO build
xcodebuild -workspace buildsystems/xcode/Kotonoha.xcworkspace -scheme KotonohaEngine -configuration Debug -arch arm64 -sdk macosx MACOSX_DEPLOYMENT_TARGET=12.0 CODE_SIGNING_ALLOWED=NO CODE_SIGN_IDENTITY="" CODE_SIGNING_REQUIRED=NO build

# Build any game app (e.g., KotonohaExample, Pmail, DokiDoki)
xcodebuild -workspace buildsystems/xcode/Kotonoha.xcworkspace -scheme KotonohaExample -configuration Debug -arch arm64 -sdk macosx MACOSX_DEPLOYMENT_TARGET=12.0 CODE_SIGNING_ALLOWED=NO CODE_SIGN_IDENTITY="" CODE_SIGNING_REQUIRED=NO build
xcodebuild -workspace buildsystems/xcode/Kotonoha.xcworkspace -scheme Pmail -configuration Debug -arch arm64 -sdk macosx MACOSX_DEPLOYMENT_TARGET=12.0 CODE_SIGNING_ALLOWED=NO CODE_SIGN_IDENTITY="" CODE_SIGNING_REQUIRED=NO build
xcodebuild -workspace buildsystems/xcode/Kotonoha.xcworkspace -scheme DokiDoki -configuration Debug -arch arm64 -sdk macosx MACOSX_DEPLOYMENT_TARGET=12.0 CODE_SIGNING_ALLOWED=NO CODE_SIGN_IDENTITY="" CODE_SIGNING_REQUIRED=NO build
```

### 2. Android Build (Gradle)
```cd buildsystems/android
./gradlew :app:assembleDebug -Pabis=arm64-v8a,x86_64 -PbundleGameAssets=false
```

---

## Verification Status
- **Xcode Compilation**: 100% success across engine, ImGui, and all 11 desktop game apps.
- **Python Embedding**: Verified exclusively for DDLC (`DokiDoki`), bundling `Python.framework` automatically into `Contents/Frameworks/`.
- **Git Hygiene**: `.gitignore` is fully configured to ignore generated `.xcodeproj/`, `.xcworkspace/`, and build artifacts.
