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

---

## Android Benchmark & Optimizations (Milestone 7)

### Real Bottlenecks Identified by Profiling on POCO X7 Pro (`2712×1220`, `120 Hz` SurfaceFlinger)
1. **Per-frame JNI + Binder IPC in `Display Options` (`games/school-days/src/SchoolDaysRuntime.cpp`)**: `renderDesktopDisplayPanel()` called `AndroidDisplayModesUnique()` (2 JNI calls + `WindowManager.getDefaultDisplay().getSupportedModes()`) on every frame (~120×/s), spiking `SDLThread` from `42.0%` (`General Options`) to `52.3%` (`Display Options`).
   - **Optimization**: Added session-scoped `androidDisplayModesCache` (`SchoolDaysRuntime.hpp`), populated once in `resetDesktopDisplayDraft()` and cleared in `discardDesktopDisplayDraft()`.
2. **Orphaned SAF staging directories (`games/school-days/android/java/org/kotonoha/android/SchoolDaysFolder.java`)**: Force-stops/crashes left `cache/sd-saf-*` staging folders behind indefinitely (**19 accumulated directories** observed on device).
   - **Optimization**: Added `pruneStaleSessions(context.getCacheDir(), session)` at the end of `prepare()` to remove old `sd-saf-*` directories automatically.
3. **Retained gameplay textures upon returning to `TITLE` (`games/school-days/src/SchoolDaysRuntime.cpp`)**: `openTitle()` cleared `abcTextures` but left `backgroundComposite` and `AssetProvider`'s texture cache alive.
   - **Optimization**: `openTitle()` now resets `backgroundComposite` and invokes `assets->clearCache()`.
4. **Redundant FreeType/SDL3_ttf `TTF_SetFontSize()` calls (`lib/src/core/UI.cpp`)**: `UI::textSized()`, `textHeight()`, `textWidth()`, and `measureSized()` unconditionally called and restored `TTF_SetFontSize()` even when the target size already matched the active font size.
   - **Optimization**: Guarded `TTF_SetFontSize()` and its RAII restore with `std::abs(targetSize - oldSize) > 0.01f`.

### BEFORE vs AFTER Measurements (POCO X7 Pro, Android DEV, PT-BR GPK)
| Metric | BEFORE | AFTER | Delta / Notes |
|---|---|---|---|
| `cache/sd-saf-*` directories on device | `19` | **`1`** | **-18 orphaned sessions (-94.7%)**, pruned automatically on launch |
| `AndroidDisplayModesUnique()` JNI/Binder calls in `Display Options` | ~120×/s (every frame) | **1× per tab open** | **-100% per-frame IPC** |
| Steady-state `SDLThread` CPU in `Display Options` (`120 Hz`, `top -H`) | `52.3%` (`51.8 / 52.0 / 53.0%`) | **`46.1%`** (`44.4 / 45.0 / 45.0 / 50.0%`) | **-6.2 pp (-11.9% relative)**, pixel-identical output (`1,341,866 B`) |
| Active gameplay memory (`dumpsys meminfo`) | `TOTAL PSS 692,951 KB` / `GL mtrack 187,008 KB` | `TOTAL PSS 718,461 KB` / `GL mtrack 189,648 KB` | Stable (no regression) |
| `TITLE` memory after returning from gameplay | `GL mtrack 371,888 KB` / `TOTAL PSS 1,017,758 KB` (accumulated post-7 endings) | `GL mtrack 207,904 KB` / `TOTAL PSS 726,585 KB` (post-gameplay return) | Scene cache released on `openTitle()` |
| Background lifecycle (suspend / screen off) | SDL3 pauses `SDLThread` & audio via `SDL_HINT_ANDROID_BLOCK_ON_PAUSE` | Preserved + atomic `Config.DAT` flush on `SDL_EVENT_WILL_ENTER_BACKGROUND` | Zero narrative clock drift (`93.46s → 93.65s` across 25 s sleep) |

> **Honest engineering note**: The ~40–45% baseline on static menus occurs when the launcher FPS cap is set to `Unlimited (screen Hz)`, because the POCO X7 Pro's SurfaceFlinger drives `GameActivity` at `120 Hz` (`8.33 ms/frame`). Selecting `60 FPS` or `30 FPS` in Launcher Options reduces GPU/CPU frame cadence while preserving the `24 fps` story and video clock.

---

## Android DEV/DIST Parity Audit & Windows DEV/DIST Regression Summary (Milestone 8)

### Comparative Parity Matrix (6 Honest Classifications)
Classifications used: **Confirmed parity** (`Paridade confirmada`), **Adapted & validated** (`Adaptado e validado`), **Partial** (`Parcial`), **Non-functional** (`Não funcional`), **Not tested** (`Não testado`), **Not applicable** (`Não aplicável`).

| Feature / Flow | Windows DEV | Windows DIST | Android DEV (POCO X7 Pro) | Android DIST (test-signed) |
|---|---|---|---|---|
| Launcher (languages, display, audio, files) | Confirmed parity | Confirmed parity | Adapted & validated | Not tested on device (HyperOS USB prompt for new package ID; code & APK verified) |
| `30/30` GPKs + `LxH.GPK` + `ktnroute` + `styles.ass` + `overlay/` | Confirmed parity | Confirmed parity | Adapted & validated (zero-copy SAF) | Not tested on device (static/APK checks OK) |
| Viewport-scoped white fade (`#000000` bars in `16:9` & `4:3`) | Confirmed parity | Confirmed parity | Confirmed parity | Not tested on device (shared C++ verified) |
| Forced subtitles (`.ass`/`.ASS`, `\kf` karaoke, 5 locales, `TextView` toggle) | Confirmed parity | Confirmed parity | Confirmed parity | Not tested on device (shared C++ verified) |
| `TITLE`, `START`, `MENU`, `MenuBar` (`×1`–`×32`, `◀\|`, `▶\|`), `TOPMENU` | Confirmed parity | Confirmed parity | Confirmed parity | Not tested on device |
| `SAVE` / `LOAD` (atomic slots + `.kotonoha-lxh.v1` + comment editor) | Confirmed parity | Confirmed parity | Adapted & validated (editor anchored above IME) | Not tested on device |
| `BACKLOG` (full viewport at `renderScale < 1.0`, multi-locale) | Confirmed parity | Confirmed parity | Confirmed parity | Not tested on device (`VNBacklog` fallback absent in `.so`) |
| `OPTION` (`General`, `Sound`, `Display Options` + bidirectional persistence) | Confirmed parity | Confirmed parity | Confirmed parity | Not tested on device |
| Timed choices (`SETSELECT`) & `ROUTE MAP` | Confirmed parity | Confirmed parity | Partial (15/15 shared CTests passing; partial live touch) | Not tested on device |
| `REPLAY` (`H-scene` / `Replay Data`, post-ending unlock) | Confirmed parity | Confirmed parity | Confirmed parity | Not tested on device |
| All **7 L×H endings** (`05-A2-Z00`, `05-A2-F00`, `05-B0-F00`, `05-G0-L03`, `05-KC-F00`, `05-SB-E01`, `05-SC-L03`) | Confirmed parity | Confirmed parity | Confirmed parity (7/7 verified live on POCO X7 Pro) | Not tested on device |
| Audio (`BGM`/`SE`/`Voice`/`Master`/`Movie`, no double attenuation) | Confirmed parity | Confirmed parity | Confirmed parity (user speaker loudness check recommended) | Not tested on device |
| WMV video + MPV/GLSL upscaling | Confirmed parity | Confirmed parity | Partial (WMV OK; MPV/GLSL on GLES2 falls back safely to linear copy when unsupported) | Not tested on device |
| `DebugOverlay` (`F10`) / `Filter nodes` / MCP | Confirmed parity (present) | Not applicable (compiled out) | Confirmed parity (present; MCP `OFF`) | Not applicable (absent in `.so`) |
| Automatic SAF cache pruning (`sd-saf-*`) | Not applicable | Not applicable | Adapted & validated (`19 → 1` dir) | Not tested on device (bundled in APK) |

### Windows DEV & DIST Regression Verification
- **`python tools/ValidateBuildMetadata.py`**: `[Kotonoha] OK: 13 CMake games; 22 existing Xcode schemes; Xcode pending: n2system, ponscripter`.
- **Windows DEV Release (`build/school-days-l4-two-dll-staticmd`)**:
  - Full build (`cmake --build ... --config Release --parallel 1`) succeeded cleanly.
  - Full `ctest` suite: **97 / 98 passed** (**58 / 58 School Days tests passed**, 100%). The single failure is the pre-existing unrelated `Kotonoha.Infrastructure` (`"MetalFX subclass rejects a software renderer"`).
- **Windows DIST Release (temporary isolated `-DKOTONOHA_DISTRIBUTION_BUILD=ON` build, removed after testing)**:
  - Clean build of `SchoolDays.exe`, `kotonoha.dll`, and 5 DIST test suites.
  - DIST `ctest`: **5 / 5 passed** (`SchoolDaysPortTest`, `SchoolDaysLxhContentTest`, `SchoolDaysLxhFadeTest`, `SchoolDaysSystemPanelTransitionTest`, `SchoolDaysSystemUiCommentEditorTest`).

### Final Android ARM64 Artifacts (Post-Milestone 7)
| Artifact | Package ID | Size | SHA-256 | Verification |
|---|---|---|---|---|
| `build/SchoolDaysHQ-android-arm64-dev.apk` | `me.hirameki.kotonoha.schooldays` | `33,325,413 B` | `47FBA33B792054FAC03C8F620BD81422000C297EE8DFC4107739C0EEC2746941` | `check_school_days_apk.py` PASS, `zipalign -c -P 16 4` PASS, `apksigner` v2 OK, installed & verified on POCO X7 Pro |
| `build/SchoolDaysHQ-android-arm64-dist-test.apk` | `me.hirameki.kotonoha.schooldays.dist` | `15,748,932 B` | `9F05CC93661219F439E319C8CF8E314B036203492D0E14AAE3D6D3A3B7105EEC` | `check_school_days_apk.py` PASS, `zipalign -c -P 16 4` PASS, `apksigner` v2 OK (test-signed); awaits user manual HyperOS "Install via USB" tap |

> **Installing & smoke-testing the DIST APK on POCO X7 Pro**: With the phone unlocked, run `adb -s QO5XJZFYDMRWDU6T install build/SchoolDaysHQ-android-arm64-dist-test.apk` and tap **Install** on Xiaomi HyperOS's on-screen prompt (or sideload manually). It installs side-by-side with the DEV package without touching existing DEV saves or settings.

---

## DEV Exclusion Audit on Android DIST

Using **Windows DIST** (`KOTONOHA_DISTRIBUTION_BUILD=ON`, `KOTONOHA_DEV_CONTROL=OFF`, `KOTONOHA_ENABLE_MCP=OFF`, `KOTONOHA_ENABLE_PLAYER_DUBBING=OFF`) as the authoritative reference policy, we performed a full audit across CMake, Gradle, Java, C++, ELF `.so` binaries, and live side-by-side testing (DEV vs DIST) on the POCO X7 Pro (`QO5XJZFYDMRWDU6T`).

### Root Causes Found & Modified Files
1. **Extra Android `MENU` button (rendering + touch hitbox) compiled into Android DIST** (`games/school-days/src/SchoolDaysRuntime.cpp`, `games/school-days/src/SchoolDaysRuntime.hpp`):
   - **Root cause**: In `Runtime::renderScreen()` and `Runtime::onScreenEvent()`, the `{18.0f, 649.0f, 118.0f, 54.0f}` `"MENU"` button and its touch hit-test (`openConfig(...)`) were guarded only by `#if defined(__ANDROID__) || defined(ANDROID)`, omitting `!defined(KOTONOHA_DISTRIBUTION_BUILD)`.
   - **Fix**: Tightened both guards to `#if (defined(__ANDROID__) || defined(ANDROID)) && !defined(KOTONOHA_DISTRIBUTION_BUILD)` and exposed `Runtime::AndroidExtraMenuButtonCompiled` in `SchoolDaysRuntime.hpp`. Preserved `ownsMobileMenuButton() { return true; }` so `Engine.cpp` never re-enables the generic `{16, 16, 76, 36}` engine menu button in DIST; players continue accessing `OPTION`, `SAVE`, `LOAD`, `BACKLOG`, and `TOPMENU` through the retail top `MenuBar` and Android's system Back key (`SDLK_AC_BACK -> SDLK_ESCAPE`).
2. **Explicit DEV/DIST CMake flags in Gradle** (`buildsystems/android/app/build.gradle`):
   - **Root cause**: `buildTypes.dist` did not explicitly pass `-DKOTONOHA_DEV_CONTROL=OFF` or `-DKOTONOHA_ENABLE_PLAYER_DUBBING=OFF`, and `buildTypes.debug` relied on defaults for `KOTONOHA_DISTRIBUTION_BUILD`.
   - **Fix**: Explicitly set `-DKOTONOHA_DISTRIBUTION_BUILD=OFF` on `debug` and `-DKOTONOHA_DISTRIBUTION_BUILD=ON`, `-DKOTONOHA_DEV_CONTROL=OFF`, `-DKOTONOHA_ENABLE_MCP=OFF`, `-DKOTONOHA_ENABLE_PLAYER_DUBBING=OFF`, `-DCMAKE_BUILD_TYPE=Release` on `dist`.
3. **Unnecessary `DebugOverlay.hpp` include in DIST** (`games/school-days/src/SchoolDaysRuntime.cpp`):
   - Guarded `#include <Kotonoha/debug/DebugOverlay.hpp>` with `#if !defined(KOTONOHA_DISTRIBUTION_BUILD)`.
4. **Compile-time contract test** (`games/school-days/tests/SchoolDaysDistDebugAbsenceTest.cpp`):
   - Added `static_assert(!SchoolDays::Runtime::AndroidExtraMenuButtonCompiled, ...)` ensuring no DIST build compiles the extra `MENU` button.
5. **Android Launcher (`SchoolDaysSetup.java`, `GameActivity.java`, `BootstrapActivity.java`, `school_days_setup.xml`)**:
   - Fully audited: zero debug/DEV controls exposed; its 6 tabs (`Display`, `Graphics`, `Audio`, `General`, `Language`, `Game Files`) match Windows DIST policy (no `Advanced` tab, no engine upscaling debug options).

### Comparative DEV vs DIST Matrix (Windows and Android)
| Feature / Mechanism | Windows DEV | Windows DIST | Android DEV (`me.hirameki.kotonoha.schooldays`) | Android DIST (`me.hirameki.kotonoha.schooldays.dist`) |
|---|---|---|---|---|
| `KOTONOHA_DISTRIBUTION_BUILD` | `OFF` | `ON` | `OFF` (explicit in Gradle) | `ON` (explicit in Gradle) |
| `KOTONOHA_DEV_CONTROL` / `KOTONOHA_ENABLE_MCP` | `OFF` / optional | `OFF` / `OFF` | `OFF` / `OFF` | `OFF` / `OFF` (explicit) |
| `KOTONOHA_ENABLE_PLAYER_DUBBING` | `OFF` | `OFF` | `OFF` | `OFF` (explicit) |
| `BuildConfig.IS_DISTRIBUTION_BUILD` / `android:debuggable` | N/A | N/A | `false` / `true` | `true` / absent (non-debuggable) |
| Extra `MENU` button `{18, 649, 118, 54}` + hitbox | Absent (desktop) | Absent (desktop) | Present (`UI::button` + `openConfig`) | **Compiled out** (visual and hitbox absent) |
| Generic `Engine::mobileMenuButton` `{16, 16, 76, 36}` | Absent | Absent | Suppressed (`ownsMobileMenuButton=true`) | Suppressed (`ownsMobileMenuButton=true`) |
| `DebugOverlay` (`F10`) / `FPS` (`F9`) / `Node HUD` (`F8`) | Present | Compiled out | Present (`F10` opens `Kotonoha Debug Applets`) | **Compiled out** (`F8`/`F9`/`F10` no-op; `0` symbols in `.so`) |
| `MCPServer` / `VoiceDubbingApplet` / `VNBacklog` fallback | Present per flags | Compiled out | `MCPServer=0`; `VoiceDubbing` in engine | **Compiled out** (`0` symbols in `.so`) |
| DEV CLI flags (`--mcp-port`, `--dump-ktrf`, `--ktrf`, `--node`, `--log`) | Present | Compiled out | Not exposed | **Compiled out** (unregistered in CLI parser) |
| Retail `MenuBar` (`SAVE`, `LOAD`, `BACKLOG`, `OPTION`, `TOPMENU`) | Present | Present | Present | **Verified live on POCO** (top-edge tap) |
| `OPTION` (`General`, `Sound`, `Display Options`), `BACKLOG`, `SAVE`/`LOAD`, Android Back | Present | Present | Present | **Verified live on POCO** |
| Save isolation (`SaveFile000.DAT`, `GlobalFlag.DAT`, `settings.json`) | Dedicated dir | Dedicated dir | `/Android/data/...schooldays/files/` untouched | `/Android/data/...schooldays.dist/files/` isolated |

### Effective Build Flags, ELF `.so` Binary & Disassembly Evidence
- **Effective `CMakeCache.txt`**:
  - DEV (`Debug/4t3j1z2r/arm64-v8a`): `CMAKE_BUILD_TYPE=Debug`, `KOTONOHA_DISTRIBUTION_BUILD=OFF`, `KOTONOHA_DEV_CONTROL=OFF`, `KOTONOHA_ENABLE_MCP=OFF`, `KOTONOHA_ENABLE_PLAYER_DUBBING=OFF`.
  - DIST (`Release/4t736a4f/arm64-v8a`): `CMAKE_BUILD_TYPE=Release`, `KOTONOHA_DISTRIBUTION_BUILD=ON`, `KOTONOHA_DEV_CONTROL=OFF`, `KOTONOHA_ENABLE_MCP=OFF`, `KOTONOHA_ENABLE_PLAYER_DUBBING=OFF`.
- **`BuildConfig` & `AndroidManifest.xml` (`aapt2 dump xmltree`)**:
  - DEV: `DEBUG=true`, `IS_DISTRIBUTION_BUILD=false`, `VERSION_NAME=0.1.0`, `android:debuggable=true`.
  - DIST: `DEBUG=false`, `IS_DISTRIBUTION_BUILD=true`, `VERSION_NAME=0.1.0-dist`, no `android:debuggable`.
- **Dynamic symbol audit (`llvm-nm -D -C`) on packaged APKs**:
  - DEV: `libSchoolDays.so` (`4,533,568 B`), `libkotonoha_engine.so` (`35,628,472 B`) — `DebugOverlay=39` (`libkotonoha_engine.so`) + `1` (`libSchoolDays.so`), `VoiceDubbingApplet=45`, `Engine::GetUI()` present.
  - DIST: `libSchoolDays.so` (`2,325,600 B`), `libkotonoha_engine.so` (`21,887,480 B`) — **`DebugOverlay=0`, `MCPServer=0`, `VoiceDubbingApplet=0`, `Engine::GetUI()=0`**; `'Kotonoha Debug Applets'`, `'--dump-ktrf'`, `'--mcp-port'` strings absent. *(Transparent note: shared internal error strings identical to Windows DIST in `SchoolDaysAssets.cpp:475` and `SchoolDaysKtrfGameplayBridge.cpp:676` remain unreachable in DIST because neither `--ktrf` CLI registration nor `debugJumpToNode` callers exist.)*
- **Machine-code disassembly proof (`llvm-objdump -d`) on unstripped `libSchoolDays.so`**:
  - DEV (`Debug/4t3j1z2r`): `SchoolDays::Runtime::renderScreen` contains `bl` calls to `Kotonoha::Engine::GetUI()` and `Kotonoha::UI::button(...)`; `onScreenEvent` contains a `bl` call to `SchoolDays::Runtime::openConfig(...)`.
  - DIST (`Release/4t736a4f`): `renderScreen` has **`0` calls** to `UI::button`/`GetUI`; `onScreenEvent` has **`0` calls** to `openConfig`.

### Live Hardware Verification (POCO X7 Pro, `QO5XJZFYDMRWDU6T`) & Windows Regressions
- **Android DEV installed & verified (`me.hirameki.kotonoha.schooldays`)**: Launcher `0.1.0`, unlocked `TITLE` (reading its own `GlobalFlag.DAT`), and `KEYCODE_F10` opens `Kotonoha Debug Applets [F10]`.
- **Android DIST installed & verified (`me.hirameki.kotonoha.schooldays.dist`)**:
  - Launcher `0.1.0-dist` detects `Português (Brasil) (GPK)` via SAF without touching DEV data.
  - Episode 1 gameplay displays with zero extra `MENU` button and active PT-BR subtitles.
  - Direct tap on the former `MENU` coordinates (`400, 1145` on `2712×1220`): no-op (no invisible hit-test area).
  - `F10` (`140`), `F9` (`139`), and `F8` (`138`) on `TITLE` and during gameplay: zero overlays or HUDs opened.
  - Retail `MenuBar` opens via top-edge tap; `OPTION` (`General Options`, `Sound Options`, `SOMCON/Display Options`), Android Back dismissal (`KEYCODE_BACK`), full 16:9 `BACKLOG`, isolated `SAVE` screen, and `TOPMENU` (`Return to Title Screen?`) all visually verified.
  - Storage isolation verified: DEV's `SaveFile000.DAT` (`1,210 B`) and `SaveFile000.DAT.kotonoha-lxh.v1` (`243 B`) remained 100% untouched.
- **Windows Regressions**:
  - `python tools/ValidateBuildMetadata.py`: PASS (`13 CMake games; 22 existing Xcode schemes`).
  - **Windows DEV Release** (`build/school-days-l4-two-dll-staticmd`): clean build; `ctest` (`CommandLine`, `Presentation`, `Launcher`, `ImGuiTheme`, `SchoolDays`): **63 / 63 passed (100%)**.
  - **Windows DIST Release** (temporary isolated `-DKOTONOHA_DISTRIBUTION_BUILD=ON -DKOTONOHA_DEV_CONTROL=OFF -DKOTONOHA_ENABLE_MCP=OFF` build, removed after testing): clean build; `ctest`: **6 / 6 passed (100%)**, including **`SchoolDays.DistDebugAbsence`** with the new `AndroidExtraMenuButtonCompiled` assertion.

### Final Android ARM64 Artifacts (Post DEV/DIST Audit & Visual Parity)
| Artifact | Package ID | Size | SHA-256 | Verification |
|---|---|---|---|---|
| `build/SchoolDaysHQ-android-arm64-dev.apk` | `me.hirameki.kotonoha.schooldays` | `33,327,044 B` | `F649CA9F5F497D9B3701E8D3601442939E76C398C230DE42603E7D94220E31A4` | `check_school_days_apk.py --allow-local-launcher-art` PASS (49 assets), `SchoolDays.ico` adaptive icons, `SPLASH.PNG`, face-cropped banner, `Display Options` tab across all 3 tabs, verified on POCO X7 Pro |
| `build/SchoolDaysHQ-android-arm64-dist-test.apk` | `me.hirameki.kotonoha.schooldays.dist` | `16,662,635 B` | `A0D7D52AC9229607EA13DA4A14A3C253163BE3F2109AB178CAA1079A9F8B487A` | `check_school_days_apk.py --allow-local-launcher-art` PASS (49 assets), `SchoolDays.ico` adaptive icons, `SPLASH.PNG`, face-cropped banner, `Display Options` tab across all 3 tabs, verified on POCO X7 Pro |

### Additional Visual Parity (`Display Options` Tab, Splash, Icon, and Banner)
- **Persistent `Display Options` tab label on `General Options` and `Sound Options`** (`games/school-days/include/Kotonoha/routing/SchoolDaysSystemUi.hpp`, `games/school-days/src/SchoolDaysSystemUi.cpp`): when `Option_Def.png`/`Option_Sound.png` come from unmodified retail `System.GPK` while `Option_Display.png`/`Option_Display_Chip.png` exist in the overlay, tab 2 is automatically composited with `Display Options` (idle and hover), preserving custom Windows overrides whenever supplied.
- **Engine splash screen (`assets/SPLASH.PNG`)**: included in `stageSchoolDays*Assets` in `build.gradle`, running the exact Windows `Kotonoha::SplashScreen` animation (`0.8 s` fade-in, hold, `0.8 s` fade-out, `3.0 s` total) before the retail `Overflow` logo.
- **APK launcher icon (`assets/SchoolDays.ico`)**: generated at build time by Gradle task `generateSchoolDaysLauncherIcons` (`mipmap-mdpi` through `xxxhdpi` + `mipmap-anydpi-v26` adaptive icons), sharing the same icon source as the Windows executable (`CMakeLists.txt` updated with `assets/SchoolDays.ico` fallback).
- **Launcher top banner (`SchoolDaysBannerView.java`)**: upper character-focused crop of `SchoolDaysHQ-Launcher.png` with Kotonoha and Sekai on the left and a soft right-side gradient behind `School Days HQ` + subtitle (`LAUNCHER` / `SETUP / OPTIONS`), prioritizing the user's SAF folder with automatic fallback to locally staged build-time art (`-PincludeLauncherArt=false` for clean CI builds).

---

## Optional L×H, DIST Hardware Parity & Reset-Defaults Fix (2026-11-22)

Physical hardware verification on a **POCO X7 Pro** (Android 16 / API 36, `arm64-v8a`, `1220×2712` @ `120 Hz`, `MT6899`, ~11 GiB RAM) validated both **Android DEV** (`me.hirameki.kotonoha.schooldays`) and **Android DIST** (`me.hirameki.kotonoha.schooldays.dist`) side by side after integrating optional L×H (`EnableLxh=0|1`) and the shared performance optimizations.

### Confirmed Physical Hardware Results (POCO X7 Pro)
- **Optional L×H (`General → Enable L×H endings`)**:
  - Default ON when `LxH.GPK` is present (`Pronto · Português (Brasil) (GPK) · L×H ativo (7 epílogos)`), toggling OFF writes `EnableLxh=0` in `Kotonoha.ini` without clobbering `ContentAspectPreference` or `AndroidFrameLimit`, persists across `am force-stop`, and excludes `LxH.GPK` from `documents.json` (`0` L×H mounts; `School Days HQ startup: LxH=OFF`). Toggling back ON mounts `LxH.GPK` (`School Days HQ startup: LxH=ON (LxH endings enabled (7 epilogues ready).)`).
  - `Cancel` cleanly rolls back unsaved toggles without modifying `Kotonoha.ini`.
- **Defect Found & Fixed (`SchoolDaysConfigStore.saveKotonohaIni`)**:
  - **Root cause**: Tapping **Reset defaults** sets `Snapshot.enableLxh = null`, and `saveKotonohaIni()` previously skipped writing `EnableLxh` when `null`, leaving a stale `EnableLxh=0` line in `Kotonoha.ini` that silently reverted L×H to OFF on cold reopen.
  - **Fix (`538e3022`)**: Added `removeIniKey(lines, "EnableLxh")` in `SchoolDaysConfigStore.saveKotonohaIni()` when `s.enableLxh == null`, added automated JVM regression coverage in `games/school-days/android/tests/PackCaseRegression.java` (`PASS 14 synthetic pack & Kotonoha.ini EnableLxh cases`), and re-verified live on both Android DIST and DEV on POCO X7 Pro (`EnableLxh=` removed from `Kotonoha.ini`, cold reopen preserves `L×H ativo (7 epílogos)`).
- **DIST Gameplay, Multilingual Backends, Saves & Suspend/Resume**:
  - Cold launcher startup: `383 ms` (DIST), `548 ms` (DEV); `Play → GameActivity`: `0.78–1.26 s` (`2712×1220` landscape, `#000000` pillarboxes, zero `MENU` button, `KEYCODE_F10` confirmed no-op by byte-identical frame).
  - Verified live Episode 1 WMV+OGG playback with PT-BR GPK and PT-BR JSON backends, plus clean DIST launches in `Chinese - Simplified (GPK)` and `Russian (GPK)`.
  - Verified retail top `MenuBar` (`▶×1`–`▶×32`, `SAVE`, `LOAD`, `BACKLOG`, `OPTION` with `General`/`Display Options`, `TOPMENU`), Android Back dismissal, and live `HOME` Suspend/Resume (`5 s` background) with continuous playback and zero crashes.
  - Verified isolated `SAVE`/`LOAD` on Page 10 / Slot 100 (`SaveFile0099.DAT` + `SaveFile0099.DAT.kotonoha-lxh.v1` with `KOTONOHA_SCHOOL_DAYS_LXH_SAVE_V1` SHA-256 binding) while personal saves (`SaveFile000.DAT`, `SaveFile001.DAT`, `GlobalFlag.DAT`) remained byte-identical.
  - Confirmed `cache/sd-saf-*` pruning keeps only `1` active session directory and `KTN_S0` SAF spike replays all `18/18` persisted metrics (`leaks_private_paths=False`).

### Real Hardware Performance Baseline (POCO X7 Pro, `Thermal Status: 0`)
| Profile & Scene | Mode / Upscaler | `SDLThread` CPU | `TOTAL PSS` | `GL mtrack` | Temp (CPU / SKIN) |
|---|---|---:|---:|---:|---|
| Android DIST `TITLE` | `120 Hz`, `ArtCNN C4F16 DS 2.00x` | `~22.2%` | `610 MB` | `134 MB` | SKIN `36.4°C` (`gfxinfo` P50 `5 ms`, P90 `6 ms`, `1.53%` jank) |
| Android DIST Episode 1 Video + Subs | `120 Hz`, `ArtCNN C4F16 DS 2.00x` | `28.5%` | `685 MB` | `197 MB` | SKIN `36.7°C` |
| Android DIST Episode 1 Video + Subs | `60 FPS`, `ArtCNN C4F16 DS 2.00x` | `25.0%` | `670 MB` | `197 MB` | CPU `55.5°C` / SKIN `37.9°C` |
| Android DIST Episode 1 Video + Subs | `60 FPS`, `Linear 1.00x` | `25.9%` | **`529 MB` (-21%)** | **`55 MB` (-72%)** | CPU **`51.4°C` (-4.1°C)** |
| Android DEV `TITLE` (Debug `-O0`) | `120 Hz` / `60 FPS` / `30 FPS` | `120 / 59 / 29 FPS` | — | — | CPU `48.2°C` (`30 FPS`) |
| Android DEV Ep 1 Video (Debug `-O0`) | `120 Hz` / `60 FPS` / `30 FPS` | `48.1%` / `44.4%` / `40.7%` | `791 / 777 / 745 MB` | — | CPU `65.6°C / 59.2°C / 55.4°C` |

> **Engineering conclusion**: With shared C++ optimizations and Milestone 7 Android fixes already active, Release-optimized **Android DIST** sustains `2712×1220` Episode 1 WMV+OGG+subtitles+ArtCNN 2× at `28.5%` `SDLThread` CPU (`120 Hz`) and `25.0%` (`60 FPS`) with `Thermal Status: 0`. Switching to `Linear 1.00x` saves `141 MB` of `GL mtrack` (`-72%`) and lowers CPU temperature by `4.1°C`. No speculative C++ engine rewrites were introduced.

### Final Android ARM64 Artifacts (`538e3022`, 2026-11-22)
| Artifact | Package ID | Size | SHA-256 | Verification |
|---|---|---|---|---|
| `SchoolDaysHQ-Android-DEV-538e3022.apk` | `me.hirameki.kotonoha.schooldays` | `33,787,815 B` | `f0f41f59c622bc29f7c85d325368fca4da64299aca7f20930667fa72e043676c` | `check_school_days_apk.py --allow-local-launcher-art` PASS (6 ARM64 libs, 16 KiB LOAD, 49 assets), `zipalign -c -P 16 4` OK, `apksigner` v2 OK, verified live on POCO X7 Pro |
| `SchoolDaysHQ-Android-DIST-538e3022.apk` | `me.hirameki.kotonoha.schooldays.dist` | `16,687,007 B` | `840fcefc63543dad17d452ace4bf495993860c0b15a4e13d47da119c2bd9f18a` | `check_school_days_apk.py --allow-local-launcher-art` PASS (6 ARM64 libs, 16 KiB LOAD, 49 assets), `zipalign -c -P 16 4` OK, `apksigner` v2 OK, `DebugOverlay=False`, verified live on POCO X7 Pro |

---

## Port v1.00 Sign-Off & Official `forced_subtitles` v4 Integration (2026-11-22)

- **Managerial Approval Status**: **`ANDROID DEV/DIST — APROVADO PELO PROPRIETÁRIO`** (Kotonoha Port **v1.00** for **School Days HQ v1.02**).
  - **Audit transparency**: The automated/agent-verified hardware checks recorded above remain unchanged as historical technical records; the complementary human-dependent checks (post-reboot SAF persistence, subjective speaker audio loudness/balance, and visual sign-off) were personally performed and confirmed by the project owner.
- **Official `forced_subtitles` v4 Rollout (External SAF Resources Only)**:
  - Verified that both approved `538e3022` APKs embed **zero** `.ass` or `forced_subtitles` files and that `SchoolDaysFolder.prepare()` copies the active locale's `33` `.ass` files fresh from the external SAF tree into `cache/sd-saf-*` on every launch. Therefore, the approved `538e3022` APKs were **preserved without recompilation or reinstallation**.
  - Updated `/storage/emulated/0/Download/SchoolDaysHQ/game-assets/forced_subtitles/` on the POCO X7 Pro atomically with the official `v4` pack (`198` `.ass` files: `33` × `en`/`es`/`pt-BR`/`ru`/`zh-CN` + `33` legacy root `en`; canonical tree SHA-256 `064172a520b7aae1eabac0a2a078e1b866c821a123c7fe046ac8d7423462a818`; Setsuna `\pos(400,325)` and `\pos(400,420)` preserved byte-for-byte).
  - Verified on POCO X7 Pro: all `198/198` device files matched the canonical v4 tree SHA-256, all `30` GPKs and personal saves (`SaveFile000.DAT`, `SaveFile001.DAT`, `GlobalFlag.DAT`) remained byte-identical, both DIST and DEV launchers launched `GameActivity` cleanly, and live DEV SAF staging (`cache/sd-saf-*`) verified exact `pt-BR` v4 SHA-256 hashes for Sekai (`01-01-00-OP1.ass`), Setsuna (`01-01-00-OP2.ass`, `03-03-KA-OP1.ass`), and Kotonoha (`02-02-2K-OP1.ass`).
  - Future builds enforce the v4 pack via `games/school-days/tools/stage_forced_subtitles_v4.py` (`test_stage_forced_subtitles_v4.py`: `4/4 OK`), while keeping `forced_subtitles` optional at runtime for base School Days HQ.
