# Pure Mail DVD animations

This implementation was checked against the retail DVD assets and the original
`data/pmail.exe`, SHA-256
`a9359d55f23831ebee3b4ca803c6aff1d28a37468de1ccb0e19f8d388ca0e654`.
The executable was inspected with [REA](https://github.com/morluto/rea) 6.7.0
and its Rizin command provider. Commercial assets are not included in this repo.

## Two formats sharing the .anim extension

Sequential animations start with a little-endian frame count, followed by
(offset, byte-size) pairs at offset 4. Frame storage begins at `0x2084`.
Their frames use Pure Mail's headerless CHAR stream: bottom-up rows on an
800×600 canvas, BGR colors, transparent skips and alpha/run tokens. Cropping
must preserve the original canvas coordinates. These include OSBOOT, the
password effects, emotion effects and both text cursors.

Lip animations have a `0x684` header, up to 16 independently voiced regions,
and 96 bytes per region. Relative to the start of each region's stride:

| Field | Byte offset |
| --- | --- |
| Actor ID | 4 |
| Three frame offsets | 12, 16, 20 |
| Three frame sizes | 44, 48, 52 |
| Region x, y | 76, 80 |
| Region width, height | 92, 96 |

Background lip frames are bottom-up BGR rectangles. Character lip frames can
instead be headerless CHAR streams; STYU04S4 is one retail example. The shared
provider also accepts headed CHD frames used by Snow Radish. Applying only the
raw-rectangle decoder silently loses character mouth animation.

`SnowAssets` retains the most recently decoded animation, so requesting a frame
does not decompress and decode the entire file again. Frame metadata is cached
separately. Stage textures are released when no longer used by the scene.

## Scripted effects: SYS 51

Native handler `0x4240e0`, jump table `0x42443c`, calls player `0x407360`.
The script supplies `(preset, x, y)`; the executable supplies the remaining
parameters. `0x42c210` is the import thunk for `timeGetTime`, confirming that
the interval argument is in milliseconds.

| Preset | File stem | Area | Interval | Restore background each frame |
| --- | --- | --- | --- | --- |
| 0 | osboot | 320×240 | 54 ms | No |
| 1 | ikari | 67×87 | 125 ms | Yes |
| 2 | jitoase | 27×97 | 125 ms | Yes |
| 3 | ase | 80×131 | 125 ms | Yes |
| 4 | aser | 95×120 | 125 ms | Yes |
| 5 | mouse | 40×64 | 160 ms | Yes |
| 6–9 | ok, error, pass_o, pass_c | 98×28 | 200 ms | Yes |
| 10–18 | pass0103 through pass0111 | 180×12 | 180 ms | Yes |

OSBOOT contains 80 frames, or 4320 ms. In PRO1 the script invokes it at
(245, 152). The graphic itself reads **FORIS OS 7.7**. It is not an AVI and must
not be replaced with the next static CRGB background.

The player renders every frame and suspends the VM until playback completes.
A slow update advances at most one frame; normal updates retain the fractional
interval remainder. Dialogue clicks do not bypass it. The final frame remains
in the scene, matching the buffer copy at `0x4075e8`. SYS 48 and SYS 69 clear
that scene's animation layers. Pausing freezes playback; save/load retains
preset, position, frame, frame count and elapsed phase with the suspended VM.

## Mouths and dialogue cursor

`0x407210` updates mouth regions whose actor ID matches the active voice actor.
During dialogue waiting, it runs every seven 17 ms ticks (119 ms). The sample
thresholds and 16-entry patterns at `0x41f58c..0x41f5c6` and
`0x44e9d4..0x44e9f3` are reproduced by `MouthFrame`. Silence [-64,64] closes
the mouth without advancing the pattern. Quiet samples include [-16378,16377];
other samples use the loud pattern.

The port follows the effective audio clip and playback position, including
pause, save/load and dubbing overlays. Original waveOut buffer alignment and
its 44-sample envelope sampling have not been compared in a synchronized run;
the port currently samples the decoded clip on the 119 ms presentation grid.
The mouth patterns and actor matching are verified; this is not a claim of
sample-exact lip timing against a running Windows executable.

The normal dialogue cursor uses CURSOR01's native canvas placement, in a
0,1,2,3,2,1 sequence at 170 ms per step (`0x41b01b..0x41b12d`). The original
also has a CURSOR02 mode; its assets decode, but the corresponding automatic
advance mode is not implemented here.

## Validation

- `PmailAnimationFormatCheck`: generated fixtures without retail data, covering
  sequential files with more than 16 frames and headerless character lip frames.
- `PmailGraphicsCheck`: all 395 CHAR assets, 620 ANIM files and 2037 frames;
  original title white key/orientation and waku placement; malformed tables.
- `PmailAnimationPlaybackCheck`: all 19 presets, OSBOOT duration, VM barrier,
  no skipped frames after stalls, save/load phase, scene cleanup, mouth patterns.
- `PmailAnimationRenderCheck`: software-rendered frames compared against CPU
  composition at the script coordinates, including OSBOOT, mouse, password,
  and actor-matched character mouth regions.
- `PmailStoryFlowCheck`: progression through PRO1, including the computer's
  scripted animations. It does not certify every native command or route.

See the port README for CTest configuration. Other presentation work remains,
including transitions and the computer's text/icon/chat commands. This
animation work does not establish 1:1 parity for the entire game.
