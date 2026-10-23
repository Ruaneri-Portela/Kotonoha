# Zero no Tsukaima – Muma ga Tsumugu Yokaze no Nocturne (PS2) port

Status: **formats layer only**. The game is not playable in the engine yet.

Assets are never stored in the repository; tools read the user's own disc image
(`.bin` MODE1/2048 or 2352, or `.iso`; a `.chd` must first be converted with
`chdman extractcd`).

## Disc layout (recovered with REA/Rizin on `SLPS_258.30`, MIPS R5900)

| File | Content |
| --- | --- |
| `SLPS_258.30` | PS2 ELF executable (entry 0x10001c) |
| `NORMAL`, `SCENEDAT` | Archives of packed TIM2 pictures (backgrounds 640x448, 8bpp) |
| `SCENE_ID` | Archive of packed scene bytecode (not decoded yet) |
| `SYSTEM` | Archive of plain-text Squirrel scripts (Shift-JIS) |
| `SOUND_ID`, `VOICE_ID` | Audio archives (PS2 ADPCM, not decoded yet) |

Every `NAME.HD` is an array of u32 entry sizes; entry *i* of `NAME.BIN` starts at
the next 2048-byte boundary after entry *i-1*.

## Compression (executable routine at 0x10f730)

Header: `u32 size, u32 mode`, then commands: `111nnnnn` literal run of n+1,
`110nnnnn` + byte repeated n+2, `iiinnnnn` (i<6) replay the i-th most recent
command with a new length (moved to front of a 6-entry MRU list, initially a
zero-run). Verified on every compressed entry of the disc (size matches).

## Implemented (`games/zero-no-tsukaima`)

- `Disc`, `ParseArchiveIndex`, `Unpack`, `DecodeTim2` (+ `ZeroFormatsTest`).
- `ZeroExtract <disc> <outdir> [--png]` dumps archives and PNGs.

## Remaining

1. Scene bytecode VM (`SCENE_ID`) and the Squirrel `SYSTEM` scripts.
2. PS2 ADPCM (VAG) decoding for `SOUND_ID` / `VOICE_ID`.
3. Engine integration: text/choice presentation and a `Main` for the game.

## Atualização: scripts de cena

- O descompactador tem um segundo estágio (de-interleave por stride, campo `mode`), já compilado; `ZeroFormatsTest` passa.
- `SCENE_ID` são scripts Squirrel 2.x compilados (775 arquivos, 775 funções main). Todos usam apenas 9 opcodes (1,2,4,19,20,30,31,32,60): são tabelas de chamadas estilo KAG (tags `label`, `tagname`, `msgwin`...), logo uma VM mínima basta, sem VM Squirrel completa.
- Os scripts `SYSTEM` (fonte Squirrel) ainda exigem uma VM/interpretador completo ou reescrita das classes.
- Ainda NÃO jogável: faltam interpretador dos scripts, camadas/texto, áudio VAG e o `Main` do jogo.

## Jogável (cena/texto) — `ZeroNoTsukaima --zero <disc.bin> [--mcp]`
- `SceneSource` lê SCENE_ID/SYSTEM direto do disco; `Game` interpreta as cenas (tags estilo KAG):
  texto Shift-JIS, falas, flags/`eval`, `goto`/`next`, escolhas, avanço por clique.
- Validado via MCP: START.KS → prólogo → sala de aula, escolhas e ~300 páginas sem travar.
- Pendente: mapa nome-de-imagem → índice do disco (fundos/CGs/sprites hoje aparecem só como texto),
  áudio VAG/BGM, saves. A ordem das cenas ao fim de um script (índice-1) é uma suposição.

## Menu, texto e saves (componentes do motor)
- `Kotonoha::TextBox` desenha a janela de texto (nome + corpo com quebra, legenda) na densidade real da tela.
- Escolhas e menus usam `Kotonoha::Prompt` (agora com lista vertical para N opções, teclado ↑/↓/Enter/1-9 e `SelectChoice` genérico) via `ChoiceMenu`.
- `GameMenu`: título (はじめから/つづきから/終了), pausa (Esc ou botão direito: voltar/セーブ/ロード/タイトル) e 3 slots.
- Saves em `SaveStore` por replay determinístico das entradas do jogador.

## Audio (added)
- `VOICE_ID` / `SOUND_ID` entries are headerless mono PS2 SPU-ADPCM (`DecodeVag`). `dispname voice=N` is the `VOICE_ID` index
  (entries of ~12 bytes are silent). Voices play at 32000 Hz and BGM at 44100 Hz (rates estimated from line lengths, unverified).
- `bgmNN` is mapped to `SOUND_ID[NN]` (guess); `bgmaNN` series and SE tags are not mapped yet.

## Images (added)
- `Catalog.cpp` rebuilds SCENEDAT name order from SYSTEM entries 31 (stage table) and 35 (CG list): backgrounds 0-147 (3 time variants for TIME stages), event CGs from 148.
- `ZeroMain.cpp` draws background then event CG (tall scroll CGs show their top part). Verified: sky and classroom backgrounds.
- Pending: CG scrolling, `ex*`/`tundorama` images, white/black stages, ECG09D/MOE* images.
- Audio: BGM decoded as stereo (4 KiB interleave).

## Engine flow (added)

The player-facing flow (title, pause, save/load by input replay, text box, choices, dim/panel styling) now lives in the engine class `Kotonoha::GameplayFlow` (`include/Kotonoha/components/GameplayFlow.hpp`). The port only implements `Kotonoha::GameSession` (story logic, pictures, audio). `LocalIterate` just returns `GameplayFlow::RunFrame` (splash, game frame, debug UI, MCP, present), the same loop School Days uses. Only the game picture is letterboxed to the logical resolution; the splash and the debug UI render over the full window.

## Audio rates (from the original code)
- Voices: `Talk(ch, voiceid)` (SLPS_258.30 @ 0x102de0) starts the VOICE_ID stream through 0x119e98 with
  rate 22050 (`addiu a3, zero, 22050` @ 0x102eac); the player converts it to SPU pitch as
  `(rate << 12) / 48000`.
- BGM (`*.STV`): `SOUND_ID.LST` puts every sound section under a format header; BGM sits under
  `ST44TL.PSH` (stereo 44.1 kHz) and the stream structures default to pitch 3763 (= 44100). Stereo
  interleave is 0x1000 bytes (the two end flags are exactly 0x1000 apart). A tuning check of the decoded
  tracks lands within ±3 cents of A440 at 44100 Hz (vs +45 cents at 24000 Hz).
- SE (`*.MNV`): under `MN2205.PSH` / `MN2205C.PSH` (mono 22.05 kHz); SYS_SE01 is the menu focus blip.
- Movies: 48 kHz stereo PCM from the PSS `SShd` header.
- Defaults: voice 22050, BGM 44100, SE 22050. Override with `ZERO_VOICE_RATE` / `ZERO_BGM_RATE` /
  `ZERO_SE_RATE`; F2/F3 (voice) and F4/F5 (BGM) still step the rate live.

## Character sprites (added)
- `EvalNutTable` (Scene.cpp) runs the data-only Squirrel main of ENVINIT.NUT (SYSTEM 31) and returns its table.
  From it `ImageCatalog` reads `characters` (pose -> dresses / faces -> codes, image `TCGnn_DRESSFACELEVEL`)
  and `positions` (左 -142, 右 142, 中央 0, 左奥/右奥 ±440, 左外/右外 ±1000, 左移動/右移動 ±148 relative,
  出/立 show, 消去/顔/無 hide).
- SCENEDAT after the event CGs: one empty entry per sprite base name (`TCG01_1D`) followed by its faces
  1..n (n = 7; 6 for TCG20-23 and 5 for TCG11 / the last TCG22 group, which lack the last faces), sorted by
  name. TCG15-18 (mob soldiers) are in ENVINIT but have no pictures on the PS2 disc. The final group (31
  entries) holds the demo/ending pictures, not sprites.
- `Game` keeps per-character pose / dress / face / xpos / visibility from character tags
  (`サイト {学生服 怒り 左 出}`), `allchar {hide}` hides, `allchar {reset}` clears. `ZeroMain` draws the
  sprites (bottom-aligned 640x448 canvases centred + xpos) between the background and the event CG.
- Verified via MCP: サイト / ルイズ / オスマン / ギーシュ / モンモランシー with the expressions of the dialogue.
- Not done: level (前/奥) zoom/y offset, show/hide slide transitions, face window (顔), time-of-day tint.

## Native front end (added)
Recovered from `OVERRIDE.NUT` (SYSTEM 35) and implemented 1:1 in `src/Frontend.cpp`
(hooked into `GameplayFlow` through the `GameSession` front-end hooks):
- Boot: WARNING (fade in 2 s, warning system voice + 2 s, fade out 2 s) → MMV_LOGO (fade in 2 s,
  logo voice + 2 s, fade out 1 s) → opening movie `ZERO2OP.PSS` → title. Any key/click skips a step.
  One of the system voice sets (`titleVoices`: logo/title/menu/warning) is picked at random (`random(6)`).
- Title: BGMA01, background fade-in 1 s, title voice, logo (16,61) fade-in 1 s, copyright and the blinking
  "press START" (1.5 s on / 0.8 s off). START → SYS_SE02 → game mode menu; 45 s idle → logo again.
- Game mode menu (SYSTEM_BG02 + GAMEMODE_TITLE): New Game, Load, Option, CG/Scene/Sound gallery, Special
  voice with ON/OFF images, hover/↑↓ (SYS_SE01), confirm (SYS_SE02), cancel → title (SYS_SE03).
  New Game and Load work; Option and the galleries only log "not ported yet".
- Pause → Title returns to the native title.
- Name tables: NORMAL entries 9/10/11 are `SCENEDAT.LST`, `SOUND_ID.LST` and `NORMAL.LST` (index = archive
  entry), entry 247 the movie names (14-byte records), SYSTEM 36 the system voice ids. BGM is now looked up
  by name (fixes an off-by-one BGM choice).
- Movies (`Pss.cpp`, `MoviePlayer.cpp`): the PSS is an MPEG-2 program stream; video 0xE0 is decoded with
  FFmpeg (mpeg2video), private stream 1 carries `SShd` (type 1 = s16le PCM, 48 kHz stereo, 0x200 interleave)
  + `SSbd` blocks alternating L/R. The soundtrack position drives the video clock.
- Not done: native in-game message window (`MAIN_MESSAGEBASE`, name plates), option and gallery screens.

## Engine audio channels and canvas (added)

- Voice, BGM and SE play through `Kotonoha::PcmPlayer` on the engine `Voice`, `BGM` and `Se` channels, so the
  debug Audio Mixer (F7 → Playback & Mixer) controls them. The movie soundtrack goes to `BGM`.
- Pictures, sprites and movie frames are `Kotonoha::CanvasImage`s drawn into the `GameplayFlow` "game" canvas
  layer at output resolution. The upscaler runs only when a picture changes (one pass per new movie frame).
  Turning upscaling off frees the upscaled copies. The layer appears in the debug Canvas Layers window.
