# Summer Radish Vacation!! 1.1 — reconstruction status

This is a native Kotonoha port under development. It reads the user's original
retail CMP archives; it does not embed or execute the Windows game engine.
**Complete 1:1 behavior has not been established.** Static decompilation verifies
the specific mechanisms below, not every route or every observable frame.

## Run

From the repository root on macOS:

```sh
cmake -S . -B build -DSUMMER_DATA_DIR="/Volumes/Server/data3/SRV_1_1"
cmake --build build --target SummerRadish SummerInspect SummerFormatsTest SummerEngineTest
build/games/summer-radish-vacation/SummerRadish.app/Contents/MacOS/SummerRadish \
  --summer "/Volumes/Server/data3/SRV_1_1"
ctest --test-dir build -R '^SummerRadish\.' --output-on-failure
```

`--summer` accepts the disc root, `DISC1`, or its `PACK` directory; `SUMMER_DIR`
is the environment equivalent. All five packs are required. Files stay on the
original volume. The engine's common CLI flags are listed by `--help`.

Enter/Space or a left click completes/advances dialogue. Escape opens the engine
pause menu; B opens history; A toggles auto; hold Control to skip previously read
pages. F5/F6 are quicksave/quickload. Native uniform/map hit masks and arrow-key
navigation are used. Uniform selection includes preview and confirmation.

Saves use the engine `.vn` container with VM positions, call stack, variables,
choices and scene state. System globals/read flags persist separately in
`preferredDocumentsDirectory()/system.dat`.
`SUMMER_SAVE_DIR` overrides that directory for isolated runs. Windows save files
are not imported. Pausing also suspends the three PCM streams.

`--dump-translation FILE` exports original script resource strings by stable
alphabetically sorted script route. `--translation FILE` loads engine translations.

## Implemented and checked

- CMP footer, encrypted directory, variable UTF-16 names, stored/compressed entries,
  literal/back-reference decoder, strict range validation. Retail inventory:
  INI **8**, SCRIPT **167**, GRAPHIC **379**, SOUND **186**, VOICE **5236** entries.
- XML parser/interpreter, labels, calls/returns, dynamic jumps, variables, first-match
  IFBLOCK, expressions, non-short-circuit logical operations. GSCRIPT read flags
  are per-script 16-bit indexed globals rather than integer literals.
- BGD adaptive 4-bit channel deltas; CGD per-channel 5-bit deltas/runs/literals;
  CHR row streams and placement; TEXB bottom-up BGRA and native alpha 0..128.
  **369** retail pictures decode; the other **10** entries are hit masks.
- Native premultiplied integer composition, including CHR preprocessing before
  blending. CHR embedded SubAnime records decode independently: **118** regions,
  each with three frames. Their playback scheduler is still pending.
- Native automatic character placement: creation-order allocation, retained slots,
  conditional positions (bit30), centering a sole actor and separating two
  automatic actors. Additional characters follow their base actor placement.
  Save version2 retains slot/order/requested-position; version1 remains readable.
- Boot warning/logo/title, original message-frame atlas, dialogue/history, text
  choices, native three-step uniform screen, day/night/cafe maps. Native map
  result codes, navigation, hit masks, badge placement and cafe previews.
- Opening MPEG from the original DISC1 loose file: PLAYMOVIE blocks the VM,
  decodes video and audio through MediaPlayer, pauses with the story, and
  resumes the following script only after playback drains. Missing movie files
  report an error instead of silently skipping the command.
- Ogg voice/SE/BGM through the engine mixer, BGM INTORO intro then loop,
  asynchronous BGM fade, SE waits, timed waits, staged scene changes at WIPE.
  Retail WIPE duration families 0/1/2/3/4/32768 and integer crossfade rendering.
- Kotonoha runtime/scenes, aspect-correct 800x600 presentation, settings, save/load,
  persistent globals/read state, translation export and debug script inventory.

## Evidence ledger

The REA CLI 6.7.0 was used with official portable Ghidra 12.1.4; its Apple Silicon
native decompiler was built from the matching bundled source. REA's ephemeral
analysis leaves the original files unchanged. Raw local Evidence lives under
`.research/summer/` (ignored by Git, together with extracted copyrighted data).

| Mechanism | Original module / entry | REA Evidence or bounded observation |
|---|---|---|
| TOPLADY route selection | SummerRadish.sdll `10004350` | `ev_ee6c2efc7596e38574f3b43b7e9d281a08aa469254ad2ab3cc3d47d483e71d85`: affection maximum, native tie priority |
| Uniform variable values | SummerRadish.sdll `10003d00` | `ev_e18e7897baf823e0668163bce46e4fc8f44cabb99e370a9b31427bb2ed512100`: index+2 and index+24 |
| Uniform preview/confirmation | SummerRadish.sdll `10004ec0`, `100052c0` | `rea-0x10004ec0.json`, `rea-0x100052c0.json`; masks, rectangles, system voices |
| Map input | SummerRadish.sdll `10001510` | `ev_8bc47d30e679f5b9cfc2698132eb5c96a9d843a6749d31f7d89fcfb6f74c0a22` |
| Map layout/badges | SummerRadish.sdll `10001f30` | `ev_fe2875d7e2fc2cf06feb5849c61c2dd9ba03516516892248b8e6123b794463a9` |
| Cafe preview | SummerRadish.sdll `10001080` | `ev_8ff9df4894394671902bc44daab837acebf7279dc982935f55303bdfc0ddee97` |
| CHR native composition | GObj_CHRData.mod `100012a0` | `ev_931549082da38b5b8a058a693c53b53fc7a63c0108786c0cee864722fe7f86c1` plus preprocessing evidence in `graphics-evidence/` |
| TEXB composition | Texture.mod `10001300` | `ev_2d97b7b63e02f865f4a840cc27a83385a08d45dd6ae3bc5cf0a38464fd4d136e` |
| Embedded animation records | GObj_SubAnime.mod | `ev_ec5d12148f1391a88c45e599841f65ec072b9f532195fc04a094727d25ea93d6` |
| XML expressions | SCRIPT.mod `100125d0` | `ev_f8471f4eace514a39f909324dc001ed7370181da48a9a8038a7e6e4f24da2774`; seeded assembly-confirmed entry |
| BGM fade | Sound.sdll `10004360` | `ev_ebe5f67ee816c34ffadb3567acd6255544a070052b2fe4d96c473149a65744fb` |
| WIPE dispatch/staging | Graphic.sdll `10003460`, `10004f80` | `rea-0x10003460.json`, `rea-0x10004f80.json` |
| Crossfade compositor | Wipe_XFade.mod `10001430` | `ev_2bcb1f142915b360f736e60d4b9afe940a5814c3a4c081a50b15bda19e610509` |
| Character slot allocation | Graphic.sdll `10001c60` | `ev_902fbac1bc07c611b3e357e0d2864b982e023cb02235142ee69b593693f6db91`: retained/conditional slots, collisions, two-actor separation |
| Movie command dispatch | Graphic.sdll `100060b0`, `10004e10` | `movie-evidence/playmovie.json`, `rea-FUN_10004e10.json`: first filename argument, synchronous playback loop before returning |
| Filename suffix | Graphic.sdll `100013f0` | Assembly-confirmed packed numeric/letter suffix and bit16 `_H`; REA name lookup for this undiscovered method remains unresolved |

## Remaining requirements for 1:1

- Original Windows execution has not been captured for differential frame,
  timing, audio and input-trace comparisons. No claim of exhaustive behavioral
  equivalence follows from the decoder tests or decompilation.
- Voice-driven mouth/eye scheduling is not connected, although every embedded
  frame decodes. Exact voice-buffer timing, tint/uncommon rendering flags and animation
  selectors require additional recovery and differential validation.
- Text typography, original text-choice strips and name-entry UI are incomplete.
  The engine uses its bundled Japanese font, not the Windows fonts named in
  CONFIG.INI. NAMEENTRY now edits four fields atomically, validates the native
  six-UTF-16-unit limit, restores defaults, remembers ChangeName00..03 and sets
  ChangePlayerName bits. Its modal layout uses the shared engine form; native
  dialog pixel geometry and name-dependent voice policies still need comparison.
- Original cleared-game extras (CG gallery, sound mode, replay browser), title
  variant unlocks, original settings and original save UI are incomplete. The
  current title exposes Start/Load/Config/Exit; settings/save UI use Kotonoha.
- Color fades, crossfade quantization and durations are recovered; renderer/clock
  parity, high-bit flags, all transition skip policies and end-credit timing
  still require comparison. Some recovered native configuration settings use
  their original defaults rather than a complete port of the configuration UI.
- Opening playback is connected and the current DISC1 supplies `summer_op.mpg`.
  Exact original movie skip input, scaling modes and audio/clock parity still
  require differential validation.
- Manual saves restore page/choice start and restart BGM; exact audio sample
  positions, current typewriter progress and native UI focus are not serialized.
  Debug jumps are inspection aids and do not reconstruct all route prerequisites.

## Validation scope

`SummerRadish.Formats` covers malformed input, compression, UTF-16, XML,
control flow, read flags, native image blend arithmetic, animation records,
map layout/navigation and transition weights. `SummerRadish.Retail` parses all
167 scripts, decodes all 369 picture resources, and follows one deterministic
script route through 1,804 messages and 56 choices, including the credit-script
control flow; native presentation is not exercised by that route test.

`SummerRadish.Engine` exercises warning/logo/title, uniform selection API,
first dialogues, native audio paths, save/load, pause, first two-option branch,
a native day-map save/load, and progression from the final prologue block to
real opening-movie playback. It does not prove all route outcomes or mouse
confirmation variants. `SUMMER_CAPTURE_DIR` writes local BMP test captures.
`SummerRadish.Boot` checks 120 frames. Retail tests require `SUMMER_DATA_DIR`.

Additional retail route audit (`tests/RouteTest.cpp`, standalone): 24 deterministic
policies across three uniforms reached an ending, visiting 83 scripts, 43,107
messages and 1,113 choices. Map destinations are selected only from assigned
FILE_ script variables (cafe uses its native result table). This checks XML
branching and dynamic calls, not presentation, every route, or every possible
map input. Compile with the Formats sources; it is not yet a CTest target.

Native name-entry contract recovered from NameEntry.sdll (SHA-256
`0650077e49f30a58ebb4f1a48f85184c2f6d064618a21833b9aa5b92e8394037`):
InitSystem `10001020`, NAMEENTRY `100019b0`, SETDEFAULTNAME `10001ca0`, dialog
`10001110`. Evidence is under `.research/summer/name-entry-init.json` and the
corresponding `rea-0x*.json` files. Numeric XML entities now decode before
substitution of the native U+F8F0/U+F8F1 player-name markers (366 retail uses).
The engine regression edits actual names, rejects empty names without advancing,
and verifies that the native name variables enter the dialogue save.

## Message interface fidelity follow-up

The MESSFRAME.CMAP hit mask and MESSAGE.INI positions now drive the native
skip/backlog/auto/system/hide/quicksave controls. Text selections use the
SELECT.TEXB three-slice geometry, 28-pixel font, original read/unread palettes,
conditional visibility and original result IDs. Selection marks the actual
native read resource; loading reconstructs the original SELECT arguments.
These contracts are corroborated by Message.sdll layout/focus routines
`10008bd0` and `100025a0` retained under `.research/summer/ui-evidence`.
This is static contract evidence, not differential Windows frame/timing proof.

The backlog now uses BACKLOG.TEXB/CMAP and the original 800x600 MESSAGE.INI
layout (744-pixel text area, 56-pixel sidebar, 16x8 text margins and
ARGB A0000040 backdrop). Message.sdll `10009b10` loads these fields;
`10007ef0` confirms one-entry rewind/forward, three-entry fast navigation,
selected voice replay and return without advancing the scenario. Evidence:
`.research/summer/ui-evidence/backlog-code-0x1000a7b4.json` and
`backlog-code-0x10007ef0.json`, SHA-256
`bb91bf636351f882a257024d95afe4929cd1411c9ec2a472d747130c6c7bddcc`.
The engine supports installing a native subclass under the shared history
component and recording voice IDs per entry. Summer replays history through
an independent Voice-channel player while story audio is paused; closing the
history stops replay. Engine tests cover native touch controls and script
position preservation. Original Windows text metrics, exact list centering
and differential timing remain unverified; this does not establish full 1:1
presentation parity.
