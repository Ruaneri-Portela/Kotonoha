# Kotonoha Unified Multi-Engine Hub (`KotonohaLauncher`)

[Português (Brasil)](README.pt-BR.md)

`launcher/` provides the standalone unified multi-engine launcher and engine-library registry (`kotonoha_unified_launcher` / `KotonohaLauncher`), kept outside `games/` as a top-level host component.

## Key Capabilities

1. **Kotonoha Multi-Engine Detection (`detectKotonohaEngine`)**:
   - Inspects directories, `.iso` images, and PS2 Mode 1 / Mode 2 (`.bin`) disc images through `Kotonoha::Filesystem` VFS V2.
   - Identifies all Kotonoha port engine families (LucaSystem, RealLive / rlBabel, KiriKiri2/Z, Ren'Py + VNX patches, N2System, PONScripter/ONScripter, Amagami PS2 SCF, School Days HQ GPK, Pure Mail ADV, Snow Radish ADV, Summer Radish / MEO CMP, Zero no Tsukaima PS2 Squirrel/KAG).
   - Automatically pairs companion translation overlays (such as `DDLC_PT-BR_Patch` -> `DDLC`).

2. **Linked game runtimes (`EngineLibraryRegistry`)**:
   - CMake links every *enabled* game's runtime library directly into `kotonoha_unified_launcher`. `GameFactories.cpp` registers the concrete `Runtime` and asset-provider constructors at link time.
   - `KotonohaLauncher` instantiates `Runtime` + `Engine` **in the same process** and returns to the hub after the game exits, restoring its per-game save environment. No game `main.cpp` or second executable is invoked.
   - A detectable game with an unlinked port is shown in the catalog, but cannot be launched until that port is enabled in CMake.

3. **Per-Game Save & Config Folder Isolation (`splitSavesByGame`)**:
   - Isolates `KOTONOHA_USER_DATA_DIR` and `settings.json` under `<SaveRoot>/Kotonoha-Profiles/<saveProfileId>/Kotonoha/<packageId>/save` so multiple titles sharing the same engine (e.g., `AIR_PT-BR`, `AIR-SE.ISO`, `Clannad_PT-BR`, `KANON-SE.ISO`, `PLANETARIAN.ISO` on RealLive; `SummerRadishVacation`, `SummerRadishVacation2`, `MEO` on CMP; `ZeroNoTsukaima0..2` on Zero) never collide.

## Build with game libraries

```sh
# All desktop game runtime libraries linked directly into the hub
cmake -S . -B build -DKOTONOHA_BUILD_ALL=ON
cmake --build build --target KotonohaLauncher

# A lighter hub linked to only a selected game's runtime
cmake -S . -B build-reallive -DKOTONOHA_BUILD_REALLIVE=ON
cmake --build build-reallive --target KotonohaLauncher
```

All source dependencies (including optional Python development packages for DDLC) must be installed for a complete build. An engine may be recognized from its files even when its runtime library was not selected. The linked hub is a desktop component only.

## CLI Usage

```bash
# Scan an entire multi-game library folder and print the engine & save-profile table
KotonohaLauncher --scan-library "/path/to/game-library"

# Detect a single game folder or disc image
KotonohaLauncher --detect "/path/to/game-library/SayaNoUta_PT-BR"

# Open the interactive ImGui multi-engine launcher preloaded with a library folder
KotonohaLauncher --library "/path/to/game-library"
```

The standalone hub is built on desktop (Linux, macOS and Windows), not Android or WebAssembly. Source detection and synthetic tests do not certify complete original-game compatibility.
