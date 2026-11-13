# Building for iOS

This guide provides instructions for building Kotonoha for iOS devices and simulators using the native Xcode project.

## Prerequisites

- **macOS** with **Xcode** installed.
- iOS SDK and Metal toolchain.

## Build Steps

### 1. Xcode Project
The native Xcode workspace and project are located in [`buildsystems/xcode/Kotonoha.xcodeproj`](file:///Users/nero/Codes/Kotonoha/buildsystems/xcode).

### 2. Opening and Compiling
1. Open the project in Xcode:
   ```bash
   open buildsystems/xcode/Kotonoha.xcodeproj
   ```
2. Select the desired iOS target scheme and destination (Simulator or physical device).
3. Build and run using `Cmd + R` or via command line with `xcodebuild`:
   ```bash
   xcodebuild -project buildsystems/xcode/Kotonoha.xcodeproj -scheme Kotonoha -destination 'platform=iOS Simulator,name=iPhone 16' build
   ```

### 3. Sychronizing Game Targets
If you add or modify game targets or sources, run the Ruby synchronization helper:
```bash
ruby buildsystems/xcode/UpdateGameTargets.rb
```
