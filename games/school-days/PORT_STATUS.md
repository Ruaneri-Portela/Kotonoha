# School Days HQ on Kotonoha

> **2026-11-05 consolidated update:** The historical sections below include
> older behavior descriptions. The current Windows third tab is the custom
> **Display Options** (Resolution/Hz, Resizable, VSync, upscaler, shader,
> external GLSL, factor and Apply/Default), not the obsolete SOMCON screen or
> a direct shortcut to the generic SettingsMenu. It uses external user-authored
> PNGs and four non-retail CMAP variants; General retains WINDOW/FULL and
> STANDARD/WIDE. General/Sound may load external PNGs while keeping their retail
> CMAPs. Required filenames and installation are in
> [`display-options/README.md`](display-options/README.md) and
> [`docs/DISPLAY_OPTIONS_USER_ART_2026-11-05.md`](docs/DISPLAY_OPTIONS_USER_ART_2026-11-05.md).
> The current GPK media path supports seekable streaming for **raw** packed
> entries with fallback for compressed/in-memory entries; it is no longer
> accurate to say every packed media entry is always materialized in memory.
> See the later `docs/PARITY_TEN_ITEMS_2026-11-05.md` and its linked evidence
> for gameplay, audio, fades, replay and settings improvements. No full
> clean-profile playthrough is claimed.

`SchoolDays` extends the Kotonoha `Runtime` rather than embedding the original
game engine. It uses Kotonoha's renderer, audio mixer, media decoders, settings,
launcher and debug APIs. Its UI is drawn natively from the retail System assets; the engine's debug
overlay remains independent.

## Running

Supply your own data; no commercial assets are included or copied:

```sh
SchoolDays --launcher
SchoolDays --school-days /path/to/SchoolDaysHQ
SchoolDays --school-days /path/to/SchoolDaysHQ/Packs
```

The retail path requires the original validated 29-pack GPK set. Extracted data
must have `Script/English/<directory>/<scene>.ENG.ORS` and its normal media
directories. Component-wise case-insensitive lookup rejects ambiguous names.
`--scripts /path/to/translation` retains the original loose `.txt` script override.

`SCHOOL_DAYS_DIR`, the remembered launcher path, `preferredDataDirectory` and
packaged resources provide discovery, in that order. The bundled
`school-days-hq.ktnroute` is routing metadata, not media. `--ktrf path` selects
another compatible original-profile binary.

The launcher has separate game-folder and KTRF-route fields with native
pickers. Both must be valid before Play; the selected route is remembered next
to the engine settings and reused without the launcher. An explicit `--ktrf`
takes precedence over the remembered route, which takes precedence over the
bundled route. `--no-config` disables remembering either path.

Saves and GlobalFlag use `preferredDocumentsDirectory("school-days")`; use
`--documents directory` for isolated tests. Quick save/load and slots 0–99 use
the original FlgH/SLog codecs, restarting the saved script as the original did.
The game-owned SaveLoad UI includes the retail asynchronous comment editor and
preserves existing comment text/metadata while editing. Existing compatible SLog
files can also be loaded through the engine save/load API.

Space pauses; Left navigates the original semantic Back rules; Right skips to
the original choice/end boundary; F5/F6 quick-save/load. Retail choices are
mouse-only: pointer motion updates the stored selection and a left-button-down
activates only the currently stored hit region. Keyboard, touch and gamepad do
not select a choice. The on-screen controls remain available. Escape opens game
configuration. Engine video/upscaling remains available through the launcher.

## Preserved

- Original KTRF full-core reader, typed reader, routing interpreter and profile
  validation; 1,857 nodes, 1,855 script resources and all original choice effects.
- Original StateStore, inverse Back rules, transactional scene handoffs, history,
  ending registration, checkpoints, persistent GlobalFlag and native save codecs.
- Original 24 Hz ORS parser/scheduler, timed choices and timeout results.
- Still backgrounds, timed WMV movies/end rolls, voice, SE slots, BGM intro/loop
  and ending tracks, subtitle text and black/white fades through Kotonoha media.
- Original title CMAP/model/render plan and title/end-background configuration
  when the corresponding retail resources exist; title movie playback uses the
  authenticated 24 Hz clock/fallback behavior and retains its terminal frame.
- Replay frontend playback and isolated Replay Data/PlayData sessions are wired
  through the original routing/save data without replacing the live narrative
  state. Replay ownership and ordinary gameplay state remain separated.
- Episode telops use the game-owned authenticated timing/alpha model on natural
  episode handoff. The MenuBar and TopMeter use retail assets/state, including
  `001`/`002` refresh behavior and the authenticated playback-speed controls.
- Retail `SetSELECT` interaction uses the authenticated mouse-only prompt policy:
  full-canvas one-choice hit testing, top/bottom two-choice geometry, stored
  selection/stale-click rules, timeout at `endTick-1`, AUTO `rand%(span+1)-1`,
  immediate semantic commit, and 24/48-tick presentation tails with the final
  alpha-zero frame rendered before scene advance.
- `Config.DAT` is loaded/cached/flushed through the game-owned retail config
  storage, preserving unknown/malformed material where the storage policy can do
  so. MenuBar Auto state is persisted through that path.
- SaveLoad comments are edited asynchronously inside the retail SystemUi flow;
  Escape cancels the editor without accidentally closing the owning SaveLoad or
  pending handoff flow.
- Recorded route checkpoint loading and completion statistics using the original
  route-map model/oracle. Chapter/ending handoffs offer save/continue.
- `debugNode`, node enumeration/jumps, original binary `loadKtrf` and byte-exact
  `dumpKtrf`. Reloading a route retains loaded GlobalFlag state.

## Not yet equivalent to retail

- Character ABC/lip-sync presentation is implemented and was observed with a
  complete ABC group in `00/00-00-A02`; full visual parity remains unverified.
- Runtime display-variant coverage is not yet retail-equivalent; the currently
  exercised WideFull path must not be read as proof for every original display
  variant or runtime variant switch.
- Frontend transition/carry-frame choreography is not yet exact across all title,
  Replay, Replay Data and gameplay transitions.
- PlayData/Replay Data exposes nested Save/Load/BackLog through an isolated
  temporary session. The RouteMap handoff from nested Replay Data LOAD remains
  inhibited pending retail verification.
- Replay SystemBGM ownership/restoration is not yet verified as retail-equivalent.
- Exact original glyph rasterization/styling is not claimed. Choice and other
  game-owned text use the authenticated geometry/advance plans with Kotonoha TTF
  typography, so glyph shape/outline/shadow can still differ from retail pixels.
- GPK media entries are read into memory through Kotonoha's AssetProvider;
  extracted media streams from disk. Very large packed movies may require more
  memory than the original streaming bridge.
- Missing optional media reports a status/log warning without stopping routing.
  Invalid/missing ORS or unresolved routing is not silently treated as success.

Current v3 validation has also been run against the original retail 29-GPK data
set. Save/routing comparisons were kept isolated with separate baseline,
A-full, B-global and current-save document/save clones so one validation path
could not silently contaminate another. Synthetic regression fixtures remain in
the automated suite for deterministic parser/media/unit coverage; they are not
the only evidence used for the current School Days parity work.

This status is a record of verified behavior, not a claim of 100% retail parity.
The current release-readiness verdict and runtime evidence are in
[`docs/RELEASE_READINESS_AUDIT.md`](docs/RELEASE_READINESS_AUDIT.md).
School Days preserves the Overflow logo's original image margins and aspect
ratio. Its lettering remains centered rather than being cropped and enlarged
to fill the viewport. A synthetic pixel regression checks the center and the
black margins.


Regression fixtures are synthetic scripts and newly generated PNG/WMV/OGG media,
never copied game assets. The media test asserts a decoded red WMV frame and
active BGM/Voice/Movie audio streams over a decoded blue PNG background.

## Build and tests

```sh
cmake -S games/school-days -B build/school-days-port -G Ninja -DBUILD_TESTING=ON
cmake --build build/school-days-port --target SchoolDays SchoolDaysPortTest \
  SchoolDaysSessionTest SchoolDaysBridgeTest SchoolDaysSceneTimelineTest SchoolDaysMediaTest
ctest --test-dir build/school-days-port -R '^SchoolDays' --output-on-failure
```

Tests cover asset discovery/traversal, original binary compatibility, all node
enumeration, transactional routing/save/load/Back/handoffs, ORS tick scheduling,
Config.DAT storage, Replay/PlayData isolation, MenuBar/TopMeter policy, title/telop
helpers, SaveLoad handoff/comment behavior, retail prompt interaction and choice
text/tail timing, plus headless Kotonoha engine/frontend and media presentation.
The custom-runtime test also edits a binary NODE label, rebuilds its integrity
envelope, reloads it, jumps to a numeric node and verifies byte-exact dumping.

Shared engine sources required (no new dependencies beyond existing SDL3):

```
src/parsers/Ktrf.c
src/parsers/KtrfTyped.c
src/parsers/Ors.c
src/routing/KtrfRuntime.c
src/routing/KtrfRouter.c
src/routing/KtrfAdapter.c
```

Public headers are under `Kotonoha/parsers`, `Kotonoha/routing` and
`Kotonoha/SceneTime.h`; `KtrfRouterInternal.h` stays private. Game-only dependency:
`ZLIB::ZLIB`.
Shared root/lib CMake and platform projects are intentionally not edited by this
port.

## Gameplay and save corrections (2026-11-03)

- Skip and Right share one transition path: reaching `Next` commits the scene
  with Auto disabled. Repeated activation no longer rewinds the last second
  before `SkipFRAME`; live choices remain interactive and block bypassing.
  Replaying a visited scene reuses its existing SP checkpoint instead of
  rejecting the transition as an unsupported duplicate. The regression exercises
  both Right and the visible button after returning to the same scene.
- Super Skip now invokes the existing KTRF next-choice bridge across scripts,
  respecting unread-scene/configuration gates. Runtime ORS pointers are rebound
  after bridge changes, including failure paths, before rendering resumes.
- `Subtitles` extends `Kotonoha::SubtitleScene` with exclusive dialogue cues.
  The next line truncates the prior cue; equal-time cues keep the final line.
  Generic ASS timelines retain overlapping subtitle support.
- Chapter/ending callback handoffs open the original `SaveFeel` screen. Its
  entry animation, 30-second countdown and bars use the original model and
  current script values `001`/`002`. Save opens the slot menu; successful save
  or Skip acknowledges the pending bridge handoff. Esc also continues.
- Save names use `Kotonoha::TextInputDialog`, including cancellation and UTF-16
  comments in retail slot metadata. The request is enabled even when the retail
  configuration disabled comment input. No OS dialog is involved.
- The custom film runtime forwards update, render and input to registered
  engine dialogs, including those above its own SaveLoad panel. The fallback
  save menu asks for a name even without a suggested title.
- `SchoolDaysMediaTest` covers named-save cancellation/write/reload and Skip
  without Auto. Optional `SchoolDays.Save` uses the original packs with an
  isolated temporary documents directory, checks retail save/reload, and
  exercises an authenticated ending's feeling-screen handoff. No test writes
  into the user's retail installation.

## Subtitle escapes, settings and media diagnostics

- The School Days `SubtitleScene` extension decodes ORS `\n`/`\N`, `\r\n`
  and `\t` only at presentation time. Translation lookup retains the original
  source string. The generic subtitle serializer continues to handle plain text.
- The obsolete SOMCON tab is covered and relabeled **Kotonoha**. Its original
  hit region now opens the engine's settings component rather than a duplicate
  page of the original menu. Native BGM/SE/Voice controls remain on Sound.
- Original wide/non-wide buttons now select 16:9 or centered 4:3 media areas;
  the setting persists in `SchoolDaysConfig.ini` as `Wide`. The menu/input
  canvas retains its 1280x720 design space. Video and still media use the same
  selected content area. Retail hit-map tests exercise both aspect buttons and
  opening the engine settings from the third tab.
- Both shared FFmpeg decode paths provide `pkt_timebase` from the source stream,
  enabling the decoder to adjust timestamps when Vorbis discards padding samples.
  CRI ADX/AHX duration comes from the exact header sample count and rate before
  stream probing; the bitrate-duration estimate is no longer needed.
- The specific WMV3 INFO message for eight trailing zero bits is routed to the
  engine's debug log. No FFmpeg warning/error level is globally disabled;
  nonzero trailing data and decoding errors keep their normal diagnostics.
  A separate `Packet corrupt` warning still occurs at the tail of one Amagami
  ADX during the integration check; PCM sample-count/signal checks pass, and
  this warning remains visible for further investigation.

References: FFmpeg's [discarded-sample timestamp handling](https://www.ffmpeg.org/doxygen/9.0/decode_8c_source.html),
[WMV3 extradata reporting](https://github.com/FFmpeg/FFmpeg/blob/master/libavcodec/vc1dec.c),
and [duration estimation](https://github.com/FFmpeg/FFmpeg/blob/master/libavformat/demux.c).
The local Ollama `gemma4:26b` supplied an advisory review; implementation choices
were checked against the sources and original game data.
