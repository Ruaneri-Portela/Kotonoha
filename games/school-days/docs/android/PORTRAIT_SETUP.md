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

## Verification Summary (POCO X7 Pro, Android 16 / API 36)

Verified on physical **POCO X7 Pro (ARM64, Android 16)** with read-only SAF access to user `SchoolDaysHQ/`:
- Portrait launcher startup, SAF folder validation (`30/30` packs, KTRF, `styles.ass`, `overlay/`, GPK & JSON detection), all 6 Options tabs, `Apply`/`Cancel`/`Reset`, persistence to `settings.json` / `Config.DAT` / `Kotonoha.ini`, and live `TitleBGM` audio playback from `Ini.GPK` + `BGM.GPK`.
- Fast landscape launch (`sensorLandscape`, `opengles2`, low-latency audio) in both **JSON (`pt-BR`)** and **Native GPK (`pt-BR`)** modes without SIGABRT or silent fallback.
- Title screen, in-game retail `OPTION` (with `Display Options` overlay), Episode 1 HD `YUV420P` video + audio playback, subtitles, touch `MENU` button, and Home/Resume lifecycle (`!isTaskRoot()` guard).
- Memory footprint during gameplay: ~517 MB TOTAL PSS (Graphics ~78 MB).
- Windows DEV Release and Windows DIST Release builds & automated CTests passed with zero regressions.
