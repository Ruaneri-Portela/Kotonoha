# School Days HQ on Android — Portrait Launcher & Full Gameplay Port

The School Days HQ Android port shares the same C++17 game logic, asset mount, translation gate, retail `Config.DAT` persistence and L×H integration as the Windows L4 build. The launcher opens in **portrait**; gameplay launches in **fullscreen landscape** (`sensorLandscape`) with touch controls.

## Phone Folder Layout (`SchoolDaysHQ/`)

Place your legally obtained School Days HQ files in internal storage (for example `/storage/emulated/0/SchoolDaysHQ/` or `/storage/emulated/0/Download/SchoolDaysHQ/`):

```text
SchoolDaysHQ/
├── Packs/
│   ├── Script.GPK
│   ├── System.GPK
│   ├── ... all 29 original HQ GPKs ...
│   └── LxH.GPK
├── game-assets/
│   ├── school-days-hq.ktnroute
│   ├── styles.ass                  (optional ASS style sheet)
│   ├── launcher-art/
│   │   └── SchoolDaysHQ-Launcher.png  (optional launcher artwork)
│   └── forced_subtitles/           (optional, <lang>/<node>.ass)
│       ├── en/
│       ├── pt-BR/
│       ├── es/
│       ├── ru/
│       └── zh-CN/
├── overlay/
│   └── System/Option/              (optional Display Options & general overrides)
└── translations/
    ├── scripts/                    (native GPK packs: Script - *.GPK + *-lxh.GPK)
    └── jsons/                      (JSON pairs: <locale>.json + <locale>-lxh.json + reviewed-identities)
```

> **Note:** Always select the parent `SchoolDaysHQ/` folder in the Android folder picker — **not** `Packs/` directly. Android SAF only grants read access to the chosen folder and its subdirectories, so selecting `Packs/` directly cannot access `game-assets/`, `overlay/` or `translations/`.

## Quick User Guide

1. Install the ARM64 APK on your Android device (Android 11 / API 30+).
2. Open **School Days HQ** (portrait launcher).
3. Tap **Select SchoolDaysHQ folder** and choose your `SchoolDaysHQ` folder.
4. Open **Options**:
   - **Display**: Native resolution / refresh rate, VSync, Narrative aspect (`Auto` / `4:3` / `16:9`), Android frame limit (`Unlimited` / `30` / `60` FPS).
   - **Graphics**: Render scale (`0.25x`–`1.00x`), OpenGL ES indicator, Upscaler (`Auto` / `None` / `Linear` / `MPV`), Upscale factor (`1.00x`–`4.00x`), Shader preset (Anime4K / ArtCNN Low/Medium/High tiers).
   - **Audio**: Retail `BGM`, `SE`, `Voice` (0–10), Male voices, Mute game audio, Engine Master & Movie volumes, and **Launcher title music** (plays `TitleBGM` from `Ini.GPK`/`BGM.GPK` inside the launcher, isolated from game mute).
   - **General**: Show text, Skip unread scenes, SuperSkip / next choice, Auto draw, Auto mode, Swap controller A/B, Enable forced subtitles (`.ass`).
   - **Language**: Choose either **Native GPK packs** (`translations/scripts/`) or **JSON dictionaries** (`translations/jsons/`) for `en`, `pt-BR`, `es`, `ru`, `zh-CN`. Both HQ and L×H files are required; silent English fallback is blocked.
   - **Game Files**: Inspect pack count (`30/30`), KTRF route, `styles.ass`, `forced_subtitles/`, `overlay/`, and detected translations; change or disconnect the folder.
   - Tap **Apply** to save (`settings.json`, `save/Config.DAT`, `save/Kotonoha.ini`), **Cancel** to discard, or **Reset defaults**.
5. Tap **Play** to launch in fullscreen landscape.
6. In-game controls: tap to advance/select choices, tap the bottom-left **MENU** button to open the retail MenuBar (Save, Load, BackLog, Route Map, Config, Auto, Skip, Title), and use Android **Back** to close modal menus.

## Engine and Storage Boundaries

- **Read-only SAF mount (`DocumentMount`)**: All 30 archives (`Packs/*.GPK` + `LxH.GPK`) stay in place on user storage and are read via SAF URIs with a thread-safe cached stream per archive (`SchoolDaysGpkArchive`) for fast index/entry reads without copying multi-gigabyte packs.
- **Per-session cache staging (`SchoolDaysFolder`)**: Only small files that require physical paths (`school-days-hq.ktnroute`, active translation GPKs or JSONs + reviewed-identities sidecars, `game-assets/styles.ass`, active-locale `forced_subtitles/`, and `overlay/` files) are staged into app cache on Play and cleaned up on exit.
- **Shared configuration & saves**: `settings.json` lives in `Android/data/<package>/files/settings.json`; `Config.DAT`, `Kotonoha.ini`, `GlobalFlag.DAT` and `Save/` live in `Android/data/<package>/files/save/` — never inside SAF or user game folders.
- **Forced subtitles**: Prefer `game-assets/forced_subtitles/<lang>/<node>.ass` (passed via `--forced-subtitles` on SAF launches), retaining legacy `forced_subtitles/` compatibility.
- **ImGui font safety**: `ImGuiTheme::loadFonts` validates SFNT/TTC bounds and active ImGui font-loader compatibility, falling back explicitly to `AddFontDefaultVector()` when system CJK collections (such as `/system/fonts/NotoSansCJK-Regular.ttc`) are incompatible with `stb_truetype` or high-DPI scaling changes across frames.
- **APK packaging**: Retail packs, KTRF routes, translations and commercial artwork are never bundled in the APK (`-PbundleGameAssets=false`). All ARM64 `.so` libraries use 16 KiB ELF `LOAD` alignment (`-Wl,-z,max-page-size=16384`).

## Current Verification Status (POCO X7 Pro, Android 16 / API 36)

> **Note:** This matrix explicitly separates what is **implemented**, **compiled**, **partially working**, **validated on real hardware**, and **pending**. Only items directly evidenced on the physical POCO X7 Pro (`QO5XJZFYDMRWDU6T`) are marked as validated on real hardware.

| Area | Current Status | Evidence / Notes |
|---|---|---|
| Portrait launcher + SAF (`30/30` GPK, KTRF, `styles.ass`, `overlay/`) | Validated on real hardware | Opens in portrait on POCO X7 Pro and reads `SchoolDaysHQ/` via read-only SAF. |
| **P0 — First subtitle, gameplay progression & BackLog** | **Validated on real hardware (fixed)** | Root cause confirmed via LLDB (`SDL_iconv_string` → Bionic `iconv` `E2BIG` infinite loop). Replaced with pure C++17 `Kotonoha::Utf8ToUtf16`/`Utf16ToUtf8`. Verified 3× consecutively in Episode 1 past multiple subtitles, train→classroom transition, top MenuBar, `MENU`, and `BACKLOG` with flat ~645–664 MB RSS. |
| **P1 — Custom Display Options overlay (`Option_Display*.png/.cmap`)** | **Validated on real hardware (fixed)** | Removed `#if defined(__ANDROID__)` exclusions in `SchoolDaysRuntime.hpp/.cpp`; eliminated artificial white tab overlay on General/Sound; native `Option_Display` renders real device modes (`2712x1220 @ 30/60/90/120 Hz`), VSync, Upscaler, Shader preset, Custom GLSL, Upscale factor (`1x`–`4x`), `DEFAULT` and `APPLY` with working touch. |
| **P1 — Audio volume (double attenuation)** | **Fixed & unit-tested (pending user ear check)** | `SchoolDaysRuntime::applyConfig()` resets `userVolume = 1.0f` on retail `BGM`/`SE`/`Voice` banks (and re-applies after `applySettings`), removing `0.2985 × 0.2985 = 0.089` (-21 dB) double attenuation and the 29.8% ceiling at level 10/10. Launcher `TitleBGM` gain raised `0.35f` → `0.60f`. Unit assertion added in `SchoolDaysRuntimeConfigStorageTest`. |
| **P1 — Bidirectional Launcher ↔ Gameplay persistence** | **Validated on real hardware (fixed)** | Android now flushes `Config.DAT` immediately on changes and on `SDL_EVENT_WILL_ENTER_BACKGROUND`; `SchoolDaysSetup.onResume()` reloads and re-syncs UI; display modes are landscape-normalized so `2712x1220 @ 90 Hz` matches both ways. Verified round-trip across `force-stop` (`BGM 7/10`, `2.00x`, `90 Hz`, `4:3`, `Reset defaults`). |
| **P1 — Russian & Chinese GPK languages** | **Diagnosed & UX validated on real hardware** | Confirmed device `/storage/emulated/0/Download/SchoolDaysHQ/translations/scripts/` contains only `Script - Brazilian.GPK`, `Script - Spanish.GPK`, `en-lxh.GPK`, `es-lxh.GPK`, `pt-BR-lxh.GPK` (`ru` and `zh-CN` exist only in `translations/jsons/`). Language tab now displays exact missing GPK filenames per disabled language and notes JSON availability below. |
| **P1 — Resolution, aspect (`4:3`/`16:9`) & touch (2712×1220)** | **Partially validated on real hardware** | Landscape 2712×1220, launcher `16:9 ↔ 4:3` switching (`Kotonoha.ini`), and touch alignment across Title, `OPTION`, `Option_Display`, popups, `Sound Options`, top MenuBar, `MENU`, and `BACKLOG` verified on POCO. |
| **P2 — Extended gameplay (choices, Save/Load, Route Map, Replay, CJK/Cyrillic, 7 L×H endings)** | **Pending extended real-hardware QA** | Unblocked after P0 fix (`Utf8ToUtf16`/`Utf16ToUtf8` also protects `SETSELECT` choices and `BACKLOG`); extended interactive QA across choices, Save/Load, Route Map, Replay, forced subtitles, CJK/Cyrillic rendering, and full 7 L×H ending flow remains pending. |
| Windows DEV Release / DIST Release + CTests | Compiled & tested | `SchoolDays` and 57/57 `SchoolDays*` tests + `Kotonoha.EncodingUtf` + `Kotonoha.AudioChannels` passed in Windows DEV Release; Windows DIST Release verified alongside. |

## Regressions and Functional Issues Observed on POCO X7 Pro — 2026-11-21

Ongoing tracking of real-hardware defects observed on POCO X7 Pro (Android 16 / API 36, DEV APK `me.hirameki.kotonoha.schooldays`) starting from HEAD `0e7dc7cf`:

### 1. [P0] Complete freeze when the first subtitle (`......`) appears
- **Symptom**: Starting a new game (Episode 1, `pt-BR` GPK mode), the train opening video played until ~32s and then froze completely on the very first subtitle (`......`), with `SDLThread` pegged at 100% CPU and RSS climbing past `3.8 GiB` until LMKD killed the app.
- **Confirmed root cause (via LLDB + `llvm-addr2line` attached to frozen `SDLThread`)**:
  ```text
  SDL_iconv (SDL_iconv.c:57) ← SDL_iconv_string (SDL_iconv.c:822)
    ← SchoolDays::Utf16 (SchoolDaysRuntime.cpp:645)
    ← SchoolDays::Runtime::dispatch PRINT_TEXT (SchoolDaysRuntime.cpp:2998)
    ← SchoolDays::Runtime::update (SchoolDaysRuntime.cpp:3708)
  ```
  On Android API 28+, SDL3 builds with `HAVE_ICONV=1` using Bionic's `iconv()`. Converting `"......"` + NUL from `UTF-8` to `UTF-16LE` via `SDL_iconv_string` caused Bionic `iconv()` to repeatedly return `E2BIG`, triggering an unbounded `stringsize *= 2` / `SDL_realloc` loop in `SDL_iconv.c:824-839` until OOM. The same `SDL_iconv_string` pattern also existed in `SchoolDaysScenes.cpp` (choices) and `SchoolDaysBackLogPresenter.cpp` (BackLog).
- **Files changed**: `lib/src/localization/Encoding.cpp`, `games/school-days/src/SchoolDaysRuntime.cpp`, `games/school-days/src/SchoolDaysScenes.cpp`, `games/school-days/src/SchoolDaysBackLogPresenter.cpp`, `lib/tests/EncodingUtfTests.cpp`, `lib/tests/CMakeLists.txt`.
- **Verification**: `Kotonoha.EncodingUtf` passed; 3 consecutive live POCO X7 Pro runs progressed smoothly past Episode 1 subtitles into the classroom scene, top MenuBar, `MENU`, and `BACKLOG` at steady ~645–664 MB RSS.
- **Status**: **Fixed & validated on real hardware**.

### 2. [P1] Russian & Chinese unavailable in GPK mode
- **Device inspection**: `/storage/emulated/0/Download/SchoolDaysHQ/translations/jsons/` has all 5 pairs (`en`, `pt-BR`, `es`, `ru`, `zh-CN`), while `translations/scripts/` contains only `Script - Brazilian.GPK`, `Script - Spanish.GPK`, `en-lxh.GPK`, `es-lxh.GPK`, and `pt-BR-lxh.GPK`. Neither `Script - Russian.GPK`/`ru-lxh.GPK` nor `Script - chinese-compatible.GPK`/`zh-CN-lxh.GPK` is present on the phone (matching Windows `SchoolDaysGpkLanguageProfile.hpp` and `SchoolDaysLauncher.cpp`).
- **Fix**: `SchoolDaysFolder.java` reports `missingGpkFiles(locale)` and `hasLegacyIncompatibleChineseGpk()`; `SchoolDaysSetup.java` + `strings.xml` (EN/pt-BR) render exact missing filenames under disabled GPK buttons and note JSON availability below.
- **Status**: **Diagnosed & UX validated on real hardware**.

### 3. [P1] Bidirectional persistence between Launcher and Gameplay
- **Confirmed root causes**: (1) `SchoolDaysRuntime` deferred `Config.DAT` flush until `shutdown()`, losing in-game edits on Android task kill; (2) `SchoolDaysSetup.onResume()` did not call `syncUiFromDraft()`; (3) portrait `SchoolDaysSetup` matched `1220x2712` against landscape `2712x1220` saved by `GameActivity`.
- **Files changed**: `games/school-days/src/SchoolDaysRuntime.cpp`, `buildsystems/android/app/src/schoolDays/java/org/kotonoha/android/SchoolDaysSetup.java`.
- **Verification**: Live POCO round-trip across `force-stop` verified (`2712 x 1220 @ 90 Hz`, `2.00x`, `BGM 7/10`, `4:3` Standard mode, and `Reset defaults`).
- **Status**: **Fixed & validated on real hardware**.

### 4. [P1] Custom Display Options overlay unused on Android
- **Confirmed root cause**: `#if defined(__ANDROID__)` / `#if !defined(__ANDROID__)` guards in `SchoolDaysRuntime.hpp/.cpp` bypassed the shared `Option_Display` CMAP/PNG/Chip renderer and controller.
- **Files changed**: `games/school-days/src/SchoolDaysRuntime.hpp`, `games/school-days/src/SchoolDaysRuntime.cpp`.
- **Verification**: Live POCO screenshots & touch tests confirmed authentic `Option_Display` skin, `30/60/90/120 Hz` popup, `2x` chip, immediate `settings.json` persistence, and clean General/Sound tabs with zero white overlay box.
- **Status**: **Fixed & validated on real hardware**.

### 5. [P1] Abnormally low audio volume in menus and gameplay
- **Confirmed root cause**: Double gain multiplication in `AudioMixer` (`userVolume * volume = 0.2985 * 0.2985 = 0.089` / -21 dB) because `settings.json` `channelVolumes` populated `userVolume` and `SchoolDaysRuntime::applyConfig()` applied `Policy::LinearGain` again via `channel->setVolume()`, also capping 10/10 at 29.8%.
- **Files changed**: `games/school-days/src/SchoolDaysRuntime.cpp`, `games/school-days/android/java/org/kotonoha/android/SchoolDaysTitleBgmPlayer.java`, `games/school-days/tests/SchoolDaysRuntimeConfigStorageTest.cpp`.
- **Verification**: Automated assertions in `SchoolDaysRuntimeConfigStorageTest` + `Kotonoha.AudioChannels` + `SchoolDaysLauncherTest` passed; live physical speaker loudness check recommended by user.
- **Status**: **Root cause fixed & unit-tested; final physical loudness check pending user confirmation**.

### 6. [P1] Screen adaptation (2712×1220), aspect ratio & touch
- **Status**: Landscape 2712×1220, `16:9 ↔ 4:3` switching, and touch hitboxes across Title, `OPTION` (3 tabs + popups), MenuBar, `MENU`, and `BACKLOG` verified on POCO X7 Pro.

---

## White Fade & Viewport (Milestone 2)

### P0 defect found
- **Symptom**: While opening/closing gameplay system panels (`SAVE`, `LOAD`, `BACKLOG`, `OPTION`), the white fade (`SchoolDaysSystemPanelTransition`) filled the entire physical surface (`2712×1220`), bleeding white onto the black pillarbox/letterbox margins outside the game viewport.
- **Confirmed root cause**: `Runtime::renderSystemPanelTransition()` (`games/school-days/src/SchoolDaysRuntime.cpp`) runs in `RenderSpace::Screen` and called `SDL_RenderFillRect(&renderer, nullptr)` for the white overlay (`255, 255, 255, WhiteAlpha`), unlike `renderExitTransition()` and `renderFrontendTransition()`, which already computed the fitted viewport rectangle.
- **Shared fix (Windows + Android)**:
  - Added pure helper `SchoolDaysSystemPanelTransition::ComputeFittedViewport(...)` in `games/school-days/include/Kotonoha/routing/SchoolDaysSystemPanelTransition.hpp`.
  - `Runtime::renderSystemPanelTransition()` computes the exact fitted active game/panel rectangle (`800×600` in `Standard`, `800×450`/`1280×720`/`1024×576` in `Wide*`, honoring Windows Standard FULL `narrativePresentationHeightScale()` and Config stretch; and `1280×720` fitted during `Revealing`) and restricts the white fade strictly to `area`, keeping all letterbox/pillarbox margins pure `#000000`.
  - Preserved retail timing (`750 ms` open / `500 ms` close / `500 ms` reveal) and left `renderLoadTransition`, `renderTransportFade`, `renderLxhFade`, and `replayHSceneTransitionActive` untouched.
- **Verification**:
  - New `SchoolDaysSystemPanelTransitionTest` (`games/school-days/tests/SchoolDaysSystemPanelTransitionTest.cpp`) verifies alpha curves, geometry across `2712×1220` (`16:9` & `4:3`) and desktop Standard FULL, plus SDL3 software-renderer `SDL_RenderReadPixels` pixel readback (pillarbox/letterbox stay `(0, 0, 0)` while center fades white). Passed 3/3 alongside `SchoolDaysExitTransitionTest` and `SchoolDaysFrontendTransitionTest`.
  - Real hardware captures on POCO X7 Pro (`2712×1220`):
    - **16:9** (`SAVE` open/close, `BACKLOG` open): pillarbox samples `x=20, 260, 2451, 2691` measured exact `(0, 0, 0)` while the active viewport faded to `(167, 167, 167)`, `(245, 244, 244)`, and `(180, 180, 180)`.
    - **4:3 Standard** (`SAVE` open): outer margins `x=20, 260, 500, 2211, 2451, 2691` measured exact `(0, 0, 0)` while the `4:3` rect (`x=600..2111`) faded to `(172, 172, 172)`.
- **Classification**: **Confirmed parity** (`Paridade confirmada`, Windows & Android).

---

## Real Forced Subtitles Integration (Milestone 3)

### Environment & End-to-End Audit
- **Initial device state**: `/storage/emulated/0/Download/SchoolDaysHQ/game-assets/` on POCO X7 Pro originally held only `school-days-hq.ktnroute` and `styles.ass`; no `forced_subtitles/` directory existed on the device prior to this milestone.
- **Legitimate test content (no retail GPK/save edits)**: Pushed the legitimate 3-OP × 5-locale set (`en`, `es`, `pt-BR`, `ru`, `zh-CN` + 33 legacy root EN `.ass` = 198 files from `SchoolDaysHQ-Windows-DIST-3OPs-5Locales-Fallback-QA-20261120/game-assets/forced_subtitles/`) into `/storage/emulated/0/Download/SchoolDaysHQ/game-assets/forced_subtitles/`.
- **Verified pipeline**:
  1. `SchoolDaysFolder.java`: detects `game-assets/forced_subtitles/` (`forced_subtitles/: OK` in **Files** tab), stages locale files into `<cache>/sd-saf-*/game-assets/forced_subtitles/<locale>/`, and passes `--forced-subtitles <path>`.
  2. `games/school-days/src/main.cpp` + `lib/src/core/CommandLine.cpp`: wire `--forced-subtitles` to `EngineOptions::forcedSubtitlesPath` and call `engine.GetForcedSubtitles().setLocale(...)` for both native GPK and JSON launches.
  3. `lib/src/components/ForcedSubtitleRenderer.cpp`: fixed case-sensitive extension filter (`entry.path().extension() != ".ass"`) to match `.ass`/`.ASS` case-insensitively (`asciiLower(...)`) and normalize cache keys to `<stem>.ass` on case-sensitive filesystems (Android/Linux).
  4. `lib/src/components/SubtitleScene.cpp` + `SystemFontFile.java`: `libass` uses `/system/fonts/NotoSansCJK-Regular.ttc` (32 MB on POCO X7 Pro), covering Latin accents, Cyrillic, and CJK glyphs.
- **Automated tests**:
  - `Kotonoha.Presentation` (`lib/tests/PresentationTests.cpp`, extended with uppercase `.ASS` discovery in an explicit locale directory), `SchoolDaysPortTest`, and `SchoolDaysRuntimeConfigStorageTest` passed (3/3).
- **Real hardware verification (POCO X7 Pro, `2712×1220`)**:
  - Launcher **Files** tab confirmed `forced_subtitles/: OK` alongside `30 / 30 (LxH.GPK: OK)`, `ktnroute: OK`, `styles.ass: OK`, `overlay/: OK`.
  - Confirmed staged `cache/sd-saf-*/game-assets/forced_subtitles/pt-BR/01-01-00-OP1.ass`.
  - Live gameplay captures on node `01/01-00-OP1` (with `F8` node clock HUD at `70.81s`, `78.16s`, `85.65s`) confirmed real `libass` `\kf` karaoke sweeps (top romaji + bottom PT-BR translation + credit-safe `\pos(400,316)`) rendered cleanly over the opening video.
  - Natural node exit from `01/01-00-OP1` to `01/01-00-B05` immediately cleared all OP forced subtitles with zero stale text; `Display subtitles` (`TextView`) `OFF`/`ON` and launcher `enableForcedSubtitles` switch verified.
- **Classification**: **Confirmed parity** (`Paridade confirmada`, Android DEV & Windows; Android DIST built and statically verified in Milestone 4).

---

## Android DIST Build & DEV vs DIST Comparison (Milestone 4)

### Dedicated `dist` Build Type (`buildsystems/android/app/build.gradle`)
- Added **`dist`** `buildType` (inheriting `release`), isolated from `debug` (DEV):
  - `applicationIdSuffix '.dist'` → isolated package ID **`me.hirameki.kotonoha.schooldays.dist`**, coexisting side-by-side with DEV (`me.hirameki.kotonoha.schooldays`) with isolated save/config storage via `getKotonohaPackageIdentifier()`.
  - `versionNameSuffix '-dist'`, `debuggable false`, `minifyEnabled true`, `shrinkResources true`.
  - Release/DIST CMake flags: `-DKOTONOHA_DISTRIBUTION_BUILD=ON`, `-DKOTONOHA_ENABLE_MCP=OFF`, `-DCMAKE_BUILD_TYPE=Release`.
  - `BuildConfig.IS_DISTRIBUTION_BUILD = true` (`false` in `debug`/`release`).
  - Signing: `signingConfigs.debug` (**test-signed DIST for QA**, explicitly not store-signed).
  - Updated `buildsystems/android/app/proguard-rules.pro` (`-keep class org.kotonoha.android.GameActivity` preserving `getKotonoha*`/`setKotonoha*`) and fixed Gradle 8 task dependency between `generate<Variant>LintVitalReportModel`/`lintVitalAnalyze<Variant>` and `stage<Variant>Assets`.

### Binary & Packaging Audit (DEV vs DIST)
| Check | Android DEV | Android DIST (test-signed) |
|---|---|---|
| Package ID | `me.hirameki.kotonoha.schooldays` | `me.hirameki.kotonoha.schooldays.dist` |
| `check_school_days_apk.py` | PASS (6 ARM64 libs, 16 KiB LOAD, 47 assets) | PASS (6 ARM64 libs, 16 KiB LOAD, 47 assets) |
| `zipalign -c -P 16 4` | PASS | PASS |
| `apksigner verify` | v2 OK (1 signer) | v2 OK (1 signer, test-signed) |
| `libSchoolDays.so` | 4,532,560 bytes | **2,326,704 bytes (-48.7%)** |
| `libkotonoha_engine.so` | 35,615,512 bytes | **21,886,632 bytes (-38.5%)** |
| `Kotonoha Debug Applets` (`DebugOverlay`) | Present (`True`) | **Absent (`False`)** |
| `BackLog unavailable:` (DIST path, no `VNBacklog` fallback) | Absent (`False`) | **Present (`True`)** |
| Live POCO X7 Pro runtime check | Executed & validated | **Not tested on device** — `adb install` of the new package ID `me.hirameki.kotonoha.schooldays.dist` returned `INSTALL_FAILED_USER_RESTRICTED` (Xiaomi HyperOS "Install via USB" requires manual user approval on-screen for new package IDs) |

---

## Comprehensive Gameplay QA & Fixes (Milestone 5)

### Two Real Defects Found & Fixed During Hardware QA
1. **[P1 — Windows & Android] `BACKLOG` shrunk to ~78% in top-left corner when `renderScale < 1.0` or target ≠ 1280×720**:
   - **Root cause**: `VisualTransition::copy()` and `VisualTransition::compose()` (`lib/src/core/VisualTransition.cpp`) disabled logical presentation (`SDL_LOGICAL_PRESENTATION_DISABLED`) for 1:1 snapshots, and `Engine.cpp` invoked `runtime.renderNarrativeOverlay()` immediately afterward without restoring `1280×720` logical presentation on `novelTarget`. At Android's default `renderScale = 0.75` (`1627×915`), BackLog rendered in raw `1280×720` pixels at top-left, leaving a white L-shaped border and misaligned touch hitboxes.
   - **Fix**: `lib/src/core/Engine.cpp` restores `SDL_SetRenderTarget` + `SDL_SetRenderLogicalPresentation(..., novelView.x, novelView.y, SDL_LOGICAL_PRESENTATION_STRETCH)` right after `visualTransition.compose(...)` and before `runtime.renderNarrativeOverlay(...)`.
   - **Regression test**: Added `renderNarrativeOverlay` logical-presentation assertion to `lib/tests/PresentationTests.cpp` (`Kotonoha.Presentation`, passed). Verified full 16:9 viewport BackLog & exact `CLOSE` touch hitbox on POCO X7 Pro.
2. **[P1 — Android] `SAVE` comment editor hidden behind landscape IME keyboard**:
   - **Root cause**: `BuildCommentEditorLayout()` (`games/school-days/src/SchoolDaysSystemUi.cpp`) vertically centered the `900×170` dialog (`y ≈ 525..695` on `2712×1220`), completely covered by Gboard (`y ≈ 400..1220`).
   - **Fix**: Top-anchored on `#if defined(__ANDROID__)` (`y ≈ 8%` of height; Windows remains centered), keeping the input box, `OK`, and `Cancel` 100% visible above Gboard.
   - **Verification**: Visually verified above Gboard on POCO X7 Pro; isolated Page 10 / Slot 99 `SAVE` (`SaveFile0099.DAT` + `.kotonoha-lxh.v1`) with typed comment + `LOAD` restore + cleanup confirmed.

### Hardware QA Summary (POCO X7 Pro, Android DEV)
- **A. Launcher**: Portrait UI, `TitleBGM`, `30/30` (`LxH.GPK: OK`), `ktnroute: OK`, `styles.ass: OK`, `forced_subtitles/: OK`, `overlay/: OK`, GPK (`pt-BR`) ↔ JSON (`ru`, `zh-CN`), `Apply`/`Reset defaults`, exact missing GPK diagnostics — **Adapted & validated**.
- **B. Display & Graphics**: `2712×1220 @ 30/60/90/120 Hz`, VSync, `16:9`/`4:3`, `renderScale`, `Option_Display` overlay, bidirectional persistence — **Confirmed parity** (MPV/GLSL on GLES2 falls back safely to linear copy when unsupported — **Partial**).
- **C. Gameplay**: `TITLE`, `START`, `MENU`, `MenuBar` (`×1`–`×32`, `◀|`, `▶|`), isolated `SAVE`/`LOAD` slot 99, full-viewport `BACKLOG`, `OPTION` (3 tabs), `SaveFeel` (30 s countdown + feeling meters), `TOPMENU` (`SYSBASE` + `Return to Title Screen?` → `YES` → `TITLE`) — **Confirmed parity** (timed `SETSELECT`, `ROUTE MAP`, `REPLAY` covered by 15/15 automated CTests on Windows; live touch QA on device remains **Partial** for unexercised menus).
- **D. Audio & Video**: Fixed single-gain `BGM`/`SE`/`Voice`, multi-minute Episode 1 WMV+OGG playback including `01/01-00-OP1`, `HOME` background suspend → clean resume — **Confirmed parity** (subjective physical speaker loudness check recommended by user).
- **E. Subtitles & Translations**: `pt-BR` GPK (accents + speaker colors + OP forced karaoke), `zh-CN` JSON (Simplified Chinese Hanzi in BackLog, zero tofu), `ru` JSON (clean launch & playback) — **Confirmed parity**.
- **F. Saves & State**: Atomic `SaveFile0099.DAT` + `.kotonoha-lxh.v1` + `GlobalFlag.DAT` + `Config.DAT` + `settings.json`, `force-stop` persistence, suspend/resume — **Confirmed parity**.

---

## Validation of All Seven L×H Endings (Milestone 6)

All **7 integrated *School Days L×H* endings** from `LxH.GPK` were executed and verified live on the **POCO X7 Pro (Android 16, ARM64, `2712×1220`)** on **Android DEV**, using the DEV-only `Filter nodes` input added to `DebugOverlay::drawRuntimeNodes()`, with live screenshots and `SDL/CUSTOM` `logcat` confirmation:

| # | Node | L×H Ending | Tested Locale | EndRoll → L×H Boundary (ticks) | L×H Media (`LxH.GPK`) | Fade OUT/IN + Epilogue + Subtitles | Terminal Next → `SaveFeel` → Title | Classification |
|---|---|---|---|---|---|---|---|---|
| 1 | `05/05-A2-Z00` | *Behind the Scenes* | `zh-CN` (JSON) | `6003` → `6075` | `05-A2-Z00-LXH.wmv` (69 ms) + `.OGG` (33 ms) | OK (fade OUT `6002`, IN `6075`, `zh-CN-lxh.json` Hanzi) | OK (`tick=13133`, countdown, auto-return to `TITLE` + `REPLAY` unlocked) | **Confirmed parity** |
| 2 | `05/05-A2-F00` | *Blue Smile* | `pt-BR` (GPK) | `5867` → `5915` | `05-A2-F00-LXH.wmv` (83 ms) + `.OGG` (51 ms) | OK (fade OUT `5851`, IN `5915`, grave epilogue) | OK (`tick=8967`, `SaveFeel` 30 s, return to `TITLE`) | **Confirmed parity** |
| 3 | `05/05-B0-F00` | *Cold Scarf* | `pt-BR` (GPK) | `5683` → `5731` | `05-B0-F00-LXH.wmv` (74 ms) + `.OGG` (73 ms) | OK (fade OUT `5642`, IN `5731`, PT-BR subtitle *"Estava na varanda da sua casa..."*) | OK (`tick=8110`, `SaveFeel`, `NO` → `TITLE`) | **Confirmed parity** |
| 4 | `05/05-G0-L03` | *Braided Hair* | `pt-BR` (GPK) | `4095` → `4119` | `05-G0-L03-LXH.wmv` (73 ms) + `.OGG` (51 ms) | OK (fade OUT `4076`, IN `4119`, sunset epilogue) | OK (`tick=7119`, `SaveFeel`, `NO` → `TITLE`) | **Confirmed parity** |
| 5 | `05/05-KC-F00` | *The End of Madness* | `pt-BR` (GPK) | `19957` → `20005` | `05-KC-F00-LXH.wmv` (72 ms) + `.OGG` (57 ms) | OK (fade OUT `19941`, IN `20005`, PT-BR subtitle *"Ei, espera por mim!"*) | OK (`tick=25527`, `SaveFeel`, `NO` → `TITLE`) | **Confirmed parity** |
| 6 | `05/05-SB-E01` | *Light* | `pt-BR` (GPK) | `6668` → `6668` (same-tick boundary) | `05-SB-E01-LXH.wmv` (84 ms) + `.OGG` (74 ms) | OK (fade OUT `6639`, IN `6668`, PT-BR subtitle *"A Kuroda vai ficar chateada..."*) | OK (`tick=14421`, `SaveFeel`, `NO` → `TITLE`) | **Confirmed parity** |
| 7 | `05/05-SC-L03` | *Innocent Sunset* | `pt-BR` (GPK) | `5111` → `5183` | `05-SC-L03-LXH.wmv` (83 ms) + `.OGG` (58 ms) | OK (fade OUT `5071`, IN `5183`, PT-BR subtitle *"Então, que tal irmos àquele café..."*) | OK (`tick=8042`, `SaveFeel`, `NO` → `TITLE`) | **Confirmed parity** |

### Integrity & Post-Ending Checks
- Verified both JSON (`zh-CN-lxh.json`) and native GPK (`LxH.PTBR.GPK`) L×H translation paths against the untouched retail `LxH.GPK`.
- Verified `REPLAY` menu unlock and rendering (`H-scene` / `Replay Data`, pages `1–4`, `CLOSE`) on `TITLE`, `GlobalFlag.DAT` update, and zero modification to the user's personal `SaveFile000.DAT` (`1210 bytes`).
- Zero crashes, zero hangs, and zero missing L×H assets across all 7 endings back-to-back.
- DEV-only convenience: added `Filter nodes` input to `DebugOverlay::drawRuntimeNodes()` (`lib/include/Kotonoha/debug/DebugOverlay.hpp`, `lib/src/debug/DebugOverlay.cpp`; absent in DIST).
