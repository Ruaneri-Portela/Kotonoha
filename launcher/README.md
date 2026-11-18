# Kotonoha Unified Multi-Engine Hub (`KotonohaLauncher`)

[Português (Brasil)](README.pt-BR.md)

`launcher/` provides the standalone unified multi-engine launcher and engine-library registry (`kotonoha_unified_launcher` / `KotonohaLauncher`), kept outside `games/` as a top-level host component.

## Key Capabilities

1. **Kotonoha Multi-Engine Detection (`detectKotonohaEngine`)**:
   - Inspects directories, `.iso` images, and PS2 Mode 1 / Mode 2 (`.bin`) disc images through `Kotonoha::Filesystem` VFS V2.
   - Identifies all Kotonoha port engine families (LucaSystem, RealLive / rlBabel, KiriKiri2/Z, Ren'Py + VNX patches, N2System, PONScripter/ONScripter, Amagami PS2 SCF, School Days HQ GPK, Pure Mail ADV, Snow Radish ADV, Summer Radish / MEO CMP, Zero no Tsukaima PS2 Squirrel/KAG).
   - Automatically pairs companion translation overlays (such as `DDLC_PT-BR_Patch` -> `DDLC`).

2. **Engines Built as Modular Libraries (`EngineLibraryRegistry`)**:
   - Maps each detected `EngineKind` to its modular CMake static/shared library target (`luca_system_formats`, `reallive_port`, `kirikiri_formats`, `doki_doki_literature_club`, `n2system_formats`, `ponscripter_port`, `amagami_port`, `school_days`, `PmailPort`, `snow_radish_vacation`, `summer_radish_vacation`, `zero_no_tsukaima`).
   - Supports in-process runtime swapping via `EngineLibraryFactory` or transparent modular process dispatch (`launchModularPortProcess`).

3. **Per-Game Save & Config Folder Isolation (`splitSavesByGame`)**:
   - Isolates `KOTONOHA_USER_DATA_DIR` and `settings.json` under `<SaveRoot>/Kotonoha-Profiles/<saveProfileId>/Kotonoha/<packageId>/save` so multiple titles sharing the same engine (e.g., `AIR_PT-BR`, `AIR-SE.ISO`, `Clannad_PT-BR`, `KANON-SE.ISO`, `PLANETARIAN.ISO` on RealLive; `SummerRadishVacation`, `SummerRadishVacation2`, `MEO` on CMP; `ZeroNoTsukaima0..2` on Zero) never collide.

## CLI Usage

```bash
# Scan an entire multi-game library folder and print the engine & save-profile table
KotonohaLauncher --scan-library "/Volumes/Nero 256GB/KotonohaAssets"

# Detect a single game folder or disc image
KotonohaLauncher --detect "/Volumes/Nero 256GB/KotonohaAssets/SayaNoUta_PT-BR"

# Open the interactive ImGui multi-engine launcher preloaded with a library folder
KotonohaLauncher --library "/Volumes/Nero 256GB/KotonohaAssets"
```
