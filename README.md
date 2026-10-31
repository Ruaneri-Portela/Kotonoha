# Kotonoha

English | [Portugues (Brasil)](README.pt-BR.md)

C++ visual-novel engine using SDL3, SDL_ttf, FFmpeg, Dear ImGui and embedded
CPython for Doki Doki Literature Club. Games extend the engine's runtime and
scene classes; the engine and ImGui are shared libraries. The SDL_ttf
configuration enables `SDLTTF_PLUTOSVG` by default, with PlutoSVG/FreeType
provided by vcpkg.

## Layout

- [lib/](lib/): public headers, engine implementations, tests and submodules.
- [assets/](assets/): engine splash, fonts, licenses and bundled GLSL shaders.
- [games/](games/): example, Zero no Tsukaima, Snow Radish Vacation, DDLC and School Days.
- [Xcode/Kotonoha.xcodeproj](Xcode/Kotonoha.xcodeproj): native iOS targets.
- [Android/](Android/): Android Studio/Gradle project with separate game APKs.
- [cmake/](cmake/): desktop configuration and `.app` packaging.

Game assets live in `games/<game>/game-assets/`. Commercial data is local and must
remain out of Git. Supply your own legally obtained data:

| Game | Source data |
|---|---|
| Zero no Tsukaima | `games/zero-no-tsukaima/game-assets/ZeroNoTsukaima.bin` |
| Snow Radish Vacation | `games/snow-radish-vacation/game-assets/SCRIPT.DAT` and the other DAT archives |
| DDLC | `games/doki-doki-literature-club/game-assets/game/*.rpa` and `game-assets/characters/*.chr` |
| School Days | Original game archives in `games/school-days/game-assets/` |

The port does not download or supply commercial game data.

## Desktop build

Requires CMake, pkg-config, FFmpeg, json-c, libass >= 0.17, FreeType, HarfBuzz, PlutoSVG and
OpenGL; DDLC also requires CPython development files. SDL3, SDL_ttf and ImGui
are built from submodules, not vcpkg packages.

```sh
git submodule update --init --recursive
export VCPKG_ROOT=/path/to/vcpkg
cmake -S . -B build \
  -DCMAKE_TOOLCHAIN_FILE="$VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake" \
  -DVCPKG_TARGET_TRIPLET=arm64-osx-dynamic
cmake --build build
ctest --test-dir build --output-on-failure
```

Disable individual games with `KOTONOHA_BUILD_EXAMPLE`, `KOTONOHA_BUILD_ZERO`,
`KOTONOHA_BUILD_SNOW`, `KOTONOHA_BUILD_DDLC` or `KOTONOHA_BUILD_SCHOOL_DAYS`. Each game can also be configured
independently from its own directory.

### macOS `.app` bundles, including the CMake Xcode generator

```sh
cmake -S . -B build/macos-xcode -G Xcode \
  -DKOTONOHA_BUILD_MACOS_BUNDLES=ON
cmake --build build/macos-xcode --config Debug
```

Each game gets its own `.app`; shared dependencies are relocated into the
bundle. Only DDLC bundles CPython, under `Contents/Frameworks`. The only
resource directories are `assets/` and the game's own `game-assets/` in
`Contents/Resources`. Local game assets are included by default.
`-DKOTONOHA_BUNDLE_LOCAL_GAME_ASSETS=OFF` leaves `game-assets/` empty and rejects
previously copied data: use a clean build when changing distribution policy.
Do not distribute commercial assets without the necessary rights.

The macOS VS Code launch configurations automatically configure a Ninja Debug
bundle build in `build/macos` before building the selected game. The
dependencies described above and Ninja must be installed. To configure it
manually, for example to set additional CMake options:

```sh
cmake -S . -B build/macos -G Ninja \
  -DCMAKE_BUILD_TYPE=Debug -DKOTONOHA_BUILD_MACOS_BUNDLES=ON
```

Each configuration builds its own target and debugs the executable inside its
`.app`. The game-data paths in `.vscode/launch.json` are local examples; adjust
them for your installation. School Days uses `--school-days` for the retail
Packs folder and `--scripts` for optional loose script overrides. Its launcher
selects both the game folder and the KTRF route; it remembers the route and
defaults to the bundled one. `--ktrf` explicitly overrides the remembered route.

## Native iOS Xcode project

Open [Xcode/Kotonoha.xcodeproj](Xcode/Kotonoha.xcodeproj). This project does not
run CMake or shell scripts during its build. Resources are assigned to each
target using native Resources phases; frameworks use Embed Frameworks with
Code Sign on Copy. Each game's `game-assets` directory is copied directly as
a folder reference, preserving its name without build scripts.

Schemes: `Kotonoha`, `ZeroNoTsukaima`, `SnowRadish`, `DokiDoki`, `SchoolDays`,
`KotonohaIOSSmoke` and `DdlcIOSScriptSmoke`. KotonohaEngine is a static framework;
apps embed the dynamic KotonohaImGui, SDL3 and SDL3_ttf frameworks.

The example executable/app is `Kotonoha`; its C++ namespace is
`KotonohaExample`, separate from the engine's `Kotonoha` namespace.
After changing School Days' library sources, run
`python3 Xcode/UpdateGameTargets.py` manually to synchronize its native target.
This preserves existing native Python modules and refuses concurrent project edits.

Prepare native dependencies before opening/building:

```sh
"$VCPKG_ROOT/vcpkg" install --x-manifest-root="$PWD" \
  --x-install-root="$PWD/vcpkg_installed" --triplet=arm64-ios
"$VCPKG_ROOT/vcpkg" install --x-manifest-root="$PWD" \
  --x-install-root="$PWD/build/ios-deps/simulator" --triplet=arm64-ios-simulator
```

The simulator setup targets Apple Silicon. Configure equivalent triplets and
architectures for Intel Macs. Ensure dependency deployment targets match the
minimum iOS version you intend to support; default vcpkg triplets can inherit
the installed SDK's version.

DDLC additionally needs BeeWare's CPython iOS SDK:

```sh
mkdir -p build/ios-deps/python
curl -fL https://github.com/beeware/Python-Apple-support/releases/download/3.15-b3/Python-3.15-iOS-support.b3.tar.gz \
  -o build/ios-deps/python/Python-iOS.tar.gz
python3 tools/PrepareApplePython.py --archive build/ios-deps/python/Python-iOS.tar.gz
xcodebuild -project Xcode/Kotonoha.xcodeproj -scheme DokiDoki \
  -sdk iphonesimulator -destination 'generic/platform=iOS Simulator' \
  -derivedDataPath build/xcode-ios CODE_SIGNING_ALLOWED=NO build
```

The preparation tool verifies the archive SHA-256 and prepares standard-library
resources, extension frameworks and `.fwork` markers for both SDKs. It runs
**before** the Xcode build; there are no Python packaging script phases.
Downloaded/prepared dependencies remain under ignored build directories.
Choose your signing team for device installation.

The native game bundle IDs match their Android application IDs:
`me.hirameki.kotonoha.zeronotsukaima`, `.snowradish`, `.ddlc` and `.schooldays`.
The example uses `org.kotonoha.example`. The platform's selected bundle ID or
application ID determines the installed app's sandbox, preferences and external
storage identity; C++ SDL metadata does not override Xcode/Gradle settings.
Earlier IDs (including `me.hirameki.kotonoha`, `me.hirameki.kotonoha.*` and
the previous `me.kotonoha.*` iOS IDs) identify separate installations.
Existing preferences/saves are not migrated automatically. Export needed saves
before uninstalling an older app, and update provisioning profiles for new IDs.

## Package paths and writable storage

Include `<Kotonoha/core/FileSystem.hpp>`:

```cpp
auto resources = Kotonoha::resourceDirectory();
auto engineAssets = Kotonoha::engineAssetDirectory();
auto gameAssets = Kotonoha::gameAssetDirectory("zero-no-tsukaima");
auto data = Kotonoha::preferredDataDirectory("zero-no-tsukaima");
auto documents = Kotonoha::preferredDocumentsDirectory("zero-no-tsukaima");
```

| Platform | Preferred data | Preferred documents |
|---|---|---|
| macOS | `.app/Contents/Resources` | SDL application-specific preference directory |
| iOS | App bundle resources, read-only | App sandbox `Documents` |
| Android | `Android/obb/<applicationId>` | `Android/data/<applicationId>/files` |

Packaged directories are always named `assets` and `game-assets`. Android
extracts APK resources into private storage before starting SDL because disc
readers, FFmpeg and CPython require real filesystem paths. Android game
defaults prefer external OBB data before packaged resources. Settings and
game persistence use the preferred documents API and never write into the app.
Directory lookup/creation failures are reported rather than silently falling
back to the working directory.

## Android

See [the Android guide](Android/README.md). SDL3 is fetched from the official
Git repository at a pinned revision; the Java SDLActivity and native SDL
library use that same checkout. Gradle builds separate example, Zero, Snow
DDLC and School Days flavors, and embeds the engine as shared native libraries.

## Native scene debugging and editable routes

F10 exposes each game's native scene/node list and jump operation. Scene asset
previews use the game's providers: pictures, video frames at the selected
timestamp, and decoded audio. After a debug jump in Zero no Tsukaima or Snow
Radish, replay saves are disabled until a normal start or save load.

The F10 inspector and MCP can dump/load editable `.json` routes and binary
`.ktnroute` routes. Generic engine routes use the `kotonoha.vn` profile and a
KTRF `VNSC` section. Registered native node callbacks are rebound when loading;
native executable code is never serialized. School Days instead uses its
original, full-core binary KTRF profile through its runtime adapter. These
profiles are not interchangeable. ORS parsing and shared routing code live in
`lib/`; game-specific runtime behavior stays in each game, without embedding a
second legacy engine.

## ASS subtitles and command-line extensions

Every game accepts `--help` (or `-h`), listing common engine options together
with that game's own options. Invalid options and missing values are errors.
For example:

```sh
build/macos/games/school-days/SchoolDays.app/Contents/MacOS/SchoolDays --help
build/macos/games/school-days/SchoolDays.app/Contents/MacOS/SchoolDays \
  --subtitle-style games/school-days/game-assets/styles.ass
```

`--subtitle-style FILE.ass` (alias `--ass-style`) imports only `[Script Info]` and `[V4+ Styles]`.
Original dialogue/events and embedded attachments are discarded; the game
supplies new cues. School Days defaults to its bundled `styles.ass`, ported
from the original style resource using the standard `.ass` extension and
the bundled Noto Sans JP font. A cue's speaker selects the identically named
style, falling back to `Default` or the first imported style.

The reusable [SubtitleScene](lib/include/Kotonoha/componentes/SubtitleScene.hpp)
renders cues with libass and inherits the engine's selected style. Use
`setCues`, `setPosition` and, optionally, `setStyle`. Shared timeline subtitles
use this scene too; School Days extends it for ORS dialogue.

[FadeScene](lib/include/Kotonoha/componentes/FadeScene.hpp), ported from
Kotonoha, fades the frame to or from a color. Drive events from an external
timeline (`add` + `setPosition`) or its own clock (`play`); `hold` keeps the
color after a fade-out. Games reuse it instead of ad-hoc fades.

SDL_ttf, ImGui and libass use only the fonts in `assets/fonts`
(`NotoSansJP-Regular.ttf`, OFL); there is no system-font fallback. Every
packaged or user path goes through
[FileResolver](lib/include/Kotonoha/core/FileSystem.hpp), which searches, in
order: the bundle, the preferred data directory (OBB on Android), the working
directory, the executable directory and the game's documents directory.
`engineAsset` looks under `assets/`; `gameAsset(game, ...)` looks under
`game-assets/` and `games/<game>/game-assets/`. `resolve` throws, listing every
searched location. The engine clears to transparent black (`0,0,0,0`).

The debug overlay caches the node list when it opens (use "Refresh node list"
to update it) and clips the node and log lists, so games with many nodes,
like School Days, keep their frame rate with the overlay open.

[CommandLine](lib/include/Kotonoha/core/CommandLine.hpp) registers the common
engine options. Games extend the same parser before calling `parse`:

```cpp
Kotonoha::CommandLine cli("MyGame", options);
cli.option("--my-game", "PATH", "Game data", gamePath);
cli.alias("--my-game", "--data");
cli.flag("--my-launcher", "Open the game launcher", launcher);
cli.parse(argc, argv);
// Handle cli.wantsHelp() before initializing SDL or loading game data.
std::cout << cli.help();
```

Options and flags can also take callbacks for game-specific validation.
Help automatically includes registered descriptions and aliases.

## School Days presentation and audio

School Days uses custom SDL scenes for the retail hover menu, timed top/bottom
choices and libass subtitles, rather than a permanent gameplay debug window.
Move the pointer to the top edge to reveal controls. Choices support mouse,
arrows, Enter/Space and their displayed number keys. Configuration, save slots,
backlog and route-map panels still use ImGui. Unsupported auto/speed controls
remain disabled.

Media decoding now fills both video and audio buffers before yielding, with
350 ms audio preroll; repeated pause/resume requests do not restart the SDL
stream. The buffering regression checks the minimum queued duration during
real-time playback and simulated frame stalls.

## Upscaling

Scale `1.0` disables upscaling; values above `1.0` enable it, up to `4.0`.
Automatic selects MetalFX on Metal, MPV on OpenGL and Linear elsewhere.

When using MPV, the launcher and F10 inspector expose a preset selector and
an ordered shader-file list. Choose multiple `.glsl` files using the native
dialog or select bundled files; remove/reorder items as needed. An empty
list uses the preset. Full paths are supported and persisted one per line;
legacy colon-separated lists still work. MetalFX has no method/preset or
GLSL-file selector, only its applicable backend and scale settings.

## Controls and diagnostics

- Click / Enter / Space: reveal or advance dialogue.
- Arrows / mouse: menus and choices; mouse wheel scrolls longer lists.
- Escape: pause/back; B: backlog; F5/F6: quicksave/load.
- F10: engine inspector; F11 / Alt+Enter: borderless fullscreen.
- F8/F9: node clock/FPS HUD.
- `--launcher`: optional pre-game configuration; default launch bypasses it.
- `--zero`, `--snow`, `--ddlc`: explicit game-data locations.
- `--translation`, `--dump-translation`: engine translation routes.
- `--config`, `--no-config`, `--renderer`, `--log`: runtime configuration.

The generic settings menu, launcher and debug inspector expose master volume
and mute. Master gain composes with channel volume/mute and per-stream gain;
new channels inherit it. In the generic menu, left/right adjusts volume by 5%.
Settings persist unless configuration persistence is disabled.

F8 includes the current node's elapsed time, including custom game runtimes;
F9 shows both FPS and milliseconds per frame. The inspector's **Debug UI scale**
setting (1x to 3x, also in the launcher) enlarges debug controls on top of normal
DPI scaling and is persisted with the other settings.

The [Portuguese reference](README.pt-BR.md) documents the complete component,
media, scripting and MCP APIs and the existing port limitations. ZNT/SRV
dialogue translations and DDLC's later acts have not been fully validated.
