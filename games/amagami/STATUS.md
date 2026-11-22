# Amagami — support summary

**English (US)** | [Português (Brasil)](STATUS.pt-BR.md)

**Implemented/tested scope:**
- Full PS2 disc (`SLPS_259.18`) parsing: 2,308 `SCENARIO.ARC` SCF scripts, 222 `AMAGAMI.IMG` system classes, `GRAPH0..2.ARC` TIM2 textures and `.zb` fragmented character meshes, `SOUND/MUSIC.AFS`, `SOUND/SOUND.AFS`, `SOUND/VOICE.AFS`, and `SOUND/MUSIC.ARC`.
- Sony `IECS` `systemBank.hd`/`systemBank.bd` SPU2 16-byte ADPCM sound-effect bank decoder (all 49 `Vagi` waves `0..48` at 39,000 Hz mono 16-bit PCM, mapped from `se/<0..48>` and MIDI notes `12..60`).
- Exact `SLPS_259.18` Classic (`0x0028e710`, `MC01..MC37` with Arrange vocal/ED fallbacks for tracks `33, 34, 38..40`) and Arrange (`0x0028e7b4`, `M01..M40`) BGM lookup tables, plus `0x0028e6d0` ambient `SOUND.AFS` lookup.
- Complete 252-entry PS2 `Sprite` texture descriptor table (`0x001b5700..0x001b7a70`, including `0x001b74d0` `bgsam/` & `kaiwa/` entries and `0x00289868` weekday variants), actor pose/costume prefix table (`0x001b7a70`), and primitive coverage across `Object`, `Interface`, `Sprite`, `FontChar`, `FontChar2`, `FontCharEx`, `NodeMapIcon`, `ControlPad`, `Music`, and `Sound`.
- Playable original prologue, calendar/campaign node graph, conversation mode, mahjong/minigame scenes, and deterministic replay save/load.

**Reference fidelity improvements (open-amagami):**
- The experimental Vita live adapter starts at `GameMain.scriptMain` and
  loads scenario classes only on demand from `SCENARIO.CPK`. The VM keeps
  `Class.findClass` non-loading, while explicit `K2X_Script.loadClass`
  and SCF class literals resolve and cache their requested class.
- Vita `K2X_Script` now supports its initial status/class-loading/frame-clock
  primitives. The live loop uses SDL's monotonic nanosecond timer for 60 Hz
  pacing instead of an approximate 16 ms delay.
- Vita diagnostic input uses Xbox A/B for confirm/cancel and PlayStation
  Circle/Cross on other pads; remapped SDL controllers preserve held state.
- Scenario SCF callers of ControlPad now receive original PS2 button bits,
  while Vita system-image callers receive SceCtrl masks. The VM exposes the
  immediate call origin, with nested-SCF regression coverage.
- The Vita sprite tweens now use the reference's original easing modes:
  1 decelerates and 2 accelerates, with regression checks for both curves.
- The Vita SDL input adapter preserves Circle when Z and Space are held
  simultaneously, allows left-stick D-pad navigation (16000 dead zone),
  clamps analog samples to signed 8-bit values, and uses the reference
  trigger threshold without overwriting physical D-pad input.

**Kotonoha UI integration:**
- The native pause menu (`戻る`, `セーブ`, `ロード`, `タイトルへ`, `設定`) is implemented by `AmagamiMenu`, a subclass of `Kotonoha::InGameMenu` that preserves the original layout and labels.
- Save and Load open `Kotonoha::VNSaveMenu` under `amagami/`; Settings opens the engine settings menu; Title returns through the runtime callback.
- `AmagamiBacklog` continues to render the native LogList through `Kotonoha::VNBacklog`; `Esc`/right-click opens the pause menu and `B`/`PageUp` opens history.
- `AmagamiEngineTest` verifies that `Esc` opens and closes the connected pause menu against the retail disc.

**Limits:** Full 6-heroine multi-route longplay verification across all 2,308 scenario permutations remains ongoing.

The more detailed [PORT_STATUS.md](PORT_STATUS.md) is an investigation log containing original-game analysis, older implementation notes, and tests tied to specific revisions or local assets. Treat it as historical evidence, not a guarantee for every platform.

[Port README](README.md) · [All ports](../README.md)
