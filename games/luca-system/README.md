# LucaSystem

**English (US)** | [Português (Brasil)](README.pt-BR.md)

Native PAK/bytecode interpretation and image/audio/text rendering tested against AIR.

**Status:** AIR-specific functionality is documented; do not assume all LucaSystem titles have equivalent coverage.

## Build

```sh
cmake -S . -B build -DKOTONOHA_BUILD_LUCA_SYSTEM=ON
cmake --build build --target LucaSystem
```

## Per-title opcode catalogs (LuckSystem)

The inspector accepts original LuckSystem text opcode catalogs, including
UTF-8 BOM and Windows CRLF. Names are resolved by index so a game-specific
OPCODE.txt cannot be mistaken for AIR's built-in opcode mapping:

    build/bin/LucaInspect /path/to/AIR/files seen170 36 --opcode-table data/AIR.txt
    build/bin/LucaInspect /path/to/other-game/files scenario --opcode-table data/SP/OPCODE.txt

Without the --opcode-table option, AIR's opcode names remain the default.
The numeric filter is optional and must be within 0..255. Truncated scripts
and malformed tables now fail instead of silently emitting partial output.

This improves inspection, not runtime compatibility for non-AIR games.
Opcode payload formats, script relocations, rendering and VM handlers remain
title-specific and require game-data regression tests.

    cmake --build build --target LucaSystemOpcodeTableTests
    ctest --test-dir build -R LucaSystem.OpcodeTable --output-on-failure

## Game data

Use original PAK files (including PARAM.PAK and SCRIPT.PAK); AIR is the reference title.
Use the launcher or pass `--luca-system /path/to/game-data` to the executable. Never commit commercial game files.

## PAK import and repack

On a native build, build the `LucaPak` host tool:

```sh
cmake --build build --target LucaPak
build/bin/LucaPak list SCRIPT.PAK
build/bin/LucaPak repack SCRIPT.PAK SCRIPT-patched.PAK name:seen170 replacement.bin
build/bin/LucaPak repack VOICE.PAK VOICE-patched.PAK id:1 replacement.ogg
```

`list` prints ID, byte length, and name. Pass one or more selector/file pairs
to replace entries, or omit the pairs to rebuild without replacements.
The tool preserves names, IDs, entry order and block alignment, and writes
to a new file. It does not add or rename entries or interpret imported
payloads. Keep commercial inputs and repacked outputs local.

### Conservative raw script import (all titles)

The patch-raw command updates one or more instruction *payloads* by their
original byte offsets, including unknown opcodes from non-AIR titles. It does
not rewrite the four-byte instruction header or the alignment byte, and every
replacement must have exactly the original payload length. Multiple edits
across different PAK entries are applied together.

    build/bin/LucaInspect /path/to/game/files seen170 --opcode-table data/AIR.txt
    build/bin/LucaPak patch-raw SCRIPT.PAK SCRIPT-patched.PAK name:seen170 2202 raw-payload.bin

The input is a binary file containing all bytes after the four-byte
instruction header (including any fixed parameters). This is meant for
expert, format-aware edits; changing a jump operand to an invalid target or
changing an opcode's internal fields can still break the game. For variable-
length AIR English dialogue, continue using patch-air, which relocates jumps.
A standalone regression exercises multiple edits, bad offsets, and wrong sizes.

### AIR dialogue import

Use `LucaInspect` with opcode `36` to find the original decimal byte offset of
the `MESSAGE` to change. Save the replacement English text as UTF-8 without a
BOM. For example, with an existing `overlay` directory:

```sh
cmake --build build --target LucaInspect LucaPak
build/bin/LucaInspect /path/to/AIR/files seen170 36
build/bin/LucaPak patch-air /path/to/AIR/files/SCRIPT.PAK overlay/SCRIPT.PAK name:seen170 2202 dialogue.txt
```

Pass `--asset-overlay overlay` when running the port against
`/path/to/AIR/files`. To import several edits at once, repeat the selector,
offset, and file arguments using offsets from the input PAK. The tool keeps
voice IDs and other languages, updates known local and cross-scenario branch
offsets, and rebuilds a new `SCRIPT.PAK`. This command targets AIR Steam
dialogue in Kotonoha; it does not edit choices, log text, or the original
engine's auxiliary tables.

### Bitmap font editing

`LucaPak edit-glyph` replaces the alpha mask and metrics of an existing glyph
in an 8-bit CZ2 atlas. For the AIR Steam dialogue font, use a 28 × 28 PNG with
transparency. The output directory must not exist yet:

```sh
build/bin/LucaPak edit-glyph /path/to/AIR/files/font_win32_1280/FONT_GOTHIC1.PAK name:ゴシック27 /path/to/AIR/files/font_win32_1280/FONT__INFO.PAK name:info27 font-overlay U+0041 glyph-A.png 3 4 24 32
```

The four numbers set the signed X and Y offsets, draw width, and advance in
pixels. An advance of zero uses the draw width. The command keeps the
original palette and re-encodes only changed CZ2 blocks, then repacks the
archives. Use `--asset-overlay font-overlay` with `/path/to/AIR/files` as
the port's data directory. The source PAKs remain untouched. The command
does not add glyph slots; codepoints sharing a cell also share its image
and draw metrics.

Use `add-glyph` instead to append a cell for an unmapped BMP character.
For example, to add `U+E000`:

```sh
build/bin/LucaPak add-glyph /path/to/AIR/files/font_win32_1280/FONT_GOTHIC1.PAK name:ゴシック27 /path/to/AIR/files/font_win32_1280/FONT__INFO.PAK name:info27 font-overlay U+E000 glyph-new.png 3 4 24 32
```

The command updates the Unicode index and metrics, enlarges the atlas if
needed, and leaves existing glyph positions and palette indices intact.
To add another glyph to the same font, use the previous output PAKs as input
and choose a new output directory.

The AIR dialogue box retains the original bitmap font and window art.
When enabled, Kotonoha's dialogue and speaker color overrides also apply to
glyphs from the bitmap atlas and its overlays. The dialogue box uses the
shared wrapping cache, keeping line breaks stable during typewriter reveal.
The side buttons open **Menu**, toggle **Skip** or **Auto**, and replay the
current line with **Voice** when it has a recording.
Story choices use the original centered `SELWIN_s` and `SELWIN` art with
Kotonoha's choice input. The date badge in the upper-left corner uses the
story date from `#6003` and the original `jul_` and `aug_` art.
Days 16–31 select `jul_`; days 1–15 select `aug_`. The badge is drawn once
after scene composition, so a missing or replaced background slot cannot
hide it. Virtual black and white fills still conceal it.

## AIR menus

Startup displays the copyright notice first, then the Visual Arts, Key and
Prototype/LUCA logos. The logos are silent. At the title, ocean waves
(`nami`, `200014`) loop on Kotonoha's `Se` channel; **Reminiscences**
(`trk22`, `100021`) starts in the DREAM prologue, not at the title. Use
Enter, Space or a click to advance the startup sequence, or Escape to
skip to the title.

**NEW GAME** opens the original story selector before starting a chapter.
On a fresh profile, only **DREAM** is available. Use the title arrow to
slide to the second page, which has **GALLERY** and **MUSIC**.

Open the pause menu with Escape or the controller back action. The port uses
AIR's `systemmenu` icons, `system_menu_bg` and English manual pages. Save,
Load and Options open Kotonoha's native slot manager and settings; Quick Save
and Quick Load keep using `quicksave.kns`. History (B or Page Up) uses the
original `backlog_texture` with Kotonoha's scrolling and voice replay. Open
menus capture input without advancing dialogue.
Pausing also freezes scripted waits and the opening movie. The opening
uses the soundtrack embedded in the original WebM; script sound effects
on separate channels no longer interrupt each other.

The AIR Save and Load screens use the original backgrounds, card frames,
focus art and page arrows in a 20-card grid. Use the arrow keys or
controller D-pad to select a card; use Page Up/Page Down or Z/C to change
pages, and Escape or X to cancel. Kotonoha handles naming, overwriting,
renaming and deleting `.vn` saves. Each new save embeds a screenshot of
the story without menus, shown on its Save and Load cards. Older saves
without a screenshot still show their slot names.

**Options** uses AIR's original background, nine English tabs, dialogue
preview frame and slider art. Kotonoha handles the display, text, audio and
system options. Use Tab or click a tab, then **Apply and save** to commit
staged changes. Escape or X discards changes still in the draft and
returns to the title or pause menu; volume and text speed apply live.
Keyboard, Mouse and Touch list the bindings supported by the port rather
than providing the original game's remapping editors.

From the title, open **Gallery** to browse the 72 CG slots in four
categories. Use the arrows or controller direction buttons to navigate,
Enter to open a slot, and Escape to go back. Page Up/Page Down switch pages
in the grid. Each CG variant unlocks when it is drawn in the story, not when
it is preloaded, and stays unlocked in `global_system.kns`. The gallery does
not display artwork from locked variants.

## Detailed research

[Concise support status](STATUS.md) · [Detailed port research](PORT_STATUS.md). Historical results are specific to the reference assets/revision and may contain superseded implementation notes.

[Build guides](../../docs/BUILD_GUIDES.md) · [Port index](../README.md)

## Save and resume behavior

`LUCA_SYSTEM_SAVE_3` stores the instruction pointer, numbers, strings
(including whitespace, newlines and binary bytes), flags, GOSUB return
stack, visible dialogue and remaining wait duration. Resuming a save does
not execute additional bytecode before restoring the saved state.

Valid `LUCA_SYSTEM_SAVE_2` files are still readable, but text already
truncated by the old whitespace-delimited format cannot be recovered.
Malformed payloads are rejected before the VM state is applied. Complete
visual-scene restoration and full AIR compatibility still require
retail-data verification.

Dependency-free codec regression test:

```sh
cmake -S . -B build -DBUILD_TESTING=ON -DKOTONOHA_BUILD_LUCA_SYSTEM=ON
cmake --build build --target LucaSystemSaveCodecTests
ctest --test-dir build -R LucaSystem.SaveCodec --output-on-failure
```

## Script-ordered dialogue

Keyboard, mouse and shared advance actions now agree: the first input
reveals unfinished text and the next resumes bytecode execution. The
runtime no longer displays extracted script strings out of order, bypassing
interleaved image, audio, wait or branch instructions.

F5/F6 are handled before generic dialogue confirmation, so saving/loading
works while a textbox is visible. A synthetic test checks the
`Reveal`/`ResumeScript` decision; original AIR data is still needed for
integration verification.

## Lossless visual save state V6

V6 now records the original (group, slot) per image and its alpha, scale,
rotation, tint, visibility and interpolated MOVE offsets with remaining time.
The renderer resumes the same arrangement of layered portraits and CZ4 facial
expressions after Load, instead of reconstructing grouping from image ID ranges.
V2–V5 saves remain readable through their original fallback layouts. The
codec rejects invalid floats, duplicate layer keys and excessive collections
without applying malformed state. Retail integration is still needed to
compare mid-transition frames and other live effects.

## Visual save state V4 & Cross-Scenario V5

`LUCA_SYSTEM_SAVE_5` records the asset IDs and positions of rendered `IMAGELOAD`
objects as well as cross-scenario call stack frames (`CallStackFrame`). Loading reopens
images from the existing PAKs and restores the exact visual layer and multi-file call frames.
Existing V4, V3 and V2 save payloads remain 100% readable.

Original scene compositing, background layers, transitions, animations
and full game compatibility remain outside this change. Saves do not
embed retail image data.

## Experimental scene composition and transitions

`SceneLayers` stages `IMAGELOAD` objects until `DRAW`, with bounded
alpha interpolation from the previous visible layer. `MESSAGE`/`END`
flush pending images when a script does not invoke `DRAW`. `FADE`
uses a timed black overlay and `WIPE` currently approximates a wipe with
cross-fading. The V4 save format still records only committed objects.

This is **not** 1:1 AIR scene composition: original masks, directions,
curves, animation semantics and background behavior require original
binary/asset validation. Standalone C++17 regression test:

```sh
cmake --build build --target LucaSystemSceneLayersTests
ctest --test-dir build -R LucaSystem.SceneLayers --output-on-failure
```

## Position-preserving sprite replacements and MOVE interruptions

Implicit-position IMAGELOAD commands now retain the previous portrait's
on-screen placement and visual transform instead of resetting it to (0,0).
Small CZ4 facial expression crops also inherit the body's animation,
tint, alpha, scale and rotation while remaining above that body. An explicit
expression position is honored.

MOVE samples its current interpolated offset before accepting another MOVE,
preventing an immediate jump to the old animation's endpoint. Save V6 uses
the exact same interpolation when snapshotting an in-progress movement.
Standalone SceneLayers regressions cover all these cases.

## Bounded gameplay image lifetime

The LucaSystem runtime now prunes decoded CPU CZ images and matching SDL GPU
textures after each committed scene, retaining precisely the assets referenced
by the staged, visible and outgoing transition layers. A still-running DRAW or
WIPE retains both scenes; the outgoing assets are released only after the
transition deadline. Returning to title and loading a save also drop obsolete
gameplay images and reset stale transition clocks. This avoids growing CPU/GPU
image caches across chapter changes without disturbing expression overlays.
Standalone C++17 tests cover live, outgoing and discarded asset lifetimes.

## Robust RGBA uploads and missing-image retry

The renderer now constructs and uploads CZ image textures fully before
publishing them to caches. Failed SDL uploads or blend/sampler setup destroy
the provisional texture, avoiding random VRAM garbage on the next frame.
Script objects, title art, dialogue atlas and splash logos share this path.
The SDL3 pixel regression verifies a valid RGBA upload and rejects missing
pixels/invalid strides.

Missing or corrupt image assets are logged no more than once per two seconds
per ID, preventing repeated archive extraction and decoding in every frame.
A later retry still permits an overlay to repair the missing asset. Invalid
non-finite or zero-size sprite geometry is ignored instead of sent to SDL.

## SDL3 pixel-based scene regression

A dedicated SDL-backed C++17 regression now exercises the same GPU composition
helpers as the native LucaSystem runtime. It renders red/blue synthetic scenes
through the software SDL3 backend, verifies the exact midpoint blend and
non-overlapping WIPE pixels, and checks that the renderer draw state, alpha
modulation and parent clip are restored. No commercial AIR files are required.

    cmake --build build --target LucaSystemGpuSceneTests
    SDL_VIDEODRIVER=dummy ctest --test-dir build -R LucaSystem.GpuScene --output-on-failure

## GPU scene composition and transition correctness

Gameplay DRAW now composes the incoming scene into a native 1280x720 SDL
render target and blends the two *complete* frames. Previously each sprite
was faded independently, causing semi-transparent hair, masks, portraits and
colored overlays to accumulate alpha incorrectly. Animated, cropped CZ4
portraits stay in authored canvas coordinates. Image assets remain on the
GPU renderer; dialogue and UI text continue through the shared Kotonoha UI.

WIPE uses two non-overlapping GPU scissor regions: the old frame to the
right, the incoming frame to the left. Neither source rectangles nor rotated
sprites are cropped per layer, preventing stretched/repeated image fragments.
The caller's clip state is restored before UI rendering. A GPU render-target
allocation failure keeps a legacy fallback and reports an SDL warning.

Expression fragments in group 3 are now rendered immediately above the
matching group-2 character slot, not on top of every portrait. The
`LucaSystem.SceneLayers` regression guards z-order and wipe
boundaries without requiring commercial artwork.

This is a compositing fix, not proof of identical retail AIR transition
masks or animation timing; those still require real-data visual comparisons.

## Horizontal WIPE reveal

`WIPE` now uses a dedicated left-to-right reveal rather than the former
cross-fade placeholder. Both source and destination texture rectangles are
clipped at the transition boundary to avoid stretching sprites. The previous
composition is kept while newly staged objects are revealed; the
`LucaSystem.SceneLayers` regression checks transition progress and clipping.

This is **not** the exact original AIR transition mask or semantics; per-title
wipe directions, timing and object composition still need verification.

## Incremental composition without duplicate drawing

While `DRAW` or `WIPE` is running, the outgoing composition retains
existing objects while incoming drawing processes only newly committed
images. Previously visible translucent sprites are no longer drawn twice.
Once the transition completes, the visible composition is rendered once.
`LucaSystem.SceneLayers` checks the outgoing/incoming boundary.

AIR's CZ4 character expressions can be small crops of the same portrait
canvas. When `IMAGELOAD` replaces a character slot with a smaller matching
crop, the previous full portrait remains underneath and only the expression
changes. `INIT` and later full portraits clear the old expression. This is
checked against the retail `cgmz20` / `cgmz21p` pair and the `seen190` flow.

Original AIR transition masks, object ordering and timing still require
retail-data validation.


## Native script nodes and assets in the Debug UI

Press **F10** and open the script inspector. The list now enumerates the
scenarios in `SCRIPT.PAK`; for the active scenario it also shows decoded
MESSAGE, SELECT, IMAGELOAD, BGM, VOICE, SE, MOVIE and other significant
instruction nodes. Selecting a node exposes its original script bytes and
logical provider asset IDs, such as `image:1127`, `voice:73` and
`bgm:100001`. The current instruction also includes visible images and the
active voice/BGM. The list refreshes when the active scenario changes.

Use **List provider assets** to browse named PAK assets and numeric image,
voice, music and SE IDs. **Dump raw asset to Documents** saves unchanged
bytes into per-game `Documents/asset-dumps`; it accepts both original
and effective (overlay) sources. **Open in Voice Dubbing** sends the
selected audio ID to the shared dubbing applet.

Jumping into an instruction starts a script from that position and **does
not reconstruct earlier VM variables, call frames or rendered layers**.
The currently loaded script is inspected without decoding every retail
script at once. `LucaSystem.DebugCatalog` is a synthetic C++17 regression
test; retail scene navigation still requires the optional retail-data suite.
