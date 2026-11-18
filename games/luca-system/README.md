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
