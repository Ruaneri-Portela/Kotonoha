# Snow Radish Vacation!! (Overflow, PC / ADV3) — port status

**Playable**, including voice-driven animation, original wipes, persistent cleared titles and the native location map. Remaining fidelity limits and validation are listed below.

## Known
- Disc: CloneCD raw image (2352-byte sectors), ISO9660. Engine `DATA/OF_ADV3.EXE` (Win32, 800x600),
  packs `DATA/PACK/{SCRIPT,GRAPHIC,SOUND,VOICE}.DAT`.
- Compression: LZSS (`LOCPU.DLL::DecodeLz`) in `SnowRadish::lzssDecode`. Entries whose compressed
  size equals the raw size are stored.
- Pack layout: independent entries + LZSS directory ending 12 bytes before EOF. The trailer is three u32 XOR
  0xF0F0F0F0: directory compressed size, raw size, checksum. 80-byte records, see `Pack.hpp`.
  Verified: SCRIPT 33 entries, GRAPHIC 669, SOUND 123 (offset chains are contiguous). VOICE 4428 entries; retail naming and script references verified.
- `.EVD` type 3 (800x600 pictures): delta/run/literal BGR stream from `LoadRgb`, stored bottom-up.
  `decodeEvd` + `SnowExtract --png` write 135 pictures (visually checked on 2 samples).
- `.obj` script: `[u32 codeOffset][u16 nStrings][nStrings*(u32 off,u16 size)][Shift-JIS strings][bytecode]`.
- SOUND.DAT entries are `seNNN.adps` (decoded by the ADPS implementation below). VOICE.DAT names are `V` + seven alphanumeric characters + `.ADP4`, including lettered takes such as `V012123a.ADP4`.

## `.obj` VM (of_adv3.exe, step function `fcn.00404694`, partial notes)
- Code is an array of u32 words at `codeOffset`; word 0 of the code area is a header count. Each word:
  bits 16-23 = operand mode (switch table at 0x404d29, 0x00-0x80), bits 24-31 = operation (0x00-0x21,
  switch at 0x404dee), bits 0-15 = immediate. Operand modes compute a value + kind (imm, string/table ref,
  relative jump +/-, `sys[]` call 0x404e60, `usr[]` object 0x4789a4 stride 0x2a); ops include
  jump/conditional jump (condition word at 0x47b7b4), call/return, push/pop (ring of 0x80 at 0x47b3a4).
- The VM/native mapping is partial; game flow, dialogue, choices, map selection, pictures, and audio are implemented, while unsupported native calls remain no-ops.
- Strings (`.obj` table) are Shift-JIS dialogue lines interleaved with voice ids (e.g. `V0121234`).

## Remaining limits
- AVI playback is not implemented; the supplied installed data has no movie directory/AVI files to validate against, and this presenter has no video decoder.
- Native-64 type 4 requests `OVERFLOW.ALPM`, `OVERFLOW2.ALPM` or `OVERFLOW3.ALPM`, none present in this retail pack. It returns without a wipe, matching the original missing-resource path (`FUN_00415d74`).
- Mouth amplitude is sampled from retained decoded PCM using the recovered cadence/tables rather than the original Windows audio-buffer position; SDL output latency can cause a small lip-sync difference.
- Type-5 pattern-4 subtractive shading uses the original subtraction blend where the renderer supports it; other backends fall back to alpha darkening. The special type-12 welcome presentation still uses a generic crossfade.
- Supplied graphics have no EVD type 2 or IMG types 1/3/4 entries, so those absent formats are not verified. The three `.SNW` files are paths used by the logo, not pictures.

## Playable build — `SnowRadish --snow <dir> [--mcp]`
- EVD backgrounds, CHD sprites, text, choices, location map, and day loop are implemented; previously validated through MCP.
- Shared title/pause/save/load flow and replay-based saves are implemented; save/replay validation is documented below.

## Audio (added)
- `.ADPS` (SOUND.DAT `seNNN`/`srNNN`): 4-bit step-table ADPCM, two interleaved channels, tables extracted from
  `of_adv3.exe` (`src/AdpsTables.inc`), decoded by `SnowRadish::decodeAdps` and played at 22050 Hz.
- Natives: 96 = SE, 112 = BGM (n -> `srNNN`, looped), 115 = BGM stop, 128 = voice. Played with SDL3 audio streams in `Main.cpp`.
- VOICE.DAT is loaded if present. All 4428 retail names pass the alphanumeric-take convention. The 33 scripts contain 6566 unique voice IDs, of which 2186 have no pack entry; missing voices remain silent (the scripts also assign IDs to unvoiced lines).

## Graphic types (added)
- Type 0 (`.EVD`/`.IMG`/`.CHD`): uncompressed BGR, `[0][w][h]` + bottom-up rows. Map pictures and UI buttons.
- Type 3 `.CHD` is the same stream as type 3 `.EVD` (full-frame CGs); `loadGraphic` tries `decodeEvd` first.
- Type 1 `.EVD` uses the CHD row format with `[1][w][yOffset][x][rows]`, `yOffset + rows == 600`, bottom-aligned.
- The 182 background/sprite `.ANM` files contain one or two independently voiced regions, not animation-type selectors. Each 96-byte region has an actor ID and three raw-BGR or embedded-CHD frames; both regions are decoded. Runtime overlays follow the speaker/native-128 actor and voice amplitude (see scheduler evidence below).
- `.ALPF` (200x150) and `.ALPA` (250x200) alpha planes are decoded and used by native-64 types 2/3. ALPF texels cover bottom-up 4x4 screen pixels; ALPA supplies the moving saturated flash plane, not a sprite mask.
- The two cursor `.ANM` files have three/four `{offset,size}` CHD records following a frame-count word; the first data offset is `0x2084`. All 184 retail ANMs decode, including both cursors. `SnowExtract --png` can export the first frame of 666/669 graphic entries; the remaining three are SNW paths.

## Engine flow (added)

The player-facing flow (title, pause, save/load by input replay, text box, choices, dim/panel styling) now lives in the engine class `Kotonoha::GameplayFlow` (`include/Kotonoha/components/GameplayFlow.hpp`). The port only implements `Kotonoha::GameSession` (story logic, pictures, audio). `LocalIterate` just returns `GameplayFlow::RunFrame` (splash, game frame, debug UI, MCP, present), the same loop School Days uses. Only the game picture is letterboxed to the logical resolution; the splash and the debug UI render over the full window.

## Audio rate and New Game flow
- BGM/SE are 22050 Hz stereo 16-bit (voices: mono), taken from the WAVEFORMATEX built in `of_adv3.exe` (file offset ~0x2e7a3).
- (Superseded) New Game used to open a heroine list; it now runs the original scripts and title (see below).

## Fixes from decompiling OF_ADV3.EXE (Ghidra via REA)
- Pack directory: every field of an 80-byte record was read from the previous record, so names, offsets and
  sizes were shifted by one entry. This was the cause of wrong scenes, sprites and sounds.
- `.ADPS`: an intro part plus a loop part. BGM refills the loop while playing; SE and voices play once and the
  stream is flushed at the end (otherwise SDL keeps the last samples, the voice never ends, and scripts that
  wait for it hang or repeat). `.ADP4` voices are mono 22050 Hz (they used to play too fast as stereo).
- Native 1: each text line plays its own voice (`Vxxxxxxx`, the string after the line); a click stops it.
- Native 49: sprite tint is RGB (ints 3..5). Above 0x100 an additive pass is used.
- Native 64: original type/pattern/speed dispatch. The presenter uses 15 ms quantized ticks and steps 4/8/2/16/1 for speed 0..4; clicks skip without entering replay history. Type 0 is instant; implemented types 2/3 use mask compositing, type 5 has scrolling/scaling/diagonal/interlaced shapes, and type 0xb inserts/slides the specified tinted STKGR sprite. See effect evidence below for ranges and missing type-4 resources.
- Native 213 and the title: the original title (pictures and hover buttons) is drawn through the custom
  title hooks of `GameplayFlow`. The heroine list was removed; New Game runs the scripts like the original.
- `GameSession::Skip()`: a click during a script wait (transition, SE) ends the wait without being recorded,
  so replay-based saves stay in sync.
- `SNOW_TRACE=1` logs every native call (`[vm]`).

## Engine audio channels and canvas (added)

- Voice, BGM (intro + loop, looping from the loop point) and SE play through `Kotonoha::PcmPlayer` on the
  engine `Voice`, `BGM` and `Se` channels, so the debug Audio Mixer controls them.
- Backgrounds, buttons and sprites are `Kotonoha::CanvasImage`s drawn into the `GameplayFlow` "game" canvas
  layer, so they go through the upscaler. They are upscaled once and the copies are freed when upscaling is
  turned off. EVD pictures are converted to RGBA when loaded, because XRGB textures blend as transparent in
  the RGBA canvas target.

## Original front end: Overflow logo and title (added)
- Boot order: engine splash (kept) → boot script `main.obj`: `69` white, `221` logo, `112(1,1)` BGM `sr001` loop, `213` title. The host `SnowSession` (GameplayFlow front-end hooks) plays 221 and 213 natively.
- Native 221 (`FUN_0043d327`): `logo00.evd` on white, 48 ms frames, 600 frames maximum (sec 20). At sec 10 it fades white → logo over 60 frames and plays voice `V0310000`. At sec 15 it fades logo → white. Falling flakes use `snow09`/`snow11.CHD` on `SLINE01-03.SNW` paths; the count starts at 1/1, adds 1 every 30 frames, and caps at 20/40; speeds are 2/1. Any input skips the logo.
- Native 213 (`FUN_004403de`, asset selection `FUN_0044090c`): uncleared items T00b (no saves: Start/Exit) or T00c (Start/Load/Exit); cleared items T00 or T00a respectively, at x 0x144. The boot entry is a wipe at speed 2 plus a 500 ms wait; other entries use speed 1. Up/Down/Enter work.
- Start re-runs the boot script with its transitions ignored. `sr001` keeps playing (not restarted), and the first wipe starts from the title snapshot.
- Pause → Title returns to the 213 title with `sr001` (this path is the port's assumption).
- Clear status comes from original system flag `0xcb` (`FUN_004186dc`), VM variable `0x50cb`. `main.obj` stores 0/1 at byte offsets `0x785c`/`0x78c8`. Changes write the versioned five-byte record `SRV`, version 1, flag 0/1 to `SDL_GetPrefPath("Kotonoha", "snow-radish-vacation")/system.dat`; input replay never writes it or defers an observed change until later input. Both the original reset and clear stores are honored. Future sessions seed the VM flag from that file; finishing replay restores the persistent flag instead of adopting a historical ending status. Later native-213 calls stop the VM and return to the title, rather than continuing into a new route.


## Native location map
- Native 219 (`FUN_00434136` → `FUN_00401468`) owns `ChoiceEvent`/`DrawChoices`, not the shared choice menu. Mouse hotspots and arrows/Return/Space select only destinations enabled by the original mask; saved inputs still store choice indices, preserving replay.
- Original `map0001`–`map0010` highlight pictures define hit rectangles. Five heroine badges use the original order `meg, alc, asg, tme, htk`. Hovered badges include `map_h01`–`map_h04` affection meters, clamped to 0..19 then divided by four; `$L302/305/303/304/306` map to VM variables `0x312e/3131/312f/3130/3132` (`FUN_00410199`).
- Clock/date/AM-PM use the original glyphs. Previews at (464,364) follow `FUN_00401156`: destination-to-background table `{8,7,4,3,9,6,10,5,2,1}`, b/c/e day/dusk/night suffixes and the recovered d-suffix room exceptions. The selected complete map (including preview, highlight and badges) is retained as the outgoing wipe frame.

## Runtime scheduler and effect evidence
- `FUN_0040b16c`/`FUN_0040b972`: background/sprite ANM layouts, three frames per actor region. `FUN_0040bf41` updates mouths every seventh scheduler call; `FUN_004273db` sleeps 17 ms, giving **119 ms**. `FUN_0042e435` supplies amplitude thresholds ±64/±16377 and the quiet/loud frame tables at `0x466e74/0x466e84`. Speaker IDs, not voice filename prefixes, select regions.
- Cursor loader `FUN_0041f20f` and message loop `FUN_004273db`: cursor01 manual wait (three frames), cursor02 auto (four frames), drawn at (715,555) every **170 ms** (10×17). Auto mode uses the shared session hooks, a two-second post-voice delay, and the correct cursor.
- Native-64 dispatcher `FUN_004358e0` → `FUN_00413c69`; executable configuration `GrpWipeWait` near `0x458af4` defaults to **15 ms**, not the former assumed 10 ms.
- Type 2 `FUN_004150ce`: hart/maru/sima/star/suri/suri-r/tako, pm00..41, ap00/01 ALPF selection; 0x400 mirrors horizontally and 0x800 inverts bytes. Normal/0x100/0x200 edges have widths 128/64/32 and phase ranges -256..128/64/32. The MMX `AlpBlend4/S/SS` implementations at `0x10002d70/0x10003080/0x10003380` use `clamp(mask+phase,0,width)/width`.
- Type 3 `FUN_0041582b`: `asabase.ALPF` plus saturated `asaflash.ALPA`, with original moving flash source offset `((phase+256)/8)*249 + row*250 + column +47`.
- Type 5 `FUN_004163eb`: effects at `0x417217/0x4172d6/0x417389/0x417450/0x417ab9/0x417c5a/0x417d4f/0x417e34/0x417f59`, covering scroll, partial reveal, squash/perspective shading, diagonal and alternating-scanline wipes. Extent is 800, or 1400 for diagonal patterns 6/7; pattern 1 intentionally has no intermediate effect.
- Type 0xb `FUN_004142c4`: low 12 pattern bits select STKGR1a..8l in character slot 1; 0x1000/0x2000 select warm/cool tint. It slides 256 pixels during the blend, rather than being an unrelated generic mask; speed 5 copies instantly.

## Validation
- Full existing build: `cd build && ninja` (no new build tree).
- `build/games/snow-radish-vacation/SnowRadishPackTest` passes synthetic alpha/ANM, malformed/multi-region ANM, map destination, type-0xb, all four title variants and cleared/reset-flag callback tests.
- The same test with the retail pack directory validates **184 ANMs (two cursors), 53 masks and 4428 ADP4 names**. **1349 inputs** reproduce the first native-219 map with identical choices, map arguments and script after replay.
- Actual SDL/OpenGL app on MCP port **8766**: original title, voiced STKGR1a/stmeg1a mouth-frame changes, ALPF wipe and map hover/preview captured; map replay save/load restored successfully. CoreAudio initialization stalled in this environment, so runtime smoke testing used `SDL_AUDIODRIVER=dummy`; PCM decoding and voice timing still run. Existing slot 1 was preserved; the previously empty slot 3 contains the first-map validation replay.
- `git diff --check -- games/snow-radish-vacation` passes. No shared engine or other game files were edited for this port.
- No complete route was played to completion in the final smoke run: cleared-flag detection is covered by the native callback test; the title/storage path is implemented, not claimed as a full ending-to-relaunch playthrough.
