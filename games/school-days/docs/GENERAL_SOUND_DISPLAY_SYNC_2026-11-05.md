# School Days HQ v1.02 — General/Sound/Display integration (2026-11-05)

> **Historical intermediate state — superseded later on 2026-11-05.** The
> statements below describing the third tab as a School Days-specific Display
> panel, General FULL retaining Exclusive fullscreen, and WINDOW retaining
> custom window dimensions are no longer the implementation. The player
> explicitly corrected these requirements. See
> `GENERAL_DISPLAY_REVISED_USER_CONTRACT_2026-11-05.md` for the final behavior
> and new QA evidence. This document is retained to preserve the earlier
> audit/test record, not as the current specification.

## Request and interpretation

The player reported that General Options / Aspect Ratio changed the authored
16:9/4:3 layout, but Window Size had no physical effect; requested that both
buttons control their corresponding fields in the new remaster Display Options
page, that all Windows-supported resolutions be offered, and that all selected
General/Sound values survive a normal quit/restart.

Keep two evidence categories separate. The retail game has authenticated
logical `DisplayType` and `WindowMode (W)` branches for the four authored UI
resource families; it has **no demonstrated Windows 10/SDL fullscreen mode
contract**. The physical coupling between General Window/Full and the v3
Display panel, and remaster persistence for W, are **explicit player-requested
remaster adaptations**, not behavioral claims about a modern Windows original.
The original-style B/D/W resource projection is preserved in
`SchoolDaysDisplayPolicy` and the user can still select the 4:3/16:9 family.

## Confirmed pre-fix defects

- `Runtime::handleConfig(RequestFull)` changed only `displayBackend.windowMode`
  and rendered the authored family. It never called `engine->applySettings`,
  so General WINDOW/FULL did not change actual SDL window mode.
- `Runtime::handleConfig(RequestWide)` changed the retail display projection,
  but did not save `displayAspectPreference` to the same `Kotonoha.ini` key used
  by the remaster Display Aspect control. Reopening could show `Aspect: Auto`
  despite an explicit General WIDE/STANDARD choice.
- `Runtime::handleConfig(SelectPage::Display)` **closed the native Config
  panel and called generic `engine->openSettings()`**. Thus the native third
  tab did not expose the Win32 catalog that already existed in the remaster
  `renderDisplayPanel` implementation.
- The resolution chooser opened at a centered scroll position without an
  indicator/scrollbar/count, displaying only a middle slice. The selected
  label used `fullscreenWidth` even when the active mode was Windowed, and a
  cached selection index could remain stale after physical mode changes.
- The native `Config.DAT` parser read `WindowMode` but the remaster only cached
  DisplayType; changing General Window/Full did not persist W. On the original
  commercial binary, a native writer of W has not been authenticated.

## Current implementation

- General `WIDE(16:9)` / `STANDARD(4:3)` updates the same explicit remaster
  preference as Display `Aspect`, writing `ContentAspectPreference=2` or `1`
  in the isolated profile's `Kotonoha.ini`. Even clicking an already-selected
  button converts `Aspect: Auto` to the explicitly chosen value. Authored
  native `DisplayType` is still cached in `Config.DAT` only on accepted native
  changes. Changing aspect does **not** silently switch the user's chosen
  physical desktop resolution: the equivalent Display control is `Aspect`,
  not `Resolution`.
- General `WINDOW` maps to physical `EngineSettings.mode=Windowed`;
  `FULL` maps to Borderless at desktop-native fullscreen geometry or Exclusive
  for a chosen custom fullscreen mode (including different refresh rate).
  Existing Windowed dimensions are preserved. `Engine::applySettings` applies
  SDL geometry and persists the physical mode to `settings.json`. The native W
  resource selector remains consistent with the selected physical mode.
- Remaster-only `SchoolDaysConfigStorage::CacheWindowMode` updates `W` only
  for an explicit General or Display action (not on startup). This complements
  the authenticated native `Config.DAT` parser with player-requested
  persistence. A saved physical mode is authoritative for the two UIs on
  subsequent launches; without one, the legacy W key can initialize the
  authored state. `CacheState/Flush` continues to preserve unknown opaque
  keys, and shutdown writes only dirty native settings.
- The third tab now retains the native School Days Config panel at
  `Page::Display` and displays its custom remaster panel. The Windows catalog
  is the union of SDL and `EnumDisplaySettingsExW` for the matching SDL
  `HMONITOR`, deduplicated and ordered. The chooser shows current range and
  total count, a scrollbar, mouse-wheel, Up/Down, PageUp/PageDown and Home/End
  scrolling. Active Windowed width/height now selects the correct label. A
  Borderless resolution **or frequency** different from desktop is converted
  to Exclusive, so the changed physical mode actually takes effect.
- Sound BGM/SE/Voice plus/minus and levels 0–10, MenVoice, Mute,
  Skip/SuperSkip and subtitles continue to use the authenticated
  `SchoolDaysRetailConfigPolicy` and `Config.DAT` state. Level 0 does not feed
  zero-size source rectangles to `SDL_RenderTexture`. No audio asset is
  modified, and no new retail effect is invented.

## Windows mode enumeration observed (read-only)

One test desktop at 1366x768@60 Hz, device `\\.\DISPLAY1`/HMONITOR=0x10001:
`EnumDisplaySettingsExW` returned 37 modern 32-bpp DEVMODE entries, collapsing
to **29 width/height/Hz combinations across 11 distinct geometries**:
`1920x1080`, `1366x768`, `1360x768`, `1280x768`, `1280x720`, `1176x664`,
`1024x768`, `800x600`, `720x576`, `720x480`, `640x480`.
Read-only `ChangeDisplaySettingsExW(CDS_TEST)` accepted all 37 enumerated
entries. These results prove this machine's driver advertises the modes;
they are **not** a promise that every GPU, monitor, cable and refresh-rate
combination on another PC will apply successfully. The app uses runtime
enumeration rather than a hard-coded global list.

## Validation

- `SchoolDaysDisplayPolicyTest` exercises first-run desktop adoption, saved
  physical resolution, General Window/Full selection and custom/desktop
  Exclusive-vs-Borderless including refresh rate.
- `SchoolDaysRuntimeConfigStorageTest` uses a disposable test directory with
  `Config.DAT`, `settings.json` and `Kotonoha.ini` to exercise General
  Window→Full→Window restart persistence; WIDE/STANDARD→Display aspect restart
  persistence (including an already-active WIDE chip); and Sound 0/10/2,
  MenVoice, Mute, Skip, SuperSkip and subtitle selections on reload. The
  system's real School Days saves and config are never used.
- Actual DEV GUI smoke (direct script node, not clean-profile playthrough):
  `build/school-days-parity-2026-11-05/qa_general_display_interactive.py`
  with private `--documents`/`--config`/DEV port. Captures `qa-general-display-
  general-window-27342646153450.png` and `qa-general-display-general-full-
  27343240965400.png` show Window and Full state; DEV `status.display.mode`
  changed **Borderless=1 → Windowed=0 → Borderless=1**. Captured
  `qa-general-display-resolution-popup-27344475369750.png` shows the embedded
  native Display panel with scrollbar and header **5–14 / 29** and modes
  including 1920x1080 / 1366x768; capture labels and command timestamps are
  generated by the DevBridge. A prior quick smoke clicked during a legitimate
  panel-opening fade and was disregarded; the final test waits for that fade.

No commercial EXE, GPK or screenshots were copied into tracked source. No
complete profile-empty playthrough to first ending was run, and no commit,
push or original user saves were modified. The UI smoke uses isolated
temporary directories; parity across all possible hardware configurations
is not claimed.

## Final Release verification

- `cmake --build build/school-days-dev-control2 --config Release --parallel 4`
  **PASS**; full `ctest --test-dir build/school-days-dev-control2 -C Release
  --output-on-failure --parallel 4` **57/57 PASS in 19.31 s**.
- Repeated same suite with `--parallel 1`: **57/57 PASS in 60.55 s**.
- `cmake --build build/school-days-dev-control4 --config Release --target
  SchoolDays --parallel 4` **PASS**, updating user-facing executable
  `build/school-days-dev-control4/games/school-days/Release/SchoolDays.exe`.
- Targeted GUI smoke **repeated on control4**, not only the control2 test
  build, with an additional fresh isolated `--documents`/`--config` profile
  (`qa-general-display-interactive-20261105-control4-final`): `General WINDOW`
  produced `display.mode=0`, `General FULL` produced `display.mode=1`, and the
  native `Display Options` tab plus resolution popup both opened; exit code 0.
  Captures `qa-general-display-general-window-27700183134950.png`,
  `qa-general-display-general-full-27700788537950.png`,
  `qa-general-display-display-tab-27701436246500.png`, and
  `qa-general-display-resolution-popup-27702057960500.png` are under the
  existing ignored QA build directory.
- The first complete CTest pass following the third-tab change had one failure
  (56/57): the **old** `SchoolDaysSaveTest` expected the removed SOMCON tab to
  open generic `EngineSettings`. Its assertion was updated to the requested
  embedded native Display panel; focused `SchoolDays.Save` then passed and
  both final 57/57 full test runs passed.
- `git diff --check -- games/school-days` returned 0; Windows LF→CRLF notices
  are not whitespace errors. No benchmark of every audio PCM output or actual
  physical switch across all 29 driver modes was performed, so tests support
  the implemented behavior rather than an unqualified pixel-/hardware-perfect
  claim for all system configurations.
