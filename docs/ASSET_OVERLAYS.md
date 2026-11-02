# Shared assets, input and translation

All engine providers use one overlay contract. Public `AssetProvider` reads,
existence checks, listings, local streaming paths and decoders check the selected
folder first. Native providers implement the `*Asset` methods for fallback;
the public entry points cannot accidentally bypass the override.

Run any game with:

```sh
Game --asset-overlay /path/to/overrides
```

Or select **Game → Asset overlay** in its launcher. The folder is remembered
with that game's engine settings. With no folder selected, the original assets
are used. Mounting a different folder invalidates the provider's texture cache.
Original installation files are never modified by an overlay.
`--no-asset-overlay` explicitly disables a previously remembered folder.

Files mirror the provider's logical IDs. Both slash styles and ASCII filename
case are normalized. Duplicate names differing only by case are rejected.
Relative traversal and links outside the selected folder are rejected. Existing
files can be replaced in place; new names require remounting the indexed folder.
Game caches may retain resources already decoded during the session.

Examples:

- DDLC: `bgm/1.ogg`, `gui/logo.png`, `script-ch0.rpyc`.
- ZNT: `script/START.KS`, `scenedat/12`, `sound/BGMA01`.
- Snow: `script` (SCRIPT.DAT data), `graphic/name.evd`.
- AIR: `Seen.txt`, `g00/name.g00`, `bgm/name.nwa`.
- School Days: `Script/English/00/00-00-A00.ENG.ORS`, `System/...`,
  `BGM/SD_BGM/...`. `--scripts` remains an alias for `--asset-overlay`.
  Legacy flat translated `.txt` names are adapted through the shared index;
  the dedicated script-only filesystem scanner has been removed.

Standard image/audio overrides are probed by the engine decoder, even when the
logical ID has a native extension. Native encoded replacements fall back to the
port's decoder. Listings merge overlay-only names with original names.

## DDLC with and without VNX PT-BR

The patch is optional. Select the original installation independently:

```sh
DokiDoki --ddlc /path/to/original/game --no-asset-overlay
DokiDoki --ddlc /path/to/original/game --asset-overlay '/path/to/[VNX+] Doki Doki [PTBR 1.1] [Binaries]'
```

The VNX adapter recognizes `game/` loose scripts and `vnx/game/` packages.
`.rpa` and VNX `.rpx` resources override the base; loose files take priority.
Scripts and the VM see the overlay before initialization. The patch installer's
archive check uses the mounted translated view, so no copying into the original
installation or relaunch is necessary. Both modes have separate regression tests.

## Common interaction

`InputActions` belongs to an Engine, rather than static variables in UI helpers.
Escape, Android Back, right-click, a two-finger tap and a left-edge swipe share
the back action. B, wheel-up and an upward swipe share the backlog action.
`ScrollGesture` supplies wheel direction, keyboard navigation and touch dragging
for lists and histories. Single taps generate a mouse click only after release;
a completed swipe is not also delivered as a tap.

Native ports can override `Runtime::handleBackAction()` and
`handleBacklogAction()` to retain their own menus. Shared components use the
same input vocabulary; School Days bridges it to its retail panels.

## Live translation and ownership

The editor writes through `Engine::setTranslation`, which notifies active scenes.
VN text subclasses, choices and timed subtitles update without advancing the
story, duplicating history or resetting subtitle time. Numeric translation route
IDs are distinct from display/debug labels. ZNT exposes route IDs and reads only
the selected scene; opening the editor does not decode the whole disc.

Engine implementation files are C++/Objective-C++. Legacy KTRF/ORS C-compatible
headers remain for file-format and port compatibility. New C++ callers can own
these values with `Resource` and the aliases in `parsers/Documents.hpp`.
Parser scratch buffers, files and callback buffers use scoped ownership.
Formatting is defined by `.clang-format` and `.editorconfig`; vendored code and
original game assets are excluded from formatting changes.

These shared changes and passing regressions do not establish complete 1:1
parity for every route, effect or ending of every port. Game-specific outstanding
work remains documented in each port's status file.

## Validation

The checked local data set covers original and VNX DDLC initialization and poem
selection, full act progression, the user-directory note and character-file
lifecycle. Both installations are read in place. ZNT's editor/inspector opening
regression uses a two-second maximum; this Mac's software-renderer run measured
about 48 ms for F7 and 32 ms for F10. These timings describe the measured test,
not a guarantee for other hardware.

The suite also checks core overlay priority/fallback/listing/decoding, live
translation without duplicate backlog entries, timed subtitle updates, gestures,
launchers, native ZNT menus, School Days route persistence, and the AIR/Summer
integration paths. Proprietary data remains optional and outside source control.

Common startup options are registered by `Kotonoha::CommandLine`:
`--launcher` (`--laucher` is accepted), `--no-launcher`, and
`--dump-translation FILE`. Export starts the selected game runtime, writes its
source dictionary and exits before advancing a story frame. Export bypasses the
launcher regardless of argument order. AIR and Summer use the engine's common
folder launcher, including its overlay and renderer settings.

Video rendering automatically uses the renderer's configured `Upscaling`
pipeline when no explicit pipeline is supplied. Ports render game pictures with
`RenderUpscaledTexture`; source crops stay in original texture coordinates and
already processed textures are not processed twice. Dynamic canvases supply a
content serial. Text rendered directly by the engine retains its native glyph
rasterization.

Android includes `air` and `summerRadish` flavors. DDLC stages the CPython
standard library and extension modules from the same Debug/Release installation
as its linked Python library. Build with Android Studio's bundled JDK when the
system Java version is newer than supported by Gradle.

The native Xcode project includes Air and SummerRadish schemes. For DDLC,
`tools/PrepareApplePython.py` verifies and prepares the pinned BeeWare archive;
`ruby Xcode/UpdateGameTargets.rb` updates the project after preparation (requires
the `xcodeproj` Ruby gem). The project links `Python.xcframework`, selecting the
SDK slice, and bundles standard-library resources and extension frameworks.
