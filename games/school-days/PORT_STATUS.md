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
- Subtitle content is shown in the accessible game control panel, not the
  original styled subtitle renderer. Timed choice behavior is retained but its
  visual layout is generic.
- GPK media entries are read into memory through Kotonoha's AssetProvider;
  extracted media streams from disk. Very large packed movies may require more
  memory than the original streaming bridge.
- Missing optional media reports a status/log warning without stopping routing.
  Invalid/missing ORS or unresolved routing is not silently treated as success.

No retail data was available for end-to-end verification in this checkout.
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
