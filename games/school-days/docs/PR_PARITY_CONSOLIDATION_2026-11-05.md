# School Days HQ v1.02 → Kotonoha v3: consolidated parity follow-up

This document accompanies the follow-up to upstream
[`Ruaneri-Portela/Kotonoha#8`](https://github.com/Ruaneri-Portela/Kotonoha/pull/8)
(*retail EXIT/title transitions*). The implementation changes since that PR
are wider than the options artwork: media, gameplay, replay, saves, front-end,
audio, generalized engine APIs, Windows and Android integration, and tests.

## Scope and evidence

The commercial PC game remains the behavior reference. The following code
aims to reproduce observed behavior, while the newer Display Options is an
**explicitly requested modernization**, not a retail feature. Evidence and
limitations are recorded in the linked game documents, notably
[`PARITY_TEN_ITEMS_2026-11-05.md`](PARITY_TEN_ITEMS_2026-11-05.md),
[`RETAIL_SPEED_GUI_INTERNAL_AUDIO_2026-11-05.md`](RETAIL_SPEED_GUI_INTERNAL_AUDIO_2026-11-05.md),
[`CHOICE_RESULT_METER_SFX_RETAIL_2026-11-05.md`](CHOICE_RESULT_METER_SFX_RETAIL_2026-11-05.md),
[`LOAD_AND_GLOBAL_PROGRESS_2026-11-05.md`](LOAD_AND_GLOBAL_PROGRESS_2026-11-05.md),
[`DISPLAY_OPTIONS_USER_ART_2026-11-05.md`](DISPLAY_OPTIONS_USER_ART_2026-11-05.md)
and [`GENERAL_SOUND_EXTERNAL_ART_2026-11-05.md`](GENERAL_SOUND_EXTERNAL_ART_2026-11-05.md).

### Game-layer improvements

1. **KTRF/ORS, ABC and packed media:** game-owned routing and asset resolution;
   safer media prep/adoption, 24-fps scene event clocks, video transitions,
   raw GPK streaming (seekable where possible), ABC voice/mouth overlays,
   audio event dispatch, logo/title/episode-telop handling.
2. **Choice behavior:** original prompt/tail timing, input and skip gates,
   SetSELECT effective 1x speed override without overwriting the user's
   selected speed, immediate `SeSelect`, feelings meter-only exit and distinct
   subsequent `SeUp`/`SeDown` on feelings changes.
3. **Playback/MenuBar:** visual speed labels x1/x2/x4/x16/x32 with original
   internal rates 1/2/4/12/24; narrative audio gate above 4; persistent
   speed chip independent of hover; AUTO ON's 13-frame moving light, frozen
   during pause; forward/back/skip transitions with authenticated timing.
4. **Transitions and layered composition:** scene/fade boundaries at 24 fps,
   integer-tick and half-open ORS fade intervals, authored fades behind
   subtitles, choice-result scene exit and black transport envelopes.
5. **Frontend and native window close:** title/config/LOAD/REPLAY handoffs,
   preserved original panel transitions, `SYSBASE` EXIT/TOPMENU fades and
   `WM_CLOSE` routed to `End this game?` confirmation when applicable.
6. **Replay/PlayData:** isolated H-scene gallery playback, Replay Data loading
   card before fresh-A00 playback, no pollution of live narrative state,
   Replay indicator independent of hover, save/route-map constraints.
7. **Persistence and progress:** `Config.DAT`, route metadata, Save/Load
   slot/comment presentation, ending/global progress semantics, controlled
   rollback/save/load and test-profile isolation.
8. **General and Sound:** configured levels, MenVoice, Mute, skip/subtitles,
   persistence; General FULL is native-desktop borderless, WINDOW a decorated
   maximized window; STANDARD+FULL presents the complete Novel picture at
   75% height with inverse hit-testing and black top/bottom bars; WIDE restores
   full-height presentation.
9. **Custom Windows Display Options:** 22 click regions, four CMAP geometries,
   pending/draft Apply/Default, monitor resolutions and refresh, Resizable,
   VSync, backend, shader preset, custom GLSL path and factor 1.0..4.0 by 0.5.
   This replaces obsolete SOMCON **only for the Windows School Days game**;
   the generic Kotonoha SettingsMenu still exists and is shared by other
   games. Its Win32-aware mode catalog and scrollable selector remain generic.
   Android retains its existing native FPS/render-scale Display controls and
   uses the original General CMAP/base/CHIP as artwork fallback for that
   logical page, since the custom Windows Display atlas is not bundled there.
   This fallback has a synthetic model/render regression test; it has not yet
   been validated on an Android device.
10. **Option artwork overlay:** user-authored General, Sound and Display
    PNGs load by logical asset name from a separate directory ahead of the
    retail GPKs, without editing `System.GPK`. The final Display CHIP fixes
    a second CLOSE outline by leaving the already complete base CLOSE intact.

### Shared engine and build dependencies

Generic `lib/` changes support seekable assets and custom FFmpeg AVIO,
MediaPlayer lifecycle/rates, audio routing, visual transitions and presentation,
render scale, physical/window display settings, image-alpha/subtitle ordering,
mode enumeration, UI selection and input mapping. School Days-specific names,
retail policy, GPK paths and scripts remain in `games/school-days`.

The Windows `SchoolDaysOptionAssets` CMake target copies authored external
option resources when they are provided, even when no C++ relink is needed.
Any explicitly selected `--asset-overlay` takes precedence over the default
`display-options` directory; include those custom Option files in the
explicit overlay if the custom panel should be available in that mode.

## Required external assets — NOT in this PR

**No commercial asset archives, EXE/DLLs, retail screenshots, copied imagery,
user save data or custom copyrighted art are included.** The game requires
the player's lawfully obtained original School Days HQ data (the retail set of
29 GPK archives) for actual gameplay. The custom options additionally require
locally supplied **user-edited** artwork under the following relative paths:

```text
<SchoolDays.exe directory>/display-options/System/Option/
  Option_Def.png              (800 x 450)
  Option_Def_Chip.png         (684 x 436)
  Option_Sound.png            (800 x 450)
  Option_Sound_Chip.png       (684 x 354)
  Option_Display.png          (800 x 450)
  Option_Display_Chip.png     (684 x 1212; corrected CLOSE atlas)
  Option_Display.cmap         (800 x 600)
  Option_Display_Wide.cmap    (800 x 450)
  Option_Display_Wide_Full.cmap (1280 x 720)
  Option_Display_Wide_Note.cmap (1024 x 576)
```

At build time, placing these in
`games/school-days/display-options/System/Option/` lets the CMake target
copy them next to the Windows executable. General/Sound deliberately keep
**their original retail CMAPs**. The authored Display CMAPs are generated
from the 22 regions described by
`display-options/display_options_controls.json`. Without the custom Display
PNG/CMAP files, the new third tab cannot render correctly. This is an
**explicit runtime prerequisite**, not a missing retail file to fabricate.

## Validation and unverified cases

On the local Windows Release branch **before merging later upstream changes**:

- Full `control2` build: PASS in serial mode; full `ctest -C Release --parallel 4`:
  **59/59 PASS**, 17.80s. A fully parallel rebuild initially encountered
  an intermittent `vcpkg z-applocal` file-in-use race, so serial build was
  used to validate compilation.
- `control4` SchoolDays Release build: PASS; isolated Windows/OpenGL GUI
  smoke with custom General, Sound and Display arts: PASS.
- After replacing `Option_Display_Chip.png` with the final user version,
  screenshot comparison `qa-close-fix-*-4492*.png` confirmed the corrected
  single-outline CLOSE in normal/hover states. Those captures are in the
  ignored `build/` QA folder, not committed.
- `git diff --check`: PASS.
- Original retail `System.GPK` hash remained unchanged.

The full clean-profile run through the first ending was **not authorized or
executed**. Special H-scene SceneCheck entries 11/22/30, exact H-scene fade-IN
and certain loading-card timings, arbitrary custom GLSL programs, exhaustive
PCM/UI combinations, all physical monitor modes and complete Android APK
validation remain outside the authenticated test coverage. H-scene ordinary
OUT 1000ms is based on original binary evidence; IN 1000ms remains a visual
approximation. See each detailed audit for the proven vs estimated boundary.

### Integrated upstream follow-up (source-only QA)

After integration with upstream `b34266b`, a separate Windows Release QA build
compiled successfully. The full School Days-focused CTest selection passed
**35/35** (parallel 4), including the Android no-custom-artwork fallback test;
this is not a device/APK test. The self-contained engine media tests initially
exposed a scheduling-sensitive missing first video frame: unlike the old
`MediaPlayer::update()` contract, the new audio/video thread could run only
after an immediate render update. The engine now bootstraps a bounded initial
buffer under the decoder mutex when necessary, before the first frame upload.
The targeted generated-media and School Days audio/video tests passed **7/7**
after that correction. These QA runs used a local noncommitted UI font copied
only into ignored build/test output directories.

The generic entire-suite result is **not** asserted green. Existing upstream
iOS Directory path expectations conflict with upstream storage implementation;
example Demo/MCP tests depend on an absent splash fixture and Windows test
locale/symlink permissions. None warrants bundling proprietary art or silently
changing iOS save paths in this School Days parity PR.
