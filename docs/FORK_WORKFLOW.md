# Kotonoha v3 Fork Workflow (`tsubaki-0x/Kotonoha`)

**English (US)** | [Português (Brasil)](FORK_WORKFLOW.pt-BR.md)

This guide documents the active architecture, branch policy, DEV/DIST build procedures, isolated testing rules, and upstream contribution workflow for the `tsubaki-0x/Kotonoha` fork targeting **School Days HQ v1.02** on **Windows x64** and **Android (`arm64-v8a`)**.

---

## 1. Shared Windows & Android Architecture

Both platforms execute one shared School Days HQ game-logic codebase without duplicating gameplay rules:

- `lib/`: generic C++17 engine, SDL3/ImGui/FFmpeg/libass integration, `Filesystem` V2 (`FileHandle`, `IMount`, `SDLFileSource`), `ForcedSubtitleRenderer`, `DebugOverlay`, and generic mobile adapters (`lib/include/Kotonoha/mobile/`).
- `modules/`: script virtual machines (`modules/vms/`) and archive providers (`modules/providers/`).
- `games/school-days/`: shared School Days HQ runtime, KTRF/ORS routing, Save/Load/GlobalFlag durability, Replay, Route Map, BackLog, retail `Config.DAT` policy, Display Options UI, and unit/runtime CTests.
- `buildsystems/android/`: Gradle configuration, JNI glue, portrait SAF folder setup, and Android `dist` buildType (`BuildConfig.IS_DISTRIBUTION_BUILD`).

---

## 2. Permanent Branches & Upstream Alignment

| Branch | Role |
|---|---|
| `dev` | Default branch of `tsubaki-0x/Kotonoha`; active development and clean upstream collaboration baseline. |
| `hybrid` | Hybrid distribution branch preserving School Days HQ DEV/DIST packaging and fork-specific Windows/Android distribution infrastructure. |
| `upstream/dev` (`Ruaneri-Portela/Kotonoha`) | Upstream reference repository. Never push directly to `upstream`. |

- Sync remotely with `git fetch origin --prune` and `git fetch upstream --prune`.
- Always create upstream PR branches directly from `upstream/dev` with narrowly scoped commits, zero School Days symbols inside `lib/`, and zero proprietary assets.
- Never force-push published branches (`dev` or `hybrid`).

---

## 3. DEV vs DIST Policy & Windows x64 Compilation

Always compile virgin Windows builds in **brand-new CMake directories** using `Visual Studio 18 2026` (`-A x64`), `Release` configuration (`--config Release --parallel 1`), SDL3/SDL3_ttf/ImGui from submodules, and auxiliary libraries from `x64-windows-static-md`.

### Mandatory CMake Flags
```cmake
-DKOTONOHA_BUILD_ALL=OFF
-DKOTONOHA_BUILD_SCHOOL_DAYS=ON
-DBUILD_TESTING=ON
-DKOTONOHA_SDL_FROM_VCPKG=OFF
-DKOTONOHA_IMGUI_FROM_VCPKG=OFF
-DKOTONOHA_DEV_CONTROL=OFF
-DKOTONOHA_ENABLE_PLAYER_DUBBING=OFF
```

- **Windows DEV**: `-DKOTONOHA_DISTRIBUTION_BUILD=OFF -DKOTONOHA_ENABLE_MCP=ON` (`WINDOWS_CUI` subsystem; `DebugOverlay` and MCP enabled).
- **Windows DIST**: `-DKOTONOHA_DISTRIBUTION_BUILD=ON -DKOTONOHA_ENABLE_MCP=OFF` (`WINDOWS_GUI` subsystem — no extra console; `DebugOverlay`, MCP, and DEV CLI flags `--mcp`, `--mcp-port`, `--mcp-root`, `--log` compiled out).

### Two-Phase Windows Distribution Layout (Private QA)
1. **Phase A (Compile)**: generate virgin DEV and DIST binaries in separate CMake directories and pass CTest.
2. **Phase B (Assemble)**: package each distribution cleanly without copying `.pdb`, `.lib`, caches, personal configs, or the 29 retail GPKs:
   - `SchoolDays.exe`, `SDL3.dll`, `SDL3_ttf.dll`, `imgui.dll`, `kotonoha.dll`
   - `translations/jsons/` (10 locale JSONs: `en`, `pt-BR`, `es`, `ru`, `zh-CN` + `*-lxh.json`) and `translations/scripts/` (9 private QA translation GPKs)
   - `overlay/System/Option/` (10 Display Options overlay PNG/CMAP files)
   - `game-assets/` (`school-days-hq.ktnroute`, `styles.ass`, and `forced_subtitles/` with 198 `.ass` files from the official **v4** pack: 33 root legacy `en` + 33 per locale across `en`, `es`, `pt-BR`, `ru`, `zh-CN`)
   - `assets/` (`SPLASH.PNG`, `SchoolDays.ico`, `SchoolDaysHQ-Launcher.png`, `Anime4K/`, `ArtCNN/`, `fonts/NotoSansJP-Regular.ttf`)
   - `third-party-licenses/` (16 license notices)

**Required `forced_subtitles` v4 staging & verification (Windows DEV/DIST and Android SAF, v1.00+):**
All official School Days HQ v1.00+ distributions (Windows DEV/DIST and Android SAF external folders) must use the owner-approved **`forced_subtitles` v4** pack (`198` `.ass` files: `33` per locale across `en`, `es`, `pt-BR`, `ru`, `zh-CN` — `17` Sekai, `14` Kotonoha, `2` Setsuna — plus `33` legacy root `en` files; canonical tree SHA-256 `064172a520b7aae1eabac0a2a078e1b866c821a123c7fe046ac8d7423462a818`, preserving Setsuna's approved `\pos(400,325)` and `\pos(400,420)` placements). Never fall back to v1/v2/v3 or mix obsolete files, and never hardcode `Downloads` in engine code or commit `.ass` files into public Git. Forced subtitles remain optional for running base School Days HQ at runtime; validation applies exclusively to official distribution staging:

```powershell
# Stage v4 cleanly from a configured local source (or KOTONOHA_FORCED_SUBTITLES_V4_DIR)
python games/school-days/tools/stage_forced_subtitles_v4.py stage --source "<approved-v4-dir>" --dest "<dist-folder>/game-assets/forced_subtitles"

# Verify an assembled Windows distribution or Android SAF folder
python games/school-days/tools/stage_forced_subtitles_v4.py verify "<dist-folder>/game-assets/forced_subtitles"
```

Run `python -m unittest discover -s games/school-days/tools -p test_stage_forced_subtitles_v4.py -v` to exercise rejection of missing, obsolete, or pre-v4 files.

**Required Windows QA font validation (DEV and DIST):** The launcher font is
private, untracked, and must never be copied blindly from a checkout's
`assets/fonts/` directory. The 1,204-byte placeholder caused invisible
launcher text; the known-good font is 5,733,060 bytes (SHA-256
`7c8597677e9fac0f54d7848ad18bc6a708dfb5baa4ebf4bd91e66efcca313bf3`).
Set `-DKOTONOHA_SCHOOL_DAYS_LAUNCHER_FONT_SOURCE=<approved-local-ttf>`
when configuring either Windows build. The School Days post-build stage then
validates and copies the approved font **after** generic engine assets.
Before packaging, run `cmake --build <build-dir> --config Release --target
SchoolDaysVerifyLauncherFont`. After assembling each DEV/DIST QA folder,
verify its **final** copy (after all asset staging, before creating a ZIP):

```powershell
cmake "-DKOTONOHA_FONT_ACTION=verify" "-DKOTONOHA_FONT_DESTINATION=<qa-folder>/assets/fonts/NotoSansJP-Regular.ttf" -P games/school-days/windows/StageLauncherFont.cmake
```

To repair a staging folder from an explicitly approved local source, run the
same script with `-DKOTONOHA_FONT_ACTION=stage`,
`-DKOTONOHA_FONT_SOURCE=<approved-local-ttf>` and the destination above.
Both modes fail on missing, wrong-size or wrong-hash fonts; neither supplies
font bytes to Git. Never package after a failed verification. Run
`ctest -R SchoolDays.LauncherFontStage` to exercise placeholder rejection.

Legacy L4 two-DLL staging scripts (`games/school-days/tools/stage_clean_local_l4.ps1`, `publish_two_dll_l4.ps1`, `stage_windows_clean_local.py`) are historical and must not be run blindly over personal installs.

---

## 4. Android DEV/DIST APKs & SAF

- Android builds live under `buildsystems/android/` and produce isolated DEV (`me.hirameki.kotonoha.schooldays`) and DIST (`me.hirameki.kotonoha.schooldays.dist`) APKs via Gradle (`assembleSchoolDaysDebug` / `assembleSchoolDaysDist`).
- Portrait launcher setup lets the player select their external `SchoolDaysHQ` folder via Storage Access Framework (SAF); the 29 original retail GPKs run standalone and optional `LxH.GPK` is controlled by `EnableLxh=0|1` in `Kotonoha.ini` (and the Windows/Android launcher checkbox/switch, plus `--lxh` / `--no-lxh` CLI flags). See [OPTIONAL_LXH_AND_PERFORMANCE.md](../games/school-days/docs/OPTIONAL_LXH_AND_PERFORMANCE.md).
- Gameplay runs in landscape using the shared `games/school-days/` runtime, with engine `SPLASH.PNG`, `SchoolDays.ico` launcher icons, face-cropped banner art, and persistent `Display Options` tab labeling.
- Validate APKs statically with `python3 tools/android/check_school_days_apk.py`, and always perform a separate physical hardware pass on the reference device (**POCO X7 Pro**) before declaring hardware readiness.

---

## 5. Isolated Testing & Commercial Asset Policy

- Run headless engine/game regression checks with `SDL_VIDEO_DRIVER=dummy` and `SDL_AUDIO_DRIVER=dummy`:
  - `python3 tools/ValidateBuildMetadata.py`
  - `python3 tools/check_game_platform_isolation.py`
  - `python3 tools/check_hybrid_android_contracts.py` (on the hybrid branch)
  - `git diff --check`
  - Windows DEV baseline: `65/65` targeted CTests (`97/98` full suite; historical `#1 Kotonoha.Infrastructure` fails only on the Apple MetalFX software-renderer stub on Windows).
  - Windows DIST baseline: `6/6` targeted DIST CTests and `91/91` full DIST CTests.
- For interactive/real-data QA, always pass isolated `--documents <temp-dir>` and `--config <temp-file>` so real saves (`Save/`, `GlobalFlag.DAT`) and `Config.DAT` are never modified.
- **Commercial & Private Asset Boundary**: never commit or publicly redistribute the 29 retail GPKs, `LxH.GPK`, private translation GPKs/JSONs, personal saves, or unlicensed artwork. Keep private QA ZIPs strictly local until a dedicated licensing audit is completed.

---

## 6. Known Limitations

- Upstream `Ruaneri-Portela/Kotonoha:dev` uses a unified multi-game Android launcher (`LauncherActivity.java` + `SafBridge.java`) and dual-ABI (`arm64-v8a` + `armeabi-v7a` NEON); upstream Android contributions must integrate cleanly with that architecture rather than replacing it.
- Physical hardware QA on POCO X7 Pro and real-GPU visual QA on Windows must always be reported separately from CTest/Gradle/static validation.
