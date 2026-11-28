# School Days HQ v1.02 — documentation index (Kotonoha v3)

**English (US)** | [Português (Brasil)](README.pt-BR.md)

**Targets:** Windows x64 and Android (`arm64-v8a`), sharing a single
School Days HQ v1.02 runtime implementation in `games/school-days/` while
generic engine functionality lives in `lib/` and Android platform adapters live
in `buildsystems/android/`. This index is navigational, not a claim of full
retail parity or physical QA completion. See
[docs/FORK_WORKFLOW.md](../../../docs/FORK_WORKFLOW.md) and
[Android portrait/SAF setup](android/PORTRAIT_SETUP.md) for current build and
distribution workflows.

The previously published documents retain their original paths to avoid
breaking external links. Dated investigation records are historical snapshots,
not up-to-date build instructions. Referencing a file here does not grant
permission to publish private art, retail packs, saves or translations.

## Stable project documents (unchanged filenames)

| Topic | Documents |
| --- | --- |
| Release and upstream integration | [Release readiness](RELEASE_READINESS_AUDIT.md), [Windows L4 handoff](RUANERI_KOTONOHA_V3_WINDOWS_L4_HANDOFF.md), [Ruaneri sync](RUANERI_SYNC_2026-11-04.md) |
| Retail research | [Runtime tracing](ORIGINAL_RUNTIME_TRACING.md), [Missing retail assets](MISSING_RETAIL_ASSETS.md), [Menu transition trace](MENU_TRANSITION_TRACE.md), [Route Map audit](MENU_ROUTE_MAP_AUDIT.md) |
| UI and parity | [Exit transition](EXIT_TRANSITION_PORT.md), [Menu SFX and hover](MENU_SFX_AND_ROUTE_MAP_HOVER_2026-11-05.md), [Title AC/CHIP](TITLE_AC_CHIP.md) |
| Launcher interface language (Windows/Android) | [Five interface languages, persistence, testing](LAUNCHER_INTERFACE_LANGUAGE.md) |
| QA | [Clean-profile pilot](CLEAN_PROFILE_PILOT.md) |

## Research notes by topic (historical snapshots)

### Configuration and launcher

- [Current Display Options metadata and private-art staging](configuration/display-options/README.md)
- [Display options art](configuration/DISPLAY_OPTIONS_USER_ART_2026-11-05.md)
- [Revised General/Display contract](configuration/GENERAL_DISPLAY_REVISED_USER_CONTRACT_2026-11-05.md)
- [Earlier General/Sound/Display investigation](configuration/GENERAL_SOUND_DISPLAY_SYNC_2026-11-05.md) — historical; later requirements may supersede it
- [General/Sound external art](configuration/GENERAL_SOUND_EXTERNAL_ART_2026-11-05.md)
- [Launcher JSON integration](configuration/LAUNCHER_JSON_L4_INTEGRATION_2026-11-06.md)

### Parity and retail comparison

- [Choice result meter and audio](parity/CHOICE_RESULT_METER_SFX_RETAIL_2026-11-05.md)
- [Load and GlobalFlag progress](parity/LOAD_AND_GLOBAL_PROGRESS_2026-11-05.md)
- [L×H fades](parity/LXH_FADE_TRANSITIONS_L4_2026-11-06.md)
- [Ten gameplay/UI parity items](parity/PARITY_TEN_ITEMS_2026-11-05.md)
- [Retail speed/UI/audio](parity/RETAIL_SPEED_GUI_INTERNAL_AUDIO_2026-11-05.md)

### Localization and L×H

- [Spanish strict JSON audit](localization/ES_JSON_STRICT_AUDIT_L4_2026-11-06.md)
- [L×H locale integration QA](localization/LXH_LOCALE_L4_STAGE2_STAGE3_QA_2026-11-07.md)
- [Universal English L×H assets](localization/LXH_UNIVERSAL_ENGLISH_ASSETS_L4_2026-11-07.md)

### QA and planning

- [L4 save provenance](qa/L4_SAVE_PROVENANCE_STAGE4.md)
- [L4 Stage 5 QA progress](qa/L4_STAGE5_QA_PROGRESS_2026-11-07.md) — dated snapshot, not necessarily current
- [Definitive L4/L×H/language/launcher plan](planning/PLANO_DEFINITIVO_L4_LXH_IDIOMAS_LAUNCHER_2026-11-07.md) — dated project plan

### Delivery records

- [Clean L4 local staging](delivery/L4_CLEAN_LOCAL_STAGING_2026-11-07.md)
- [Two-DLL community handoff](delivery/L4_TWO_DLL_COMMUNITY_HANDOFF_2026-11-07.md)

## Do not relocate without updating dependencies

The `src/`, `include/`, `game-assets/`, `windows/` and `tests/fixtures/` folders
have paths referenced in CMake, runtime code and test harnesses. The private
artwork lives under `game-assets/overlay/` (or the legacy source
`game-assets/display-options/`) and `game-assets/launcher-art/`. The approved
options UI can be embedded in the Windows and Android native binaries;
external overlays are optional. Private Windows and Android builds can also
embed the exact approved runtime `assets/` pack (shaders, font, splash, icon,
and launcher artwork) with `KOTONOHA_SCHOOL_DAYS_RUNTIME_ASSETS_SOURCE` and
`KOTONOHA_SCHOOL_DAYS_REQUIRE_EMBEDDED_ASSETS=ON`. The Android properties are
`schoolDaysRuntimeAssetsSource` and `schoolDaysRequireEmbeddedAssets=true`.
The runtime uses an application-private, session-scoped compatibility directory
for APIs that require filenames; it does not need `assets/` in the game folder.
`game-assets/`, forced subtitles, translations, and retail Packs stay external.
Private artwork and generated native data must not be committed. Keep the
required third-party license notices in the package.
Public Display Options metadata is in
`docs/configuration/display-options/`. Moving these directories requires
updating consumers, building and running CTest.

Commercial packs, local L×H media, real saves and private translation JSONs
must remain outside the Git history. The `build/` tree contains active L4
builds, backups and junctions to the retail install; never run a blanket
recursive cleanup there.

## Optional private Windows executable icon

The monochrome School Days HQ icon is **user-supplied, locally approved
artwork**, not a repository asset. Convert the owner's authorized artwork to
Windows ICO format and place it at
`games/school-days/windows/SchoolDays.ico` in your local checkout. Do not
commit the file or include it in public source archives. The tracked
`games/school-days/windows/SchoolDays.rc` refers to this filename, but
CMake registers that resource **only when the ICO exists**. A clean Git
worktree without the icon still configures and builds a functional Windows
executable, using the platform's ordinary fallback application icon.
