# Kotonoha Unified Multi-Engine Hub (`KotonohaLauncher`)

[Português (Brasil)](README.pt-BR.md)

`launcher/` provides the standalone unified multi-engine launcher and engine-library registry (`kotonoha_unified_launcher` / `KotonohaLauncher`), kept outside `games/` as a top-level host component.

## Key Capabilities

1. **Kotonoha Multi-Engine Detection (`detectKotonohaEngine`)**:
   - Inspects directories, `.iso` images, and PS2 Mode 1 / Mode 2 (`.bin`) disc images through `Kotonoha::Filesystem` VFS V2.
   - Identifies all Kotonoha port engine families (LucaSystem, RealLive / rlBabel, KiriKiri2/Z, Ren'Py + VNX patches, N2System, PONScripter/ONScripter, Amagami PS2 SCF, School Days HQ GPK, Pure Mail ADV, Snow Radish ADV, Summer Radish / MEO CMP, Zero no Tsukaima PS2 Squirrel/KAG).
   - Automatically pairs companion translation overlays (such as `DDLC_PT-BR_Patch` -> `DDLC`).

2. **Linked game runtimes (`EngineLibraryRegistry`)**:
   - With `KOTONOHA_BUILD_ALL=ON`, CMake links the **12 real game** runtime libraries (excluding Example) directly into `kotonoha_unified_launcher`. `GameFactories.cpp` registers the concrete `Runtime` and asset-provider constructors at link time.
   - `KotonohaLauncher` instantiates `Runtime` + `Engine` **in the same process** and returns to the hub after the game exits, restoring its per-game save environment. No game `main.cpp` or second executable is invoked.
   - The Example demo is never registered or linked into the hub; it remains a separate game target. The hub is not generated for individual-port builds.

3. **Per-Game Save & Config Folder Isolation (`splitSavesByGame`)**:
   - Isolates `KOTONOHA_USER_DATA_DIR` and `settings.json` under `<SaveRoot>/Kotonoha-Profiles/<saveProfileId>/save` (omitting package names in unified launcher mode and on mobile/WASM) so multiple titles sharing the same engine (e.g., `AIR_PT-BR`, `AIR-SE.ISO`, `Clannad_PT-BR`, `KANON-SE.ISO`, `PLANETARIAN.ISO` on RealLive; `SummerRadishVacation`, `SummerRadishVacation2`, `MEO` on CMP; `ZeroNoTsukaima0..2` on Zero) never collide.

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

The standalone hub is built on desktop (Linux, macOS, Windows) and mobile (Android, iOS), but not WebAssembly. Source detection and synthetic tests do not certify complete original-game compatibility.
