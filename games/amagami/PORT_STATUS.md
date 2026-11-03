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
  Fragments are composed at integer coordinates before scaling the whole actor,
  avoiding seams in hair and clothing. Separate eye/mouth expressions, blinking,
  audio-driven mouth frames, inline F/E/M/P cues, costume/pose crossfades,
  actor zoom, scripted nods and position/scale oscillation are supported.
- Cold breath uses the original three `sysgraph/breath1..3` textures and the
  `Breath.run` layer drift, scale and fade timings. `Actor.setBreath` controls
  the amount; voiced lines emit it, using `CharParam` pose directions.
  Inline W frame waits also pause the text reveal.
- Backgrounds (including rotation), interface textures, assembled characters
  and breath particles draw through the shared engine upscaling pipeline.
  The composed character cache updates only when its pixel content changes,
  keeping motion/fade-only frames from rerunning the upscaler.
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
