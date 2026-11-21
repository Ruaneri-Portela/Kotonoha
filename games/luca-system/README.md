# LucaSystem

**English (US)** | [Português (Brasil)](README.pt-BR.md)

Native PAK/bytecode interpretation and image/audio/text rendering tested against AIR.

**Status:** AIR-specific functionality is documented; do not assume all LucaSystem titles have equivalent coverage.

## Build

```sh
cmake -S . -B build -DKOTONOHA_BUILD_LUCA_SYSTEM=ON
cmake --build build --target LucaSystem
```

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

## AIR menus

Open the pause menu with Escape or the controller back action. The port uses
AIR's `systemmenu` icons, `system_menu_bg` and English manual pages. Save,
Load and Options open Kotonoha's native slot manager and settings; Quick Save
and Quick Load keep using `quicksave.kns`. History (B or Page Up) uses the
original `backlog_texture` with Kotonoha's scrolling and voice replay. Open
menus capture input without advancing dialogue.

The AIR Save and Load screens use the original backgrounds, card frames,
focus art and page arrows in a 20-card grid. Use the arrow keys or
controller D-pad to select a card; use Page Up/Page Down or Z/C to change
pages, and Escape or X to cancel. Kotonoha handles naming, overwriting,
renaming and deleting `.vn` saves. Save cards show slot names rather than
the original game's screenshots.

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

Original AIR transition masks, object ordering and timing still require
retail-data validation.
