# iOS build

**English (US)** | [Português (Brasil)](BUILD_IOS.pt-BR.md)

The checked-in native Xcode project is `buildsystems/xcode/Kotonoha.xcodeproj`. Use macOS with Xcode and the target-specific SDL and vcpkg dependencies.

```sh
open buildsystems/xcode/Kotonoha.xcodeproj
xcodebuild -list -project buildsystems/xcode/Kotonoha.xcodeproj
xcodebuild -project buildsystems/xcode/Kotonoha.xcodeproj \
  -scheme 'KotonohaExample (iOS)' \
  -configuration Debug \
  -sdk iphonesimulator \
  -destination 'generic/platform=iOS Simulator' build
```

For a physical device, select an iOS device destination and configure signing in Xcode. Game-specific schemes use the same project; see [Xcode documentation](../buildsystems/xcode/README.md).

**Important:** Older instructions reference `UpdateGameTargets.rb` and `Kotonoha.xcworkspace`. Neither is present in the current tracked tree. Work with `Kotonoha.xcodeproj`; do not run the missing generator. Build and signing depend on local SDKs and installed frameworks.
