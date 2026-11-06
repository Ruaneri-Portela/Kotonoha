# School Days HQ v1.02 — Menu SFX and Route Map hover parity (2026-11-05)

## Scope and observable behavior

The retail game plays `SysSe/NewSys/SeClick_01` for the sampled mouse activations
in the title menu, LOAD, Route Map, OPTION, Replay, and the gameplay system bar.
In the prior Kotonoha build, some actions played a click immediately and a
second `SeView` when the destination panel opened. Other close actions used
`SeCancel` or were silent. In the retail Route Map, the active episode remains
highlighted while **only the tab under the pointer** gains a blue outline;
the outline disappears on pointer exit.

The changes stay within `games/school-days`:

- `SchoolDaysRuntime.cpp/.hpp`: click sound is emitted at activation; the
  subsequent system-panel/Route Map/configuration handoff suppresses the
  redundant `SeView`. Mouse activation of EXIT and TOPMENU uses the same click
  as the retail popup. BackLog close also uses click. A `Sfx:` event is recorded
  in the optional DEV trace for direct confirmation of cue selection.
- `SchoolDaysSystemUi.cpp`: Route Map controls and close/selection actions in
  Save/Load, Replay, Config and Exit now request the authenticated click cue.
- `SchoolDaysRouteMapRenderPlan.cpp/.hpp`: among common chip records 0–5,
  only the selected (pointer-hovered) episode's blue outline is drawn. The
  active-episode artwork, records 6–10, nodes, and navigation overlays keep
  their prior draw order.

## Evidence from the original and the Kotonoha build

The recordings were captured locally from both processes using Windows audio
loopback. The first original-game capture was silent because that application's
audio output had been muted. **It is excluded from the comparison.** The user
unmuted the original before the recordings below.

| Recorded sequence | Retail original | Kotonoha v3 | Result |
| --- | --- | --- | --- |
| Route Map: episode tabs, close, then LOAD close | `SeClick_01` across all observed clicks | Same cue | Matches sampled actions |
| Title → OPTION, Sound tab, General tab | `SeClick_01` | Same cue | Matches sampled actions |
| Gameplay: SAVE, LOAD, BACKLOG and OPTION open/close | `SeClick_01` for eight actions | One `SeClick_01` per action | Matches sampled actions |
| Gameplay: TOPMENU popup and its NO action | `SeClick_01` for both | One `SeClick_01` per action | Matches sampled actions |
| Route Map pointer across episode tabs 1, 2, 3 and neutral area | Blue outline follows the pointer and clears in the neutral area | Same visible result in matched screenshots | Matches sampled states |

Audio verification uses `build/school-days-release-audit/classify_menu_audio.py`
to correlate each captured waveform against locally extracted original SFX.
In the seven title/menu clicks, correlation to `SeClick_01` was approximately
0.95–0.99 in the original and 0.82–0.93 in Kotonoha. In the ten gameplay
actions, it was 0.70–1.00 in the original and 0.84–0.94 in Kotonoha. The other
tested cues (`SeClick_02`, `SeView`, `SeCancel_01` and `SeSelect`) did not show
a comparable match on those samples. The Kotonoha DEV trace recorded seven
click cues for seven title/menu actions and ten click cues for ten gameplay
actions. These measurements support the absence of a complete, extra `SeView`
sample in the recorded actions; they cannot prove that no other untested path
plays one.

Evidence files remain local and outside source control:

- `build/school-days-release-audit/original-audio/` contains the SFX references,
  original captures and click timestamps, including
  `original-gameplay-panels.wav` / `.json` and
  `original-menu-unmuted-samsung.wav`.
- `build/school-days-release-audit/kotonoha-audio/` contains
  `kotonoha-gameplay-panels.wav` / `.json` and
  `kotonoha-menu-samsung.wav` / `summary.json`.
- `build/school-days-release-audit/menu-hover-original/` and
  `menu-hover-kotonoha/` contain screenshots for the pointer states.

Original game audio and image assets are deliberately **not distributed** with
the Kotonoha source or the public build.

## Validation

On 2026-11-05, `cmake --build build/school-days-dev-control4 --config Release
--target SchoolDays --parallel 4` succeeded. After rebuilding
`build/school-days-dev-control2` in Release with a serial build to avoid DLL
copy contention, `ctest --test-dir build/school-days-dev-control2 -C Release
--output-on-failure` passed **57/57**. The DEV control4 configuration has
`BUILD_TESTING=OFF`, so it is not the location of the test suite.

The evidence covers the listed menu interactions and Route Map pointer states,
not all sound cues, routes, episode screens, or audio transitions in the game.
No inference about an unobserved retail cue is used to change other sound
paths. The native Route Map interaction still requires pointer hover before
activation, as already implemented by the CMAP selection model.
The hover screenshots cover episode tabs 1–3 and a neutral pointer position on
the completed profile. They do not establish the behavior for other tabs,
alternate-host mode, leaving the game window, or leaving the pointer stationary
after a page change; those cases need separate retail captures.
