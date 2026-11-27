# Documentation

**English (US)** | [Português (Brasil)](README.pt-BR.md)

Start with the [project README](../README.md), then choose a topic:

| Topic | Guide |
| --- | --- |
| Visual identity | [Official Kotonoha brand README](../design/README.md) |
| Build overview | [Build guides](BUILD_GUIDES.md) |
| Native dependencies | [Homebrew / apt / MSYS2 UCRT / vcpkg / submodules](DEPENDENCIES.md) |
| Windows (native solution) | [Visual Studio 18 / v145](BUILD_VISUAL_STUDIO.md) |
| Windows cross-build | [MinGW on macOS/Linux hosts](BUILD_WINDOWS.md) |
| Android | [Android](BUILD_ANDROID.md) · [Gradle details](../buildsystems/android/README.md) |
| Apple | [macOS](BUILD_MACOS.md) · [iOS](BUILD_IOS.md) · [Xcode](../buildsystems/xcode/README.md) |
| Browser | [WebAssembly](BUILD_WEB.md) |
| Tests and build scope | [Validation](BUILD_VALIDATION.md) |
| Licensing and provenance | [Repository license audit](LICENSING.md) · [Third-party notices](../THIRD_PARTY_NOTICES.md) |
| CI, build artifacts and dependency cache | [GitHub Actions and vcpkg](CI_VCPKG.md) |
| Portable outputs, shared SDL entrypoints, ZIP | [Deployment and CPack](BUILD_PACKAGES.md) |
| Assets and patches | [Overlays and translation](ASSET_OVERLAYS.md) |
| File locations | [Directory policy](DIRECTORY_POLICY.md) |
| UI, subtitles, and saved options | [Engine settings](ENGINE_SETTINGS.md) |
| Touchscreen, keyboard and controllers | [Unified input](INPUT_CONTROLS.md) |
| Debug UI, applets and Git build info | [Debug tools and SDL diagnostics](DEBUG_TOOLS.md) |
| Game ports | [Port index](../games/README.md) · [Compatibility audit](PORT_COMPATIBILITY.md) |
| Internals | [Virtual machines](../modules/vms/README.md) · [Asset providers](../modules/providers/README.md) |

## Documentation policy

Active guides are concise and maintained in **en-US** (`*.md`) and **pt-BR** (`*.pt-BR.md`). Keep command names, symbols, paths, and filenames unchanged across translations. When changing a guide, update its counterpart and verify its links.

Files named `PORT_STATUS.md`, dated investigation notes in `games/*/docs/`, third-party license/source notices, and asset-folder instructions can contain historical evidence or legal requirements. They are **reference records**, not guaranteed current feature descriptions. Do not delete or silently rewrite forensic evidence when updating an overview; use the [port index](../games/README.md) for current high-level guidance.

## Experimental virtual filesystem

The experimental, read-only [Filesystem V2](filesystem/README.md) is present in the `dev` source tree, but migration from legacy readers remains partial. The [original Portuguese architecture specification](filesystem/SPECIFICATION.pt-BR.md) remains the design reference; do not assume any game port is fully migrated without verification.

## Fork-specific Windows L4 preservation

When integrating upstream, retain the School Days HQ Windows L4 visual/behavior regression suite, and use only synthetic assets and isolated `--documents` test profiles. Never follow NTFS junctions in cleanup routines or remove original retail binaries, GPK archives, private translations, installed QA releases or real saves. Earlier QA captures document their original test conditions, not necessarily the latest engine state. The old fork documentation index is preserved in the independent pre-sync archive.
