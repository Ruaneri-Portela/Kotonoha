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
- **Status**: Landscape 2712×1220, `16:9 ↔ 4:3` switching, and touch hitboxes across Title, `OPTION` (3 tabs + popups), MenuBar, `MENU`, and `BACKLOG` verified on POCO X7 Pro; extended P2 gameplay/L×H QA remains pending.
