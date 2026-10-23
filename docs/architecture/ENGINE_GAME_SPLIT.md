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
libKotonoha         shared runtime/profile library, including SDL callbacks
        ^
        |
SchoolDays          SDL entry executable (desktop)
```

On Android there is no desktop executable target; SDLActivity loads
`libKotonoha.so`, which contains the same callbacks and application runtime.

## Source ownership

### Engine

All engine implementation and runtime subsystem files live under `src/`, with
public interfaces in `include/Kotonoha/`. The reusable base in `KotonohaEngine`
contains low-level rendering/media/audio, time and I/O, the generic KTRF
runtime/router, platform helpers, upscaling, configuration and other
game-independent primitives.

`libKotonoha` adds the higher-level scene/gameplay host, component systems,
renderers, parser and debug UI from `src/`, plus game-provided profile adapters.
The latter are kept in a separate CMake source group and do not leak into the
reusable `KotonohaEngine` archive. PCM analysis, ABC animation/texture helpers
and the virtual-asset provider seam are engine-owned.

Ports share three engine helpers for media:
- `Kotonoha::GameplayFlow` draws the session scene into a "game" canvas layer at output resolution.
- `Kotonoha::CanvasImage` sends pictures and video frames through the upscaler. It re-upscales only
  changed sources.
- `Kotonoha::PcmPlayer` plays decoded PCM clips on the `Voice`/`BGM`/`Se` mixer channels.

`cmake/KotonohaTargets.cmake` enforces the base-engine boundary at configure
time: sources and public headers assigned to `KotonohaEngine` are rejected if
they contain an explicit `SchoolDays`/`schoolDays` implementation dependency.
Engine C++ declarations use `Kotonoha` or nested namespaces. C-compatible
exports retain their `Kotonoha` prefix; SDL and Android entry points retain
the global names required by their platform ABIs.

### School Days HQ

Game-owned implementation lives under:

```text
games/school-days/src/
```

This directory owns the School Days routing/profile, KTRF adapter, persistence,
frontend/UI, GPK archive reader/mount, save/load/replay/route-map/menu behavior,
generated authenticated routing tables and SDL app callbacks. The small
profile-specific `Gameplay` adapter lives at `src/runtime/Gameplay.cpp` and is
compiled as a profile-runtime extension rather than part of the reusable core.
Generic and runtime component, parser, renderer and debug files are all under
`src/`, with their APIs under `include/Kotonoha/`; these subsystem directories
are no longer stored under the game tree. Game-specific routing/persistence
headers and asset policy remain under `games/school-days/include` and
`games/school-days/src/`.

Each application (`games/generic`, `games/school-days`) has its own
`CMakeLists.txt` and links the shared `KotonohaRuntime` library
(`libKotonoha`); macOS produces `Kotonoha.app` (generic engine + debug UI) and
`SchoolDays.app`. The game's `Main.cpp` ends with
`#include <Kotonoha/Main.hpp>`, which defines the `SDL_App*` entry points with
the engine's default behaviour (engine master lifecycle, forced scripts,
overlays, debug UI) and then calls the game's `LocalArgs`, `LocalInit`,
`LocalEvent`, `LocalIterate` and `LocalQuit` hooks. Generic's hooks just return
`SDL_APP_CONTINUE`; School Days' hooks add its KTRF/retail behaviour (in
`SchoolDaysCallbacks.cpp`). The reported `SDL_AppResult` is always the worst of
the default and local results (FAILURE > SUCCESS > CONTINUE).

All three ports (School Days, Snow Radish Vacation, Zero no Tsukaima) share the
same game loop: `LocalInit` attaches a `Kotonoha::GameSession` to a
`Kotonoha::GameplayFlow` (`include/Kotonoha/components/GameplayFlow.hpp`),
`LocalEvent` forwards to `GameplayFlow::HandleEvent` and `LocalIterate` calls
`Kotonoha::MarkFrameHandled()` and returns `GameplayFlow::RunFrame`, which runs
the splash, the game frame, the debug UI, the MCP service and the present.
Snow and Zero sessions use the flow's generic title/text box/choices/pause/save
UI. School Days' `SchoolDaysSession` returns `OwnsFrontend() == true`: the flow
forwards events and frames to the retail front end (KTRF controller, title,
SaveLoad, route map, config, menu bar, BackLog) and the session picks the debug
GUI (KTRF scene list/state inspector) and the Gameplay reported to MCP.

A session that keeps the generic in-game UI but has its own boot/title screens
overrides the front-end hooks instead (`HasFrontend`, `EnterFrontend(boot)`,
`FrontendEvent`, `FrontendTick`, `DrawFrontend`). The flow then calls them
whenever it would show its own title page; `FrontendTick` returns the
`TitleAction` picked (New Game, Load, Quit) and the flow still provides the
load page (Escape/right click goes back), pause, save/load and the text box.
Zero no Tsukaima uses this for its native warning/logo/movie/title/menu.

Only the game picture uses the game viewport: `GameplayFlow::Frame` enables the
letterboxed logical presentation (`Config::width`/`height`) while it draws the
session and disables it again before returning, and pointer events are mapped
through the same viewport. The splash, debug UI, MCP screenshots and other
overlays therefore always render in full-window coordinates. School Days can load `Script/` assets from a loose
directory with `--sd-script-dir <dir>` (maps `Script/<rest>` to `<dir>/<rest>`,
overriding the GPK) and takes its splash from `System/SYSTEM/LOGO.png` in the
GPK. The default path calls the engine master lifecycle
(`EngineMasterInit`, `EngineMasterEvent`, `EngineMasterMain`,
`EngineMasterShutdown`) for common SDL, renderer, audio, font, GUI, and engine
setup, and School Days applies its policy on top. SDLActivity loads `libKotonohaSchoolDays.so` or `libKotonohaGeneric.so` (Intent extra `target` = `SchoolDays` | `Generic`) on
Android.

When debug UI is enabled, the game continues to render across the full window;
the menu bar and debug tools are drawn as overlays, without a reduced,
letterboxed game viewport or per-frame render readback. Display-mode changes
are managed by `Kotonoha::SetDisplay`, shared by the debug settings and the
School Days SDL event path. School Days adds its KTRF scene catalog and state
inspector to that Debug menu. Whenever the debug UI is turned on (F7,
`KOTONOHA_DEBUG_UI=1` or the settings), `Debug::SyncActivation` closes every
tool window, so only the Debug panel is shown; the other windows are opened
from its checkboxes. Script/Text editors display virtual
archive-backed scenes read-only; filesystem scenes retain editing support.

Image upscaling is generated and cached with each decoded image texture;
video upscaling is regenerated only when a decoded frame changes. Frame
presentation reuses those textures and never runs the upscaler over the full
screen. Changing the enable flag or factor invalidates the content-texture
caches. With upscaling enabled, a factor above `1.0` is required; if no shader
backend is available or it fails, the engine retains the requested factor and
uses SDL texture scaling instead of silently disabling upscaling.

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

Retail School Days content is packaged with `libKotonoha`. The game-owned
mount/resolver can expose the 29 v1.02 GPK files through the engine's generic
asset-provider and media interfaces (`sdgpk://` + custom FFmpeg AVIO). GPK
parsing, DFLT/zlib handling, Script.GPK ORS resolution and game-specific
resource naming are not part of `KotonohaEngine`.

## Dependency rule

The allowed direction is:

```text
SchoolDays -> libKotonoha -> KotonohaEngine -> third-party libraries
```

`KotonohaEngine` must not link to a School Days target or compile a School Days
implementation unit. `libKotonoha` links the reusable base with the higher
level runtime and profile adapters. Zlib is linked to `libKotonoha` for
GPK/Config.DAT, not to the reusable engine.

## Build artifacts

Windows desktop CMake produces, at minimum:

```text
Release/KotonohaEngine.lib
Release/libKotonoha.dll
Release/SchoolDays.exe
```

On macOS the app bundle contains `libKotonoha.dylib` in `Contents/Frameworks`;
on Android SDLActivity continues to load `libKotonoha.so`. Engine-owned
Anime4K shaders are staged with the `SchoolDays` app.

## Validation expectations

Before merging a boundary change:

1. Configure CMake from a clean build tree.
2. Build `KotonohaEngine` independently.
3. Build `libKotonoha` and the `SchoolDays` entry executable.
4. Verify the engine archive has no School Days symbols/dependencies.
5. Run generic media/KTRF tests and School Days focused gates. Engine tests
   live in `tests/`; each game keeps its own tests under `games/<game>/tests`.
   School Days' CTest targets (`games/school-days/tests/CMakeLists.txt`) are
   enabled with `KOTONOHA_ROUTER_TESTS`/`KOTONOHA_ORS_PARSER_TESTS`; its
   `ktrf_*` folders are standalone CMake projects and `ktrf/` holds the Python
   tests for `tools/ktrf` (`python3 -m unittest discover -s games/school-days/tests/ktrf`).
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

## Mobile game data

- Android: retail `.gpk` files (and `school-days-hq.ktnroute`) go in the OBB folder
  `Android/obb/me.hirameki.kotonoha/`; `Kotonoha.java` passes it as `--sd-gpk-root`.
- iOS: copy the `.gpk` files and the route into the app Documents folder (File
  Sharing is enabled). The engine exposes it through `KotonohaMobileGameDataDir()`.
- Xcode: `Xcode/KotonohaEngineTest.xcodeproj` has the `Kotonoha` (generic) and
  `SchoolDays` targets/schemes.
