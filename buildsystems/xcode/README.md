# Kotonoha Xcode (macOS and iOS)

**English (US)** | [Português (Brasil)](README.pt-BR.md)

Use the **checked-in** project: `buildsystems/xcode/Kotonoha.xcodeproj`. Select a macOS or iOS scheme for the desired game. The tree currently contains a single `Kotonoha.xcodeproj`, not the older multi-project workspace described in historical notes.

```sh
xcodebuild -list -project buildsystems/xcode/Kotonoha.xcodeproj
xcodebuild -project buildsystems/xcode/Kotonoha.xcodeproj \
  -scheme 'KotonohaExample (macOS)' \
  -configuration Debug build
xcodebuild -project buildsystems/xcode/Kotonoha.xcodeproj \
  -scheme 'KotonohaExample (iOS)' -configuration Debug \
  -sdk iphonesimulator \
  -destination 'generic/platform=iOS Simulator' build
```

**Dependencies:** Configure `Config/*.xcconfig` for locally installed SDL3, SDL3_ttf, FFmpeg, libass, font libraries, and vcpkg libraries. The DokiDoki target additionally requires platform-matching CPython frameworks/standard-library resources; inspect `tools/PrepareApple.py` for the available preparation workflow. SDL frameworks must match the selected SDK and be embedded into application bundles. Commercial game data should remain local.

**Current port coverage:** All 16 game applications plus `KotonohaLauncher` have macOS and iOS schemes (34 total). `ProjectManifest.json` records `xcode_pending: []`; adding a port still requires a real device/simulator build before claiming Apple readiness.

**Limitations:** Codesigning is required to run on real iOS devices. For build troubleshooting and platforms, see [macOS](../../docs/BUILD_MACOS.md), [iOS](../../docs/BUILD_IOS.md), and [validation](../../docs/BUILD_VALIDATION.md).
