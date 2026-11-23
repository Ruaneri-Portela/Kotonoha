# Kotonoha

**English (US)** | [Português (Brasil)](README.pt-BR.md)

Kotonoha is a modular **C++17 visual novel engine** built with SDL3. It provides scenes, dialogue, saves, input, asset providers, media playback (FFmpeg), subtitles (libass), and developer tools (Dear ImGui). Selected ports use additional runtimes such as CPython.

> **Development status:** The active integration branch is `dev`; `main` can lag behind. Game ports have different levels of compatibility. A working scene or smoke test does **not** mean complete parity with the original game.

## Quick start

Requirements: CMake, a C++17 toolchain, Git/submodules, and the dependencies declared in [`vcpkg.json`](vcpkg.json). Set up vcpkg before configuring if it is not already integrated with your environment.

```sh
git clone --recurse-submodules https://github.com/Ruaneri-Portela/Kotonoha.git
cd Kotonoha
git switch dev  # Latest integrated development fixes
cmake -S . -B build -DKOTONOHA_BUILD_EXAMPLE=ON
cmake --build build --target Kotonoha
```

If using vcpkg explicitly, add `-DCMAKE_TOOLCHAIN_FILE="$VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake"` to the configure command. To build a port, enable its flag (for example, `-DKOTONOHA_BUILD_REALLIVE=ON`) and build the corresponding target. Run `ctest --test-dir build --output-on-failure` for configured native tests.

## Documentation

- **[Documentation index](docs/README.md)** — all guides and platform entry points.
- **[Visual identity and icon style guide](assets/branding/README.md)** — canonical K, splash, colors, themes, and cross-platform icon build.
- **[Build guides](docs/BUILD_GUIDES.md)** — desktop, Android, iOS, macOS, and WebAssembly.
- **[CI and vcpkg](docs/CI_VCPKG.md)** — one-runner Linux/WASM pipeline, artifact builds, and cache policy.
- **[Game ports](games/README.md)** — supported modules, limitations, and game-specific instructions.
- **[Modules](modules/vms/README.md)** — script VMs; **[asset providers](modules/providers/README.md)** — file and archive access.
- **[Overlays](docs/ASSET_OVERLAYS.md)**, **[storage](docs/DIRECTORY_POLICY.md)**, and **[engine settings](docs/ENGINE_SETTINGS.md)**.

## Current integration notes

The source tree includes **16 CMake/Android game applications (15 ports + Example)** (including Example, N2System and PONscripter). The shared launcher is the default for interactive starts; `--no-launcher`, `--frames`, and MCP/headless execution are handled centrally. All 16 game applications have entries in the Xcode target manifest; verify actual device and simulator builds before claiming platform readiness. See [build validation](docs/BUILD_VALIDATION.md).

Linux and WebAssembly artifacts are built sequentially in **one GitHub Actions job**. Incremental game/engine build caches are disabled; vcpkg dependency binary caching is retained. See [CI instructions](docs/CI_VCPKG.md). Source and synthetic tests cannot establish full retail-game parity.

The [standalone unified launcher](launcher/README.md) is a desktop executable that links the game runtime libraries directly (excluding Example) and runs each game in the same process; it does not spawn separate game binaries. It is generated only with `KOTONOHA_BUILD_ALL=ON` on native desktop.

## Repository layout

| Path | Purpose |
| --- | --- |
| `launcher/` | Standalone multi-engine desktop launcher |
| `lib/` | Shared engine, public headers, and tests |
| `modules/vms/` | Script virtual machines |
| `modules/providers/` | Asset and archive providers |
| `games/` | Individual game ports and demo |
| `assets/` | Shared engine resources and third-party notices |
| `buildsystems/` | CMake helpers, Android Gradle project, and native Xcode project |
| `docs/` | Cross-platform documentation |
| `tools/` | Build and asset utilities |

See the [MIT license](LICENSE), [third-party notices](THIRD_PARTY_NOTICES.md) and [license audit](docs/LICENSING.md) before distributing binaries.

## Game data and licensing

**Commercial game data is not distributed.** Use only legally obtained assets and keep them out of Git. The expected folder for local game data is `games/<game>/game-assets/`. See the corresponding [port README](games/README.md) for required files. Original-game compatibility and intellectual property remain with their respective owners.

Contributions should keep the engine platform-neutral, document new options in both languages, and preserve historical investigation records when refactoring active guides.
