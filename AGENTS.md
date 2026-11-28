# Agent Guidelines for Kotonoha

**English (US)** | [Português (Brasil)](AGENTS.pt-BR.md)

This document provides definitive guidance for AI agents, automated coding assistants, and human contributors working within the Kotonoha repository. All agents operating on this codebase must follow these instructions, architectural rules, code style standards, and validation workflows.

---

## 1. Project Overview & Architecture

Kotonoha is a modular **C++17 visual novel engine** built upon SDL3, Dear ImGui, FFmpeg, and libass, with support for additional runtimes (such as CPython for Ren'Py/DDLC). It targets Desktop (Linux, Windows, macOS), Mobile (iOS, Android), and Web (WebAssembly via Emscripten).

### Repository Structure

| Directory / File | Description |
|---|---|
| `lib/` | Core engine library, shared interfaces, public headers (`lib/include/Kotonoha/`), and unit tests. |
| `modules/vms/` | Script virtual machines (Amagami, Artemis, KAG, MEO, N2System, Ponscripter, Pure Mail, RealLive, Ren'Py, Snow Radish, Summer Radish, TJS2). Pure script execution, independent of rendering. |
| `modules/providers/` | Archive/package containers and `Kotonoha::AssetProvider` implementations (VFS adapters, overlays, caching). |
| `modules/formats/` | Pure file-format codecs in `modules/formats/<type>/<format>/`, one folder per file format, named after the format (never a game or engine). Codecs return neutral RGBA/PCM data and depend only on the C++ standard library. |
| `games/` | 16 game applications (15 game ports and Example) (`example`, `amagami`, `zero-no-tsukaima`, `snow-radish-vacation`, `summer-radish-vacation`, `doki-doki-literature-club`, `school-days`, `reallive`, `kirikiri`, `luca-system`, `pmail`, `katawa-shoujo`, `meo`, `n2system`, `ponscripter`, `artemis`). |
| `buildsystems/` | Platform build orchestration: `cmake/` (CMake modules), `android/` (Gradle project), `xcode/` (Native `Kotonoha.xcodeproj`), `visualstudio/` (Native `Kotonoha.sln`). |
| `design/` | Canonical brand tokens (`design/tokens.json`) and generated platform icons/splash (`design/generated/`). |
| `assets/` | Shared engine resources (fonts, icons, shaders) and legal third-party notices. |
| `docs/` | Comprehensive bilingual documentation (`*.md` in en-US, `*.pt-BR.md` in pt-BR). |
| `tools/` | Build verification, metadata validation, asset packaging, and web dev utilities. |
| `launcher/` | Unified multi-engine launcher, detached host (`host/`), bridges, and desktop manager. |
| `vcpkg.json` / `vcpkg-overlays/` | Dependency manifest and custom vcpkg port/triplet overlays (`ports/`, `triplets/`). |

### Architectural Invariants

1. **Shared SDL3 Application Lifecycle**:
   - Every game port must use `Kotonoha/core/SDLCallbackApp.hpp` and declare its export using `KOTONOHA_SDL_CALLBACK_APP(MyPort)`.
   - Ports define static `init`, `event`, `iterate`, and `quit` methods.
   - **Never** write a standard `int main()` or a separate `SDL_AppInit` function.
2. **SDL RAII Resource Management**:
   - Own all SDL resources using the RAII aliases defined in `Kotonoha/core/SDL.hpp` (`WindowPtr`, `RendererPtr`, `TexturePtr`, `SurfacePtr`, `IOStreamPtr`, `AudioStreamPtr`, `FontPtr`, `SDLMemoryPtr<T>`, `SDLArrayPtr<T>`, `AudioDeviceHandle`, `GamepadPtr`, `JoystickPtr`).
   - Use `.get()` to pass borrowed raw pointers to SDL functions.
   - Use `std::move` to transfer ownership.
   - **Never** call `SDL_Destroy*` or `SDL_free` manually on an object managed by an RAII handle.
3. **Clean Separation of Concerns**:
   - VM modules (`modules/vms/`) only execute bytecode/scripts; presentation and rendering remain in `games/`.
   - Format modules (`modules/formats/<type>/<format>/`) implement one file format per folder, are game- and engine-neutral, and return neutral RGBA pixels, interleaved PCM or indexed frames. They depend only on the C++ standard library; games and providers convert to SDL/engine types.
   - Asset providers (`modules/providers/`) handle archive/package containers and implement `Kotonoha::AssetProvider` with VFS mounting, overlays, and caching.
   - Avoid circular dependencies between VMs, formats, providers, and presentation layers.
4. **Virtual Filesystem (Filesystem V2)**:
   - The engine uses a read-only virtual filesystem (`FileHandle`, `IMount`, `SDLFileSource`, `DirectoryMount`, `LayeredMount`, `ISO9660Mount`, `Filesystem::mount`).
   - Saves, preferences, and Documents directories remain strictly outside this read-only VFS.
   - Never use `std::ifstream` for virtual assets; use `SDLFileSource` and `FileHandle`.

---

## 2. Critical Policies & Constraints

### Game Assets & Legal Boundary
- **Never commit, stage, or redistribute commercial game data.** All proprietary assets must remain strictly in local, untracked directories (`games/<slug>/game-assets/`).
- Only `games/example` includes freely distributable sample assets.
- In packaging (`cpack`), retail assets are excluded by default (`-DKOTONOHA_CPACK_INCLUDE_GAME_ASSETS=OFF`).

### Historical & Forensic Records
- Files named `PORT_STATUS.md` and dated investigation notes in `games/*/docs/` are **immutable historical records**.
- **Do not delete, overwrite, or silently rewrite** these files when updating engine features or overviews.

### Bilingual Documentation Policy
- All active documentation must be maintained in pairs:
  - English (US): `docs/FOO.md`
  - Brazilian Portuguese: `docs/FOO.pt-BR.md`
- Code symbols, command-line arguments, file paths, options, and identifiers must remain **identical** between translations.
- Whenever you modify an existing guide or add a new one, update both versions simultaneously.

### Storage & Directory Policy (`FileResolver` & `StoragePaths`)
- Games must **never** construct hardcoded platform file paths.
- Use `FileResolver` and `Kotonoha/Filesystem/StoragePaths.hpp`.
- Storage locations are keyed by Application ID (`<app-id>`):
  - **macOS**: `~/Library/Application Support/Kotonoha/<app-id>`
  - **Windows**: `%APPDATA%/Kotonoha/<app-id>`
  - **Linux**: `$XDG_DATA_HOME/Kotonoha/<app-id>`
  - **iOS**: `Documents/Kotonoha/<app-id>`
  - **Android**: `Android/data/<app-id>/files/Kotonoha/<app-id>`
  - **Web**: `/save` mount backed by IndexedDB (`IDBFS`).
- Overlays: `--asset-overlay <path>` intercepts assets by logical ID before base game assets.

---

## 3. Coding Standards & Conventions

- **Language Standard**: C++17 (`set(CMAKE_CXX_STANDARD 17)`).
- **Formatting (.clang-format)**:
  - Style: LLVM
  - Indentation: 2 spaces
  - Column Limit: 80 characters
  - Include Sorting: Case-sensitive
- **EditorConfig (.editorconfig)**:
  - Line endings: LF
  - Encoding: UTF-8
  - Trailing whitespace: Trimmed
  - C++ and general files: 2 spaces
  - CMake files (`CMakeLists.txt`, `*.cmake`): 4 spaces
- **Apple & Swift**:
  - Swift 5+ / SwiftUI modern idioms (async/await, avoid Combine).
  - 4 spaces indentation, PascalCase for types, camelCase for properties/methods.
  - Native Xcode project located at `buildsystems/xcode/Kotonoha.xcodeproj`.
- **Public API Usage**:
  - Always access resources through the public `AssetProvider` interface (`read()`, `loadSurface()`, `texture()`, `loadAudio()`, `loadTextUtf8()`) to allow asset overlays and caching to function correctly.

---

## 4. Build Systems & Compilation

### Desktop (Linux, macOS, Windows) via CMake

```sh
# Configure with Example port
cmake -S . -B build -DKOTONOHA_BUILD_EXAMPLE=ON

# Build target
cmake --build build --target Kotonoha

# Build specific game ports (e.g., RealLive, DDLC, Amagami)
cmake -S . -B build -DKOTONOHA_BUILD_REALLIVE=ON
cmake --build build --target RealLive

# Build all desktop ports
cmake -S . -B build -DKOTONOHA_BUILD_ALL=ON
```

### Dependency Management (vcpkg)
- Local builds use Git submodules by default.
- CI and vcpkg-managed builds use:
  ```sh
  cmake -S . -B build \
    -DCMAKE_TOOLCHAIN_FILE="$VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake" \
    -DVCPKG_TARGET_TRIPLET=<triplet> \
    '-DVCPKG_MANIFEST_FEATURES=sdl-vcpkg' \
    -DKOTONOHA_SDL_PROVIDER=VCPKG
  ```

### Apple Platforms (macOS & iOS)
- **Native Xcode Project**: `buildsystems/xcode/Kotonoha.xcodeproj` (contains 34 shared schemes: 16 games + `KotonohaLauncher` on macOS and iOS).
- **Preparation Script**:
  ```sh
  python3 tools/PrepareApple.py
  ```
- **Build with xcodebuild**:
  ```sh
  xcodebuild -project buildsystems/xcode/Kotonoha.xcodeproj \
    -scheme 'KotonohaExample (macOS)' -configuration Debug build

  xcodebuild -project buildsystems/xcode/Kotonoha.xcodeproj \
    -scheme 'KotonohaExample (iOS)' -configuration Debug \
    -sdk iphonesimulator -destination 'generic/platform=iOS Simulator' build
  ```
- *Note*: Never run obsolete scripts such as `UpdateGameTargets.rb` or refer to `Kotonoha.xcworkspace`.

### Android via Gradle
- Located in `buildsystems/android/`.
- Requires JDK 17, Android SDK 36, NDK 30.0.16248370, CMake 3.22.1.
- ELF binaries use 16 KiB page size alignment (`-Wl,-z,max-page-size=16384`).
- Commands:
  ```sh
  cd buildsystems/android
  ./gradlew :app:assembleExampleDebug
  ./gradlew :app:assembleRealLiveDebug -Pabis=arm64-v8a
  ```

### WebAssembly (Emscripten)
- Toolchain: `emcmake cmake` with `vcpkg-overlays/ports` and `wasm32-emscripten`.
- Asyncify: `-sASYNCIFY=1` with `-O2` optimization and `-fexceptions`.
- Asset Serving: HTTP server **must** support byte ranges (`206 Partial Content`).
- Development Server:
  ```sh
  python3 tools/serve_web.py --root build-web/outputs --port 3000
  ```

---

## 5. Verification & Testing Playbook

Before committing any changes or concluding an agent task, execute the relevant verification steps:

### 1. Build Metadata Validation (Crucial)
Run immediately after modifying `CMakeLists.txt`, `KotonohaGames.cmake`, `KotonohaOptions.cmake`, Xcode schemes, `Config/*.xcconfig`, `project.pbxproj`, or Visual Studio `.vcxproj` files:
```sh
python3 tools/ValidateBuildMetadata.py
python3 tools/check_xcode_sources.py
python3 tools/check_visual_studio_sources.py
```
*Expected output (`ValidateBuildMetadata.py`)*: `[Kotonoha] OK: 16 CMake games; 34 existing Xcode schemes; Xcode pending: none`

### 2. Native C++ Tests
```sh
# Headless test execution
SDL_VIDEO_DRIVER=dummy SDL_AUDIO_DRIVER=dummy ctest --test-dir build --output-on-failure

# Targeted test suites
ctest --test-dir build -R '^Kotonoha\.FilesystemV2$' --output-on-failure
ctest --test-dir build -R 'Kotonoha\.(BuildVersion|Launcher)' --output-on-failure
```

### 3. Web & Range Server Tests
```sh
node tools/check_web_shell.js
node --check tools/web_selftest.js
python3 -m unittest discover -s tools -p test_serve_web.py -v
python3 tools/check_wasm_asyncify.py <path/to/game.js>
```

### 4. CPack & Archive Verification
```sh
python3 tools/check_game_zip.py <path/to/package.zip>
python3 tools/check_sdl_cpack.py <build_dir>
```

---

## 6. Agent Rules: DOs and DON'Ts

### DO:
- **DO** verify that both `*.md` and `*.pt-BR.md` are updated in tandem when editing documentation.
- **DO** run `python3 tools/ValidateBuildMetadata.py` whenever touching build configurations or Xcode files.
- **DO** adhere strictly to the shared SDL3 lifecycle (`KOTONOHA_SDL_CALLBACK_APP`).
- **DO** manage SDL pointers using the RAII typedefs in `Kotonoha/core/SDL.hpp`.
- **DO** check `.clang-format` and `.editorconfig` rules (2 spaces for C++, 4 spaces for CMake).
- **DO** query assets using public methods of `Kotonoha::AssetProvider` to preserve overlay and caching functionality.
- **DO** use `FileResolver` and `StoragePaths` for all user-writable and engine-asset paths.

### DO NOT:
- **DO NOT** commit or redistribute any proprietary game assets.
- **DO NOT** write a custom `main()` or duplicate `SDL_AppInit` for any game port.
- **DO NOT** invoke `SDL_Destroy*()` or `SDL_free()` on RAII-wrapped objects.
- **DO NOT** delete or overwrite `PORT_STATUS.md` or historical reverse-engineering documentation.
- **DO NOT** load large multi-hundred-megabyte archives into WebAssembly MEMFS; use HTTP Range with Fetch instead.
- **DO NOT** search for or execute nonexistent tools like `UpdateGameTargets.rb` or `Kotonoha.xcworkspace`.
- **DO NOT** hardcode absolute filesystem paths for saves or configuration files.

---

## 7. School Days HQ & Fork Collaboration Policies (`Ruaneri-Portela/Kotonoha` & `tsubaki-0x/Kotonoha`)

All core engine rules above are mandatory across both the canonical upstream repository (`Ruaneri-Portela/Kotonoha`) and the School Days HQ collaboration fork (`tsubaki-0x/Kotonoha`). See [docs/FORK_WORKFLOW.md](docs/FORK_WORKFLOW.md) for detailed DEV/DIST and QA procedures.

### Authorized Targets & Shared Architecture
- Both **Windows x64** and **Android (`arm64-v8a` / dual-ABI)** are active, authorized targets for **School Days HQ v1.02** (superseding the historical 2026-11 Windows-L4-only restriction).
- Preserve a single shared School Days game-logic implementation in `games/school-days/`, generic engine code in `lib/`, VMs/providers in `modules/`, and Android JNI/Gradle/SAF platform adapters in `buildsystems/android/`.
- Do not duplicate game logic across Windows and Android or introduce game-specific MCP/DevBridge servers (use generic MCP in `lib/`).

### Repository & Branch Roles
- `Ruaneri-Portela/Kotonoha` (`dev`): canonical upstream integration line for the multi-engine runtime, build systems, and all 16 game applications.
- `tsubaki-0x/Kotonoha` (`dev` / `hybrid`): collaboration and private QA packaging fork (`hybrid` preserves fork-specific School Days HQ DEV/DIST distribution tooling).
- Never force-push published shared branches (`dev`, `main`, or `hybrid`), and keep cross-repository PRs focused, clean, and free of game-specific symbols in `lib/`.

### DEV vs DIST Policy (Windows & Android)
- **DEV** (`-DKOTONOHA_DISTRIBUTION_BUILD=OFF -DKOTONOHA_ENABLE_MCP=ON` on Windows; `debug`/`release` with `BuildConfig.IS_DISTRIBUTION_BUILD=false` on Android): Release-optimized development build with `DebugOverlay`, MCP, and QA tooling.
- **DIST** (`-DKOTONOHA_DISTRIBUTION_BUILD=ON -DKOTONOHA_ENABLE_MCP=OFF -DKOTONOHA_DEV_CONTROL=OFF -DKOTONOHA_ENABLE_PLAYER_DUBBING=OFF`; `dist` buildType with `BuildConfig.IS_DISTRIBUTION_BUILD=true` on Android): clean player build with `DebugOverlay`, MCP, and DEV CLI options compiled out, and `WINDOWS_GUI` subsystem on Windows (no extra console window).
- On Windows, build virgin outputs with `--config Release --parallel 1` in fresh CMake directories and assemble complete private QA distributions (1 EXE + 4 DLLs + `translations/` + `overlay/` + `game-assets/` + `assets/` + `third-party-licenses/`) strictly separately from raw CMake output directories; do not blindly run legacy L4 two-DLL staging scripts (`stage_clean_local_l4.ps1`, `publish_two_dll_l4.ps1`, `stage_windows_clean_local.py`).

### Non-Negotiable Data, Asset & Testing Safety
- Never execute the retail Windows EXE/installer, alter the 29 original retail GPKs, or overwrite `LxH.GPK`.
- Never alter personal saves (`Save/`, `GlobalFlag.DAT`) or personal `Config.DAT`/`settings.json`: isolate all tests with `--documents` and `--config` in disposable directories, preferring synthetic fixtures.
- Never start a second GUI instance while `SchoolDays.exe` is already running.
- Never commit commercial GPKs, private translations, or unlicensed redistribution packs to public Git, and never publish private QA ZIPs as public releases.
- Preserve owner-authorized fork asset decisions (`assets/SPLASH.PNG` and `assets/SchoolDays.ico`, without restoring legacy `assets/icon.ico`); keep fork visual identity separate from unsolicited upstream PRs.
- Always distinguish static validation, unit/CTest execution, real-GPU visual checks, and physical POCO X7 Pro hardware QA. Do not claim hardware validation from static or Gradle checks alone.
