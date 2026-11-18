# Amagami — support summary

**English (US)** | [Português (Brasil)](STATUS.pt-BR.md)

**Implemented/tested scope:**
- Full PS2 disc (`SLPS_259.18`) parsing: 2,308 `SCENARIO.ARC` SCF scripts, 222 `AMAGAMI.IMG` system classes, `GRAPH0..2.ARC` TIM2 textures and `.zb` fragmented character meshes, `SOUND/MUSIC.AFS`, `SOUND/SOUND.AFS`, `SOUND/VOICE.AFS`, and `SOUND/MUSIC.ARC`.
- Sony `IECS` `systemBank.hd`/`systemBank.bd` SPU2 16-byte ADPCM sound-effect bank decoder (all 49 `Vagi` waves `0..48` at 39,000 Hz mono 16-bit PCM, mapped from `se/<0..48>` and MIDI notes `12..60`).
- Exact `SLPS_259.18` Classic (`0x0028e710`, `MC01..MC37` with Arrange vocal/ED fallbacks for tracks `33, 34, 38..40`) and Arrange (`0x0028e7b4`, `M01..M40`) BGM lookup tables, plus `0x0028e6d0` ambient `SOUND.AFS` lookup.
- Complete 252-entry PS2 `Sprite` texture descriptor table (`0x001b5700..0x001b7a70`, including `0x001b74d0` `bgsam/` & `kaiwa/` entries and `0x00289868` weekday variants), actor pose/costume prefix table (`0x001b7a70`), and primitive coverage across `Object`, `Interface`, `Sprite`, `FontChar`, `FontChar2`, `FontCharEx`, `NodeMapIcon`, `ControlPad`, `Music`, and `Sound`.
- Playable original prologue, calendar/campaign node graph, conversation mode, mahjong/minigame scenes, and deterministic replay save/load.

**Limits:** Full 6-heroine multi-route longplay verification across all 2,308 scenario permutations remains ongoing.

The more detailed [PORT_STATUS.md](PORT_STATUS.md) is an investigation log containing original-game analysis, older implementation notes, and tests tied to specific revisions or local assets. Treat it as historical evidence, not a guarantee for every platform.

[Port README](README.md) · [All ports](../README.md)
