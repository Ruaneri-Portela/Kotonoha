# School Days HQ / Kotonoha v3 — final General/Display correction (2026-11-05)

## Historical intermediate contract (partially superseded later on 2026-11-05)

**Final update:** The subsequent custom user-art `Display Options` request overrides item 1 ONLY: on Windows, the third tab now uses a School Days-only Display panel instead of opening SettingsMenu directly. The global SettingsMenu and resolution catalog still exist for other games; General WINDOW/FULL/STANDARD/WIDE remains as described below. See `DISPLAY_OPTIONS_USER_ART_2026-11-05.md`.

## User's earlier corrected requirements (historical)

1. The old retail third-tab SOMCON region must continue to **open Kotonoha's
   global SettingsMenu**. Games share that engine feature; a game may specialize
   its options, but School Days must not replace the shared component with
   another School Days-only resolution page. Preserve SOMCON original art.
2. Expand the **global** SettingsMenu resolution chooser to every mode the
   active Windows monitor driver advertises, including supported refresh rates.
   No 960/1280/1600/1920 fixed shortlist. Selector must navigate the whole
   list, not silently hide higher/lower modes.
3. General `FULL` = **desktop-native borderless fullscreen**, never exclusive.
   General `WINDOW` = native Windows window with title bar Minimize, Maximize,
   Close accessible and game sized to the display's available area. Using a
   desktop-sized *client* without maximizing put the caption above the screen
   on this machine; Windows maximization fixes that.
4. General `STANDARD (4:3)` in FULL = vertically compress the authored game
   image into a centered 75%-height picture with black bands above/below, as
   in the supplied original screenshot (virtual 800x600 host, 800x450 content
   at y=75). General `WIDE (16:9)` restores full height. Do not crop the video
   or subtitles, change the monitor display mode, or deform unrelated games.
5. Keep the existing persistence and semantics of Sound/General buttons. No
   commercial original executable run, real user saves, commits or pushes.

## Code boundaries and implementation

- Engine-wide: `lib/include/Kotonoha/components/DisplayModeCatalog.hpp` and
  `lib/src/components/DisplayModeCatalog.cpp` find modes from the selected
  SDL display ID's Windows `HMONITOR`. On Windows, use the driver
  `EnumDisplaySettingsExW` 32bpp table as authority; normalize NTSC 29/59/119
  Hz values to 29.97/59.94/119.88 as SDL does, preserve genuine 60 separately,
  dedupe by width/height/Hz and order deterministically. Use SDL fallback for
  non-Windows or unavailable Win32 monitor properties. The shared
  `SettingsMenu` now opens a scrollable SelectBox, showing resolution *and*
  refresh; the shared SelectBox handles wheel/swipe/scrollbar/Home/End/PgUp/PgDn
  and visible total. This also benefits all other Kotonoha games.
- The native School Days third tab calls `closePanels()` and
  `engine->openSettings()` on Windows, and its custom `renderDisplayPanel` no
  longer overpaints `SOMCON Options`. Android retains its own mobile refresh
  and render-scale page.
- School Days General `SelectGeneralWindowSize` sets FULL Borderless or
  WINDOW Windowed, both at active desktop resolution/frequency, never Exclusive.
  WINDOW restores SDL window borders/resizability and maximizes the Windows
  window, so its visible caption is not cut off by a desktop-sized client.
  The screen client is smaller in height when the Windows taskbar/caption
  consumes some of the physical desktop, as normal for maximized Windows apps.
- A generic no-op-by-default `Runtime::narrativePresentationHeightScale()`
  allows School Days only to request 0.75 in Standard+Full. `Engine::Iterate`
  composites the complete owned Novel texture into the centered inner 75%,
  not a clipped rectangle. Mouse coordinates and initial choice-hover seed
  are inverse-transformed to keep interactions aligned. WIDE and all other
  games default to 1.0.
- `SchoolDaysSystemUi` changes the FULL/Standard Config panel's host viewport
  to cover the full physical width/height non-uniformly, reflecting the
  original 800x600/800x450 layout with empty black bands. Screen CMAP input
  uses that same transform. This applies only to the Config panel in
  FULL+STANDARD; generic SettingsMenu and other native UI remain independent.
- The engine's native mouse-capture path now prevents the UP edge of the same
  press that opens generic Settings from activating a different generic tab
  beneath it. The test had revealed unintended `VN text` tab activation when
  clicking the original SOMCON region; the final popup opens on `Display`.

## Observed validation (isolated QA, no commercial GUI)

The tested monitor's Win32 mode table returned **37 native DEVMODE entries / 29
distinct width×height×refresh tuples across 11 resolutions**; all native
entries were accepted by `ChangeDisplaySettingsExW(CDS_TEST)` without changing
the desktop. Generic catalog tests cover duplication, fraction refresh rates,
mode selection and keyboard navigation.

With the current development executable, `qa_general_corrected_final.py`
against its own `--documents`, `--config` and DEV port produced:

- FULL: engine mode 1 (borderless), real HWND without WS_CAPTION, window/client
  dimensions 1366×768, desktop rect (0,0,1366,768).
- WINDOW: engine mode 0 (windowed), real HWND with WS_CAPTION + maximized
  style, physical rect (-8,-8,1374,746), client 1366×715 — title bar visible
  and inside work area. These values apply to the tested 1366×768 desktop;
  the implementation uses the user's selected monitor, not fixed 1366×768.
- STANDARD+FULL Config render shows original-style black top/bottom bands,
  the entire panel across the width, and selected Standard chip. Clicking
  WIDE from that exact stretched CMAP restores full-height Wide art; selecting
  Standard again restores bands. The game story raster itself uses the same
  vertical 75% compression.
- The old native third tab displays literally `SOMCON Options` in a fresh
  screenshot. Clicking it opens the generic `Settings` with `Display` page;
  the modal resolution chooser shows 29 modes, with screenshots showing
  its middle selection (9/29), bottom using End (29/29) and top using Home
  (1/29). Representative captures under ignored
  `build/school-days-parity-2026-11-05/` include:
  `qa-global-retail-general-somcon-29580551969250.png`,
  `qa-global-global-resolutions-picker-29642096600650.png`,
  `qa-global-global-resolutions-end-29642669276550.png`,
  `qa-global-global-resolutions-home-29643236598600.png`,
  `qa-corrected-general-full-standard-29711457386200.png`,
  `qa-corrected-general-full-wide-restored-29712204445800.png`.

No claim of pixel-perfect comparison against all original scenes or actual
physical application of every resolution/refresh combination is made. The
older `GENERAL_SOUND_DISPLAY_SYNC_2026-11-05.md` is retained solely as an
intermediate-state audit and superseded where requirements changed.

## Final Release integration and regression (same working tree)

- Full multi-target `cmake --build build/school-days-dev-control2 --config
  Release --parallel 4`: **PASS**. The first combined CTest found one failure
  in `SchoolDaysRuntimeConfigStorageTest`, reproduced twice in isolation:
  status `0xc0000409` immediately after Windows-specific maximization was
  attempted under a dummy/offscreen SDL test driver. The OS-only calls
  `SDL_SetWindowBordered` and `SDL_MaximizeWindow` are now gated on the actual
  SDL video driver name `windows`. The same isolated persistence test passed
  after the correction, followed by full `ctest --parallel 4`: **58/58 PASS**
  (15.78 seconds) **and serial `ctest --parallel 1`: 58/58 PASS** (60.88
  seconds). Headless regression tests retain their physical settings
  transitions, without invoking a nonexistent Windows caption.
- `cmake --build build/school-days-dev-control4 --config Release --target
  SchoolDays --parallel 4`: **PASS**; produced
  `D:\Dev\Kotonoha-v3\build\school-days-dev-control4\games\school-days\Release\SchoolDays.exe`
  (2026-11-05 18:29:50 local; SHA-256
  `802A531FFBA9E5BE6F1DF9FFDF3E4C9B1BA5252B854960A4FD99A0D4F8C8FA75`).
- Final control4 visual smoke with untouched original asset packs and *fresh
  isolated QA documents/configs*: `qa_global_settings_resolutions.py` and
  `qa_general_corrected_final.py` both exited **0**. Main panel shows
  `SOMCON Options`; clicking opens shared Settings on **Display** (no
  erroneous VN text tab activation). Resolution picker shows all **29**
  modes and scrolls 1/29→29/29. FULL real HWND is borderless at 1366×768;
  WINDOW HWND is decorated+maximized with visible caption and native client
  1366×715. In Full, selecting Standard renders the retail 75%-height panel,
  choosing Wide restores full-height, and selecting Standard again restores
  the black bands. Importantly, closing the Standard Config panel reveals
  actual **gameplay video also compressed in the center**, verified visually
  at `qa-corrected-gameplay-full-standard-30385875544600.png`; reopening
  Options retains Standard. All captures remain under the ignored
  `build/school-days-parity-2026-11-05/` QA directory.
- `git diff --check` returned 0 (Git only warned about automatic LF→CRLF
  conversion on some source files). No commit, push, original executable
  launch, player-save access or user config mutation was performed.
