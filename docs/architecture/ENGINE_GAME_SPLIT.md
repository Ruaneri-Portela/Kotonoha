# Engine / game boundary

## Goal

Kotonoha is the reusable engine. School Days HQ is one application built on top
of that engine. The two must be independently identifiable in the build so a
regression can be attributed either to engine infrastructure or to the game
profile/runtime.

The canonical CMake targets are:

```text
KotonohaEngine      static reusable engine library
        ^
        |
SchoolDaysHQ        game executable (SHARED callback library on Android)
```

On Android the CMake target remains named `SchoolDaysHQ`, but its output name is
temporarily `Kotonoha` because the existing SDLActivity Java bootstrap loads
`libKotonoha.so` by name.

## Source ownership

### Engine

Generic engine implementation stays under `src/` and is compiled into
`KotonohaEngine`. This includes the low-level renderer/media/audio primitives,
time and I/O infrastructure, generic KTRF parser/runtime/router, platform
helpers, upscaling, configuration primitives and other game-independent code.

`cmake/KotonohaTargets.cmake` enforces this boundary at configure time. A source
compiled into `KotonohaEngine` is rejected if it contains an explicit
`SchoolDays`/`schoolDays` implementation dependency.

### School Days HQ

Game-owned implementation lives under:

```text
games/school-days/src/
```

This directory owns the School Days routing/profile, KTRF adapter, persistence,
frontend/UI, GPK archive reader/mount, save/load/replay/route-map/menu behavior,
generated authenticated routing tables, and the SDL callback entry point.

The executable also owns a **profile-facing compatibility runtime** under
`games/school-days/src/runtime/` (`Gameplay`, ORS Events/Prompt/Image/Audio/Video
and their render adapters). These files originated as generic engine components
but now contain authenticated School Days behavior. They are deliberately kept
out of `KotonohaEngine` until their extension interfaces are generalized. This
prevents a reverse `engine -> game` link dependency while avoiding a risky
semantic rewrite during the structural split.

## Public context seam

`Kotonoha_Game` no longer exposes School Days-named state. The shared engine
context uses generic profile hooks:

- `menuVoiceEnabled`
- `messageLayerEnabled`
- `profileAssetsRoot`
- `textLogCaptureEnabled`
- `textLogUserdata`
- `textLogAppend`

School Days projects its native MenVoice/message/backlog behavior onto those
hooks from the application layer.

Scene timing is similarly generic: `Kotonoha_SceneTick` and its 24 Hz conversion
helpers live in `Kotonoha/SceneTime.h`. `SchoolDaysSceneTime.h` is retained only
as a compatibility include for older callers.

## Retail assets

Retail School Days content is application-owned. `SchoolDaysHQ` can mount the
29 v1.02 GPK files and expose their resources through the existing engine media
interfaces (`sdgpk://` + custom FFmpeg AVIO). GPK parsing, DFLT/zlib handling,
Script.GPK ORS resolution and game-specific resource naming are therefore not
part of `KotonohaEngine`.

## Dependency rule

The allowed direction is:

```text
SchoolDaysHQ -> KotonohaEngine -> third-party libraries
```

The engine target must not link to a School Days target or compile a School
Days implementation unit. Zlib is linked by `SchoolDaysHQ` for GPK/Config.DAT;
it is not an engine dependency solely because School Days needs it.

## Build artifacts

Windows desktop CMake produces, at minimum:

```text
Release/KotonohaEngine.lib
Release/SchoolDaysHQ.exe
```

The application receives the runtime SDL/FFmpeg/libass DLLs next to the
executable in the same way as before. Engine-owned Anime4K shaders are still
staged as engine runtime assets.

## Validation expectations

Before merging a boundary change:

1. Configure CMake from a clean build tree.
2. Build `KotonohaEngine` independently.
3. Build `SchoolDaysHQ` against that library.
4. Verify the engine archive has no School Days symbols/dependencies.
5. Run generic media/KTRF tests and School Days focused gates.
6. Run `git diff --check`.

The original School Days executable is never required for these build gates.

## Remaining compatibility seams

This split intentionally does not rename or reinterpret authenticated game
behavior just to make the source tree look cleaner. All School Days headers now
live under `games/school-days/include`, so the reusable engine include tree has
no School Days API dependency. The game runtime still contains older
profile-facing class names and ORS-specific APIs internally. Future cleanup
should proceed in this order so behavior remains reviewable:

1. Introduce a generic asset provider interface (resolve/read/list/reference)
   and keep GPK/filesystem policy in the game.
2. Make playback-rate mechanics fully generic and keep the retail
   `{1, 2, 4, 12, 24}` policy in School Days.
3. Replace ORS-specific Video keys with generic media handles/time windows.
4. Extract prompt presentation/choice policy behind a game presentation hook.
5. Extract ABC/voice animation behind an image/overlay extension.
6. Generalize or delete the legacy profile-facing API names once every caller
   is behind the new extension interfaces.

Until those seams are complete, moving those APIs back into `KotonohaEngine`
would recreate the reverse dependency this split is designed to prevent.
