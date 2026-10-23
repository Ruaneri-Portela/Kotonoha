# Snow Radish Vacation!! (Overflow, PC / ADV3) — port status

**Playable** (see sections below). The "Known/Pending" notes are historical.

## Known
- Disc: CloneCD raw image (2352-byte sectors), ISO9660. Engine `DATA/OF_ADV3.EXE` (Win32, 800x600),
  packs `DATA/PACK/{SCRIPT,GRAPHIC,SOUND,VOICE}.DAT`.
- Compression: LZSS (`LOCPU.DLL::DecodeLz`) in `SnowRadish::lzssDecode`. Entries whose compressed
  size equals the raw size are stored.
- Pack layout: independent entries + LZSS directory ending 12 bytes before EOF. The trailer is three u32 XOR
  0xF0F0F0F0: directory compressed size, raw size, checksum. 80-byte records, see `Pack.hpp`.
  Verified: SCRIPT 33 entries, GRAPHIC 669, SOUND 123 (offset chains are contiguous). VOICE.DAT not tested.
- `.EVD` type 3 (800x600 pictures): delta/run/literal BGR stream from `LoadRgb`, stored bottom-up.
  `decodeEvd` + `SnowExtract --png` write 135 pictures (visually checked on 2 samples).
- `.obj` script: `[u32 codeOffset][u16 nStrings][nStrings*(u32 off,u16 size)][Shift-JIS strings][bytecode]`.
- SOUND.DAT entries are `seNNN.adps` (probably PS-ADPCM); not decoded yet.

## `.obj` VM (of_adv3.exe, step function `fcn.00404694`, partial notes)
- Code is an array of u32 words at `codeOffset`; word 0 of the code area is a header count. Each word:
  bits 16-23 = operand mode (switch table at 0x404d29, 0x00-0x80), bits 24-31 = operation (0x00-0x21,
  switch at 0x404dee), bits 0-15 = immediate. Operand modes compute a value + kind (imm, string/table ref,
  relative jump +/-, `sys[]` call 0x404e60, `usr[]` object 0x4789a4 stride 0x2a); ops include
  jump/conditional jump (condition word at 0x47b7b4), call/return, push/pop (ring of 0x80 at 0x47b3a4).
- Not yet mapped: per-opcode semantics, the `sys[]` native table (graphics, text, voice, bgm, select, save).
- Strings (`.obj` table) are Shift-JIS dialogue lines interleaved with voice ids (e.g. `V0121234`).

## Pending
- Other graphic types: EVD/IMG type 0/1/2/4 (non-800x600, sprites), CHD, ANM, ALPF/ALPA (alpha), SNW.
- The `.obj` bytecode VM, VOICE.DAT, ADPS audio, AVI movies, saves, engine integration, Main, MCP validation.

## Jogável — `SnowRadish --snow <dir> [--mcp]`
- Fundos EVD, sprites CHD, texto, escolhas, mapa de locais e loop de dias; validado via MCP.
- Pendente: áudio (.adps/VOICE.DAT), saves, transições, nomes/gráficos reais do mapa.

## Menu, texto e saves
Mesmo conjunto do Zero: `TextBox`, `Prompt`/`ChoiceMenu` para escolhas e o mapa de locais, `GameMenu` (título/pausa/3 slots) e saves por replay. Validado via MCP (salvar e carregar restauram o mesmo trecho).

## Audio (added)
- `.ADPS` (SOUND.DAT `seNNN`/`srNNN`): 4-bit step-table ADPCM, two interleaved channels, tables extracted from
  `of_adv3.exe` (`src/AdpsTables.inc`), decoded by `SnowRadish::decodeAdps`. Sample rate assumed 44100 Hz (unverified).
- Natives: 96 = SE, 112 = BGM (n -> `srNNN`, looped), 115 = BGM stop, 128 = voice. Played with SDL3 audio streams in `Main.cpp`.
- VOICE.DAT is loaded if present next to the other packs; its entry naming was not verified (no file available to test).

## Graphic types (added)
- Type 0 (`.EVD`/`.IMG`/`.CHD`): uncompressed BGR, `[0][w][h]` + bottom-up rows. Map pictures and UI buttons.
- Type 3 `.CHD` is the same stream as type 3 `.EVD` (full-frame CGs); `loadGraphic` tries `decodeEvd` first.
- Type 1 `.EVD` uses the CHD row format with `[1][w][yOffset][x][rows]`, `yOffset + rows == 600`, bottom-aligned.
- Still undecoded: `.ANM` (multi-frame, 182 files), `.EVD` type 2, `.IMG` types 1/3/4, `.ALPF`/`.ALPA`, `.SNW`.
- `SnowExtract --png` now also writes sprites. 422 of 669 graphic entries decode.

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
- Native 64: transition. The original wipe goes 0..256 with step 4/8/2/16/1 for speed 0..4, and a click
  ends it. Type 0 is an instant copy. Wipe types 2-5 / 0xb are drawn as a crossfade.
- Native 213 and the title: the original title (pictures and hover buttons) is drawn through the custom
  title hooks of `GameplayFlow`. The heroine list was removed; New Game runs the scripts like the original.
- `GameSession::Skip()`: a click during a script wait (transition, SE) ends the wait without being recorded,
  so replay-based saves stay in sync.
- `SNOW_TRACE=1` logs every native call (`[vm]`).
- Not done: the exact length of a wipe tick (assumed 10 ms), the real shapes of the wipe types.

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
- Native 213 (`FUN_004403de`): items T00b (no saves: Start/Exit) or T00c (Start/Load/Exit) at x 0x144. The boot entry is a wipe at speed 2 plus a 500 ms wait; other entries use speed 1. Up/Down/Enter work.
- Start re-runs the boot script with its transitions ignored. `sr001` keeps playing (not restarted), and the first wipe starts from the title snapshot.
- Pause → Title returns to the 213 title with `sr001` (this path is the port's assumption).
- Not implemented: the cleared-game title variants (flag 0xcb, T00/T00a).
