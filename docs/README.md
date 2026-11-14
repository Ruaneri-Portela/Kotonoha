# Documentation

**English (US)** | [Português (Brasil)](README.pt-BR.md)

Start with the [project README](../README.md), then choose a topic:

| Topic | Guide |
| --- | --- |
| Build overview | [Build guides](BUILD_GUIDES.md) |
| Android | [Android](BUILD_ANDROID.md) · [Gradle details](../buildsystems/android/README.md) |
| Apple | [macOS](BUILD_MACOS.md) · [iOS](BUILD_IOS.md) · [Xcode](../buildsystems/xcode/README.md) |
| Browser | [WebAssembly](BUILD_WEB.md) |
| Tests and build scope | [Validation](BUILD_VALIDATION.md) |
| Assets and patches | [Overlays and translation](ASSET_OVERLAYS.md) |
| File locations | [Directory policy](DIRECTORY_POLICY.md) |
| UI, subtitles, and saved options | [Engine settings](ENGINE_SETTINGS.md) |
| Game ports | [Port index](../games/README.md) |
| Internals | [Virtual machines](../modules/vms/README.md) · [Asset providers](../modules/providers/README.md) |

## Documentation policy

Active guides are concise and maintained in **en-US** (`*.md`) and **pt-BR** (`*.pt-BR.md`). Keep command names, symbols, paths, and filenames unchanged across translations. When changing a guide, update its counterpart and verify its links.

Files named `PORT_STATUS.md`, dated investigation notes in `games/*/docs/`, third-party license/source notices, and asset-folder instructions can contain historical evidence or legal requirements. They are **reference records**, not guaranteed current feature descriptions. Do not delete or silently rewrite forensic evidence when updating an overview; use the [port index](../games/README.md) for current high-level guidance.
