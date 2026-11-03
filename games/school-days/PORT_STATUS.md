# School Days HQ on Kotonoha

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
`--asset-overlay /path/to/translation` uses the shared engine overlay.

`SCHOOL_DAYS_DIR`, the remembered launcher path, `preferredDataDirectory` and
packaged resources provide discovery, in that order. The bundled
`school-days-hq.ktnroute` is routing metadata, not media. `--ktrf path` selects
another compatible original-profile binary.

The launcher has separate game-folder and KTRF-route fields with native
pickers. Both must be valid before Play; the selected route is remembered next
to the engine settings and reused without the launcher. An explicit `--ktrf`
takes precedence over the remembered route, which takes precedence over the
bundled route. `--no-config` disables remembering either path.

Saves and GlobalFlag use `preferredDocumentsDirectory()`; use
`--documents directory` for isolated tests. Quick save/load and slots 0–99 use
the original FlgH/SLog codecs, restarting the saved script as the original did.
Slot filenames and the generic UI differ from retail; retail save thumbnails,
comments and its metadata browser are not implemented. Existing compatible SLog
files can be loaded through the engine save/load API.

Space pauses; Left navigates the original semantic Back rules; Right skips to
the original choice/end boundary; 1–9 select choices; F5/F9 quick-save/load.
The engine may reserve F9 for its debugger. The on-screen controls remain
available. Escape opens game audio configuration. Engine video/upscaling and
persistent settings are available through the launcher/engine settings.

## Preserved

- Original KTRF full-core reader, typed reader, routing interpreter and profile
  validation; 1,857 nodes, 1,855 script resources and all original choice effects.
- Original StateStore, inverse Back rules, transactional scene handoffs, history,
  ending registration, checkpoints, persistent GlobalFlag and native save codecs.
- Original 24 Hz ORS parser/scheduler, timed choices and timeout results.
- Still backgrounds, timed WMV movies/end rolls, voice, SE slots, BGM intro/loop
  and ending tracks, subtitle text and black/white fades through Kotonoha media.
- Original title CMAP/model/render plan and title/end-background configuration
  when the corresponding retail resources exist; a generic menu is the fallback.
- Recorded route checkpoint loading and completion statistics using the original
  route-map model/oracle. Chapter/ending handoffs offer save/continue.
- `debugNode`, node enumeration/jumps, original binary `loadKtrf` and byte-exact
  `dumpKtrf`. Reloading a route retains loaded GlobalFlag state.

## Not yet equivalent to retail

- Replay/gallery/PlayData playback, title movies, episode telops and retail
  transition choreography are not ported. The native title's replay action reports
  this rather than pretending to start it.
- Character ABC/lip-sync layers, MoveSom spatial transforms, exact ASS styling,
  original glyph rasterization, retail choice geometry/fade tails, top meters and
  complete retail configuration UI remain unsupported.
- Dialogue uses the shared libass subtitle component, with speaker styles and
  one active utterance at a time. Native glyph metrics and exact styling still
  need comparison. Timed choice behavior is retained; its layout is simplified.
- GPK media entries are read into memory through Kotonoha's AssetProvider;
  extracted media streams from disk. Very large packed movies may require more
  memory than the original streaming bridge.
- Missing optional media reports a status/log warning without stopping routing.
  Invalid/missing ORS or unresolved routing is not silently treated as success.

The supplied retail installation has now been exercised: all 29 GPK archives
open, 1,824 ORS resources parse into 86,414 events, and the native-resource
splash/title/config/load/route-map/exit/save/backlog panels were rendered locally.
These are captures of the port, not differential captures of Windows execution.
The resource audit identifies 32 logical OP1 routing resources without a matching
physical ORS file; their native dispatch is under investigation and they are not
silently skipped. Native save comment submission now uses a cancellable text
form instead of replacing the user's comment with an empty string.

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
enumeration, transactional routing/save/load/Back/handoffs, ORS tick scheduling
and headless Kotonoha engine/frontend and actual media presentation runs.
The custom-runtime test also edits a binary NODE label, rebuilds its integrity
envelope, reloads it, jumps to a numeric node and verifies byte-exact dumping.

Shared engine sources required (no new dependencies beyond existing SDL3):

```
src/parsers/Ktrf.cpp
src/parsers/KtrfTyped.cpp
src/parsers/Ors.cpp
src/routing/KtrfRuntime.cpp
src/routing/KtrfRouter.cpp
src/routing/KtrfAdapter.cpp
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
