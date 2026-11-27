# Kotonoha Unified Multi-Engine Hub (`KotonohaLauncher`)

[Português (Brasil)](README.pt-BR.md)

`launcher/` provides the standalone unified multi-engine launcher and engine-library registry (`kotonoha_unified_launcher` / `KotonohaLauncher`), kept outside `games/` as a top-level host component. The folder is split by ownership: `core/` + `bridge/` + `include/` are the reusable `KotonohaLauncherCore`; `app/` is what produces the launcher executable.

## Layout

| Path | Owns | Builds |
|---|---|---|
| `core/` | `KotonohaLauncherCore` logic: engine detection, save profiles, parameter persistence, route validation and the C ABI implementation | shared library / Apple framework / WASM static archive |
| `bridge/` | C ABI consumers only: `dotnet/`, `js/`, `java/`, `swift/` | linked by the matching frontend; `java/JavaBridge.cpp` is also compiled into the core on native platforms |
| `include/` | Public C ABI headers (`KotonohaLauncher/`) | both targets |
| `app/` | `KotonohaLauncher` executable: SDL entrypoint `main.cpp`, native Swift/ObjC hosts in `ios/` and `macos/`, bundle resources in `Resources/` | desktop/mobile launcher app |
| `tests/` | Source-contract tests | `KotonohaUnifiedLauncherTests` |

The Xcode navigator mirrors this split with a `KotonohaLauncherCore` group (`core/`, `bridge/`, `include/`) and a `KotonohaLauncher` group (`app/`, `Resources/`, `ios/`, `macos/`). Android activities remain in the Gradle app module at `buildsystems/android/app/src/main/java/`.

## Key Capabilities

1. **Kotonoha Multi-Engine Detection (`detectKotonohaEngine`)**:
   - Inspects directories, `.iso` images, and PS2 Mode 1 / Mode 2 (`.bin`) disc images through `Kotonoha::Filesystem` VFS V2.
   - Identifies all Kotonoha port engine families (LucaSystem, RealLive / rlBabel, KiriKiri2/Z, Ren'Py + VNX patches, N2System, PONScripter/ONScripter, Amagami PS2 SCF, School Days HQ GPK, Pure Mail ADV, Snow Radish ADV, Summer Radish / MEO CMP, Zero no Tsukaima PS2 Squirrel/KAG).
   - Automatically pairs companion translation overlays (such as `DDLC_PT-BR_Patch` -> `DDLC`).

2. **Linked game runtimes (`EngineLibraryRegistry`)**:
   - With `KOTONOHA_BUILD_ALL=ON`, CMake links the **12 real game** runtime libraries (excluding Example) directly into `kotonoha_unified_launcher`. `GameFactories.cpp` registers the concrete `Runtime` and asset-provider constructors at link time.
   - `KotonohaLauncher` (`app/main.cpp`) instantiates `Runtime` + `Engine` **in the same process** and returns to the hub after the game exits, restoring its per-game save environment. No game `main.cpp` or second executable is invoked.
   - The Example demo is never registered or linked into the hub; it remains a separate game target. The hub is not generated for individual-port builds.

3. **Per-Game Save & Config Folder Isolation (`splitSavesByGame`)**:
   - Isolates `KOTONOHA_USER_DATA_DIR` and `settings.json` under `<SaveRoot>/Kotonoha/Profiles/<saveProfileId>/save` (omitting package names in unified launcher mode and on mobile/WASM) so multiple titles sharing the same engine (e.g., `AIR_PT-BR`, `AIR-SE.ISO`, `Clannad_PT-BR`, `KANON-SE.ISO`, `PLANETARIAN.ISO` on RealLive; `SummerRadishVacation`, `SummerRadishVacation2`, `MEO` on CMP; `ZeroNoTsukaima0..2` on Zero) never collide.

## Build with game libraries

```sh
# All desktop game runtime libraries linked directly into the hub
cmake -S . -B build -DKOTONOHA_BUILD_ALL=ON
cmake --build build --target KotonohaLauncher

# Individual port builds do NOT create KotonohaLauncher.
# Example remains an independent application, never linked into the hub.
```

All source dependencies (including optional Python development packages for DDLC) must be installed for a complete build. The linked hub is built only with `KOTONOHA_BUILD_ALL=ON` on native desktop platforms. It does not include Example. macOS bundles include the DDLC Python standard library because the hub links that runtime.

## Faster game-library scanning

The library scanner uses a **detection-only, lazy directory mount**: it checks
the small set of paths needed to identify a game without indexing every nested
asset or texture. Normal gameplay still uses the full read-only VFS index.
Discovery results (including unknown folders) are cached with bounded
root/metadata fingerprints and a periodic hourly refresh; the cache is saved
once per library scan, not once per title. Adding or removing game files in a
top-level folder invalidates the cache through its modification timestamp.

The launcher intentionally scans the selected folder and its immediate
subfolders/disc images, not the entire filesystem. Point it at the folder
containing game directories, or select an individual game folder.

## CLI Usage

```bash
# Scan an entire multi-game library folder and print the engine & save-profile table
KotonohaLauncher --scan-library "/path/to/game-library"

# Detect a single game folder or disc image
KotonohaLauncher --detect "/path/to/game-library/SayaNoUta_PT-BR"

# Open the interactive ImGui multi-engine launcher preloaded with a library folder
KotonohaLauncher --library "/path/to/game-library"
```

The standalone hub app is built on desktop (Linux, macOS, Windows) and mobile (Android, iOS), not WebAssembly. The reusable core itself is available on every platform. Source detection and synthetic tests do not certify complete original-game compatibility.

## `KotonohaLauncherCore` and `bridge/`

`KotonohaLauncherCore` is the reusable launcher library holding all business logic: engine detection, save-profile isolation, parameter persistence, route validation and the shared C ABI in `include/KotonohaLauncher/LauncherBridge.h`.

- **CMake / desktop**: built as a **SHARED** library (`libKotonohaLauncherCore.dylib` / `.so` / `KotonohaLauncherCore.dll`); `KotonohaLauncher` links it. Emscripten is the only exception — wasm32 has no `dlopen` shared-library model, so it is built as a static archive and linked into the game page.
- **Xcode (macOS/iOS)**: the target is a real **framework** (`KotonohaLauncherCore.framework`, `com.apple.product-type.framework`) with `DEFINES_MODULE = YES` and the umbrella header `bridge/swift/KotonohaLauncherCore.h`. Both Apple launcher apps link and embed it with `CodeSignOnCopy`; Swift clients can `import KotonohaLauncherCore`.
- **Android**: built as `libKotonohaLauncherCore.so`; the JNI exports live in `bridge/java/JavaBridge.cpp` and are loaded with `System.loadLibrary("KotonohaLauncherCore")`. Desktop builds export the same JNI symbols from the same source file (see below).
- The legacy CMake name `kotonoha_unified_launcher` remains as an alias.

All exported C ABI entry points use `KOTONOHA_LAUNCHER_EXPORT` so the shared library publishes them with default visibility (or `__declspec(dllexport)` on Windows).

### JNI bindings (`KOTONOHA_JNI_BINDINGS`)

`bridge/java/JavaBridge.cpp` is compiled into `KotonohaLauncherCore` on every native platform (macOS, Linux, Windows and Android). On desktop, headers come from `find_package(JNI)` (with `JAVA_HOME` as a fallback for JBR/JDK installs); Android gets `jni.h` from the NDK. The option `KOTONOHA_JNI_BINDINGS` defaults to `ON` when JNI is found and `OFF` otherwise, so a build without a JDK still succeeds. Use `-DKOTONOHA_ENABLE_JNI=ON` (alias of `KOTONOHA_JNI_BINDINGS`) to force the bindings; configuration then fails with a clear message if headers are missing.

The core exports all 15 `Java_me_hirameki_*` symbols (7 for `me.hirameki.kotonoha.android.LauncherBridge` and 8 for `me.hirameki.kotonoha.launcher.KotonohaLauncherBridge`), so a Java host needs no separate JNI shim:

```sh
nm -gU libKotonohaLauncherCore.dylib | grep Java_me_hirameki
```

### Bridges

| Directory | Consumer | Entry point |
|---|---|---|
| `bridge/dotnet/` | .NET / P/Invoke | `KotonohaLauncher.cs`, `DllImport("KotonohaLauncherCore")`, `KotonohaGameLaunchRequest`, UTF-8 marshalling, `FreeString` |
| `bridge/js/` | Emscripten / browser | `JsBridge.cpp` (`EMSCRIPTEN_KEEPALIVE`) + ES module `bridge.js` (`createLauncherBridge`) using `Module.ccall` / `UTF8ToString` |
| `bridge/java/` | Java / JNI (all native platforms) | `JavaBridge.cpp` + `KotonohaLauncherBridge.java` (`org.kotonoha.launcher`-style native methods, now `me.hirameki.kotonoha.launcher`) |
| `bridge/swift/` | Swift / Xcode | `KotonohaLauncherCore.swift` facade (`LaunchRequest`, `scanRoots`, async variant), `KotonohaLauncherCore.h` umbrella header, `module.modulemap` |

Each bridge calls the same C ABI; none duplicates launcher logic. Heap strings returned by C must be released via `KotonohaLauncherFreeString` (the facades already do this).

**JNI namespace note:** the shipped app uses `me.hirameki.kotonoha.android` and its JNI symbols are `Java_me_hirameki_kotonoha_android_LauncherBridge_*`. The reusable bridge exports `Java_me_hirameki_kotonoha_launcher_KotonohaLauncherBridge_*`. Both sets are exported by the core on desktop as well; there are no `org.kotonoha.android` symbols left.

The Avalonia desktop manager lives in
[`desktop/KotonohaManager/`](desktop/KotonohaManager/README.md) and uses the
same bridge and design tokens.

## Unified design system and UI feature parity

`design/README.md` and `design/tokens.json` are the source of truth. All four
native surfaces use the same **Soft** derivation (slate `#1B2740` background,
raised `#24304B`, lilac `#A79BD4` accent, muted `#A6B2C8` text, 8/12/20 dp
radii and 4/8/16/24 dp spacing). ImGui defines the values in
`lib/src/debug/ImGuiTheme.cpp`; macOS/iOS SwiftUI repeat them in `KotonohaBrand`
/ `KotonohaMetrics`; Android Material 3 uses `res/values/colors.xml`,
`themes.xml` and `styles.xml`. The official v2 palette remains intact for brand
assets; only UI surfaces are softened.

| Capability (C ABI / native equivalent) | ImGui (desktop) | SwiftUI macOS | SwiftUI iOS | Android Material 3 |
| --- | --- | --- | --- | --- |
| Add folder/library and disc image | Library / Folder… + Disc Image… | Add Folder/Library + Add ISO/BIN (NSOpenPanel) | Add Folder/Library + Add ISO/BIN (Files.app) + ZIP | Add Folder/Library (SAF tree) + Add ISO/BIN + ZIP |
| `KotonohaLauncherScanRootsJson` | Library path + Rescan | `addRoots` + rescan | bookmark roots + rescan | SAF roots + rescan |
| `KotonohaLauncherDetectPathJson` | Detected-engine table / `--detect` | Detect Folder… / Detect Disc/File… | Detect Folder / Disc… | Detect folder / disc |
| `KotonohaLauncherLoadGameParamsJson` | saved parameter map | `params` from catalog JSON | `params` from catalog JSON | `params` from catalog JSON |
| `KotonohaLauncherSaveGameParams` | Parameters section, saved on Play | Parameters sheet → Save | Parameters sheet → Save | Parameters sheet → Save |
| `KotonohaLauncherUpscalePresetsJson` | Upscaling → Shader (OpenGL) | Shader Preset picker | Shader Preset picker | Shader Preset dropdown |
| `KotonohaLauncherValidateRoute` | route status below `--ktrf` | route validation message | route validation message | route status below `--ktrf` |
| `KotonohaLauncherDefaultSchoolDaysRoute` | shown below route input | shown in Parameters sheet | used as route placeholder | shown in Parameters sheet |
| `KotonohaLauncherResolveSaveDirectory` | Isolated Save Directory line | resolved directory + custom save root | resolved directory + custom save root | custom save root + save directory in Parameters |
| `KotonohaLauncherFreeString` | internal in C ABI wrappers | internal in store facades | internal in store facades | internal in JNI facades |
| Graphics, upscaling and per-game overrides | tabs + optional parameters | Parameters/Settings sheets | Parameters/Settings sheets | graphics + parameters sheets |

`FreeString` has no direct UI control by design; it is the memory-management
counterpart of the JSON-returning entry points and is called by every facade.
