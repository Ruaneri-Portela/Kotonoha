# Amagami — PS2 Japan (SLPS_259.18)

**Playable prologue on Kotonoha; the full game is not yet ported.**

The original `n0dea_ev01A` event can be played from beginning to end. Its three
answers execute distinct original blocks and converge back into the story.
The calendar, event unlocks, conversation mode, affection system, later routes
and endings are not implemented as a complete game flow. At the end of the
prologue the application returns to its title when the player presses Enter.
Other events can be inspected or started for development; unsupported calls
stop with the method and bytecode position instead of silently inventing a route.

## Run

Build from the repository root (the normal engine dependencies are required):

```sh
cmake -S . -B build -DKOTONOHA_BUILD_AMAGAMI=ON
cmake --build build --target Amagami AmagamiInspect
```

On macOS:

```sh
build/games/amagami/Amagami.app/Contents/MacOS/Amagami \
  --amagami '/path/to/Amagami (Japan).iso'
```

On other desktop platforms use the `Amagami` executable in `build/games/amagami`.
Launching without a disc opens the engine's disc picker. Its Play button remembers
the selected ISO and engine settings. `AMAGAMI_DISC` is also supported. The disc
is read in place; no retail data is packaged or copied into the repository.

Controls: Enter/Space/left click completes the text reveal, then advances; arrows + Enter or 1–3 selects answers;
Escape opens pause; F5/F9 saves/loads the quick slot; PageUp toggles the last four
history entries. The quick save is in the SDL preference directory
`Kotonoha/amagami/quick-save.txt` and uses deterministic input replay.

Development entry points:

```sh
Amagami --amagami DISC.iso --start n0dea_ev01A --no-launcher
AmagamiInspect DISC.iso --verify
AmagamiInspect DISC.iso --script n0dea_ev01A
AmagamiInspect DISC.iso --class GameMain
AmagamiInspect DISC.iso --play n0dea_ev01A 2
AmagamiInspect DISC.iso --png GRAPH/GRAPH1.ARC 723 background.png
```

`--play` runs the bytecode headlessly; the last argument is the zero-based answer
to select. `--start` and the engine debug node list allow direct event entry.
Translation export uses scenario indices as routes; original dialogue keys are
looked up through the engine translation dictionary before display.

## Implemented

- Engine `DiscImage` ISO9660 reader and disc asset provider, following the ZNT
  integration. Read-only ARC/AFS ranges are checked against the disc file size.
- ARC bucket tables and records; original case-sensitive filename hash
  `h = h * 0x3fad + signed_char`. Every scenario filename resolves to its actual
  archive entry; backgrounds and character atlases are resolved by filename.
- 2 KiB LZ ring, initial write index `0x7de`, 11-bit match offset and 5-bit
  match length + 3. `GRAPH0.PAC` expands exactly to `GRAPH0.ARC`.
- `AMAGAMI.IMG` class container and SCF version 4 classes, instance/class
  variables and methods, primitive constants, nested arrays and CP932 text.
- SCF stack interpreter with call frames, variables, message sends, literals,
  arrays, numeric comparisons, branches and suspended native results. It
  executes the original scenario bytecode, including the answer branch.
- Native dialogue and answer presentation, history, replay saves, title/pause
  UI and engine launcher. Save loading validates a replay in a candidate session
  before replacing the live session.
- Shared engine TIM2 decoder used by Amagami and ZNT. Character atlas fragments
  are composed from the disc's `.zb` rectangles, with separate face components.
- `AmagamiTextBox` extends `Kotonoha::VNTextBox`, reusing UTF-8 typewriter
  behavior at the original default of 10 characters/second. Confirm completes
  the current text before advancing and leaves the voice playing on reveal.
  Menus suspend audio and text progression. Original title background and
  message-window textures replace the placeholder title and dialogue panel.
  Character given names and temporary `setName`/`setNameDef` are honored.
- The text box and answer selector are registered subclasses of
  `Kotonoha::VNTextBox` and `Kotonoha::ChoiceQuestionScene`. The live translation
  editor uses the scenario route and original script key, updates the displayed
  line immediately, and preserves ordinary ASCII in translated text. Removing
  a translation restores Japanese display text without its control commands.
- Original title/start/main-menu, normal/choice windows, selection header,
  selector and continue-indicator textures are read directly from the ISO.
- Cooperative presentation threads share the original script objects while
  keeping independent VM call stacks. Scripted sleep, camera pan, zoom,
  quarter-turn rotation, waits and black/white fades advance on a 60 Hz clock.
  Creating a new background resets its transform, so the opening sky rotation
  no longer carries into indoor scenes. Identity opcodes 6/7 now match the
  original interpreter (inequality/equality respectively).
- Character body names use uppercase pose and lowercase costume. The original
  executable table at VA `0x1b7a70` supplies body/face prefixes and face aliases.
  Body fragments are alpha-composited at integer coordinates into a cached
  cropped texture. Transparent overlap preserves earlier fragments instead of
  cutting holes through the face, hair or clothing. Separate eye/mouth expressions, blinking,
  audio-driven mouth frames, inline F/E/M/P cues, costume/pose crossfades,
  actor zoom, scripted nods and position/scale oscillation are supported.
- Cold breath uses the original three `sysgraph/breath1..3` textures and the
  `Breath.run` layer drift, scale and fade timings. `Actor.setBreath` controls
  the amount; voiced lines emit it, using `CharParam` pose directions.
  Inline W frame waits also pause the text reveal.
- Backgrounds (including rotation), interface textures, assembled characters
  and breath particles draw through the shared engine upscaling pipeline.
  Static body textures and individual facial tiles each retain their cached
  upscale. Blinks, mouth frames, motion and fades reuse those textures instead
  of upscaling a reconstructed actor every frame.
- Engine `Kotonoha::decryptCriAudio` / `decodeCriAudio` support CRI type-8
  AHX and ADX before FFmpeg decoding. The disc provider supplies Amagami's
  key parameters; engine code contains no game key. Decoding trims to the
  container sample count, excluding padded terminal samples. ADX v4 BGM uses
  its embedded loop start/end, with original default BGM/voice gain ratios.
- Nested voice AFS metadata resolves `MIY1858.ahx` etc. FFmpeg decodes AHX/ADX;
  PCM goes through the engine's `voice` and `bgm` mixer channels. Music IDs use
  `M01_...` through `M40_...` metadata rather than raw AFS indices. Replay restores
  the BGM track and gain, starting the track again.

## Presentation limitations

The presentation is closer to the supplied game's scripts but is not yet a
verified pixel-perfect PS2 recreation. Fonts, actor origin/projection, arbitrary
non-quarter-turn rotation under nonuniform scale, vibration waveforms and
random blink sequences still need comparison against the original renderer.
Mouth movement follows PCM amplitude rather than the exact PS2 voice-device
algorithm. Some inline commands (including fractional voice waits), complex
multi-step actions, lighting/deformation, snow, sound effects and tutorials
remain incomplete. The pause/backlog screens remain simplified.

Replay saves restore dialogue/decision/costume state and restart music; they do
not preserve presentation-thread phase or the exact in-flight transition.
Gallery `setAlbum` has no gallery implementation. The classical MIDI soundtrack
mode, full scrollable backlog and controller rumble remain unimplemented.
Coverage of the prologue does not establish coverage of calendar/later routes.

## Verification

Verified on the supplied Japan ISO on macOS arm64:

- 2,308 scenario classes, 9,160 scenario methods, 84,467 string literals;
  all 222 system classes parse completely.
- 2,156 TIM2 entries decode across GRAPH0/1/2; all archive ranges validate.
- `GRAPH0.PAC` matches `GRAPH0.ARC` byte for byte after decompression.
- All three prologue answers reach original `EoS`, each with over 400 dialogue
  pages. Tests cover different answer text and saves before/after selection.
- MIY1858 encrypted AHX produces exactly 63,459 mono samples at 24 kHz,
  peak approximately 1.019 and RMS approximately 0.286, compared with the
  former encrypted decode peak of 5.758. An independent local vgmstream decode
  agreed after clipping to S16 (RMSE approximately 0.000087). Reference cipher
  behavior: [vgmstream AHX decoder](https://github.com/vgmstream/vgmstream/blob/master/src/coding/mpeg_custom_utils_ahx.c).
  REA found the original key string `mituba` at VA `0x29bc40`, referenced by
  the initialization routines near `0x1ac00c` and `0x1ac3b0`.
- All three answers also run to the same ending/dialogue count with timed
  presentation threads enabled. Every visible prologue body/face pair resolves
  to a real disc asset; camera motion cannot collapse actor scale.
- Engine checks assert the decrypted voice's sample count, peak and RMS,
  stereo ADX decoding, first-confirm reveal, camera motion during dialogue,
  visible sky pixels, live translation edit/removal, 2x engine upscaling,
  original choice navigation and outdoor cold-breath rendering.
- Engine software-renderer checks cover title, first dialogue, keyboard choice,
  Miya's fragmented sprite, an AHX voice decode and pause. Optional captures are
  written only when `AMAGAMI_CAPTURE_DIR` is set.
- Synthetic format/VM tests check malformed inputs, bounded reads, literal
  arrays, recovered comparisons, relative branches and native yield/resume.
- ZNT format tests run against the shared TIM2 decoder as regression coverage.

Enable disc checks explicitly (the path stays in the local CMake cache):

```sh
cmake -S . -B build -DAMAGAMI_DISC_IMAGE='/path/to/Amagami (Japan).iso'
cmake --build build --target Amagami AmagamiInspect AmagamiFormatsTest \
  AmagamiVmTest AmagamiSessionTest AmagamiEngineTest ZeroFormatsTest
ctest --test-dir build -R 'Amagami|ZeroNoTsukaima.Formats' --output-on-failure
```

## Reverse-engineering evidence

Analysis used the local REA CLI (`execute-rizin-command`) and Rizin against the
user's executable. Executable SHA-256:
`4df23518af6e3bbe8134eb025275e12243244df4e991a35b6274080d8126d3fc`.
Key routines in `SLPS_259.18`: LZ `0x117a68`, SCF interpreter `0x11dfe8`,
integer operations `0x11dc34`, ARC lookup/hash `0x1862a0` / `0x186560`,
background path formatting `0x10a2c0`. REA observations and disc-only probes
remain under ignored `.research/amagami/`; neither executable bytes nor script
text are included in this source directory.

Additional REA observations: identity handlers `0x11ef5c` / `0x11efc4`,
branch handler `0x11e81c`, actor prefix lookup `0x10a1c8`, actor face primitive
`0x115458`. Presentation parameters are also read from the original `Actor`,
`ActorPicture`, `Breath`, `CharParam`, `n0_MacroScripts` and event SCF classes.

The expanded regression run also exposed an existing `Kotonoha.AssetProvider`
check failure: the test expects an exception for a missing overlay directory,
while the current `AssetOverlay` constructor returns without mounting it. Neither
that constructor nor that test was changed by this port work. Amagami's seven
checks and the Infrastructure, ComponentHooks, Presentation and ZNT Formats
checks pass independently.

## Fidelity fixes and opcode audit (2026-11-03)

- Event pictures resolve through `Source::eventPicture` using the original
  `epi/e%1x_%02d%c0.tm2` convention (ELF format at VA `0x28e4d8`).
  The reported `stand/e6_24a.tm2` failure now resolves `epi/e6_24a0.tm2`.
  `Source::graphic` owns archive lookup for both existence checks and reads.
- Native Boolean AND/OR (`0x0c` / `0x0d`) are implemented and tested against
  all Boolean combinations. REA handlers: `0x11f1f0` and `0x11f2d8`.
- `AmagamiInspect DISC --opcodes` scans all 2,308 scenarios and 222 system
  classes, preserving instruction boundaries. The supplied disc contains 51
  opcode values across 1,592,565 instructions. The CSV includes support status,
  occurrence counts and the first class/method/offset for each opcode.
- Exception operations `0x2a`, `0x68`, `0x69`, `0x6a`, `0x6b` are now
  implemented: exception slot, clear, relative handler registration, handler
  removal and throw. Handlers restore the stack and unwind calls, with a
  thread-wide maximum of 30. Script array/arithmetic faults can enter catch;
  unsupported native methods and malformed bytecode still fail explicitly.
  This expands bytecode support, not the implemented calendar/conversation flow.
- Regression checks decode the event picture and verify opaque face pixels
  at overlapping mesh boundaries, in addition to the playable prologue checks.

## Portuguese draft and native engine controls

- [Translation files](../../translations/amagami/README.md) cover all 411
  unique prologue entries plus partial drafts in other scenes and shared labels. They are generated
  drafts, not a translation of the full game or a complete linguistic review.
- `translationSources()` exports all 2,308 native scenarios (84,414 entries)
  through the shared engine export path; previously `--dump-translation` failed
  because only the live-editor route methods were implemented.
- Native speaker labels, pause/title choices and event-end text honor the shared
  dictionary. Longer translated dialogue uses cached line/font measurements to
  fit the native window; source line breaks stay fixed during text reveal.
- Runtime hooks let MCP start, advance and step Amagami without pretending it
  uses the generic VNManager timeline. MCP reads the registered textbox/choice
  instances and exposes `runtime_diagnostic`. Agent pause freezes native clocks
  and audio. Save replay still does not restore exact animation/audio phase.
- `Amagami.Translation` loads the shipped Portuguese dictionary in the actual
  engine and exercises dialogue, choices, live edits and the native clock.

## Integrated VM and interface follow-up

- Scalar receivers dispatch through their SCF types (`Integer`, `Float`,
  `True`/`False`, etc.). `isKindOf` follows the original inheritance tables;
  `getClass` returns a class value. This fixes `.isKindOf/1` in
  `K2X_Script_.actTime:36`.
- `actStart` executes the original bytecode and initializes the animation
  multiplier to `1.0`; acknowledging it without updating state made camera
  movement collapse to zero. Real `actTime` and `laugh` bytecode, including
  the `n0_Action` movement coroutine, have regression coverage.
- `checkClear` remains a diagnostic-only no-op in the standalone scenario
  runner: it prints campaign completion statistics whose EventNode totals are
  not initialized here. Campaign/map initialization is still incomplete.
- ItemView `openItem`/`closeItem` now render the original small item texture and
  `baseitem` frame with the script's 12-frame scale/alpha transitions.
  `showItem`/`hideItem` preserve the item and animate visibility. The original
  scenario `n3nka_st01A` exercises item 220 in a regression check.
- Item textures use the engine texture upscaling path and natural dimensions;
  they are independent of background camera transforms. Broader SD/chibi
  presentation and the unused timing-minigame prototype are not validated.
- The pause menu opens Kotonoha settings. Translated title/choice headings,
  start prompt and backlog use the engine dictionary. Global strings export
  under `nodo`; the live editor lists those sources even without a dictionary.
- `ja.json` and `pt-BR.json` have matching corpus keys for all scenarios and
  original system-class strings. Most values outside the covered draft are
  still Japanese. The shared UI records global strings it measures/renders;
  ImGui debug labels are not fully localized.
- Root-level OpenCode dumps, logs and translation experiments were removed
  from the project root; recoverable artifacts live in ignored
  `.research/amagami/opencode-recovery/`. Parallel legacy translation workers
  were stopped before moving their intermediate files.

Validation of the integrated follow-up: `Amagami.Session`, `Amagami.Engine`,
`Amagami.Vm` and `Amagami.Translation` passed (4/4). The embedded MCP client
saved at the prologue choice, restored the save three times, and verified that
answers 0, 1 and 2 reached distinct original blocks with Portuguese dialogue
and no runtime diagnostic. Shared Presentation, DebugTranslation and MCP
regressions also passed before the final Amagami-only dispatch change.

Auxiliary review used the instructions in `.opencode/agents/gemma4-26b.md`
against the local Gemma endpoint. Suggested changes were checked against the
SCF class tables: `True`/`False` and class-object reflection were preserved.
The Jetson review timed out; its analysis is unverified. The OpenCode CLI
reported a provider routing error for the configured local Gemma model, so
that review was performed directly through Ollama.
