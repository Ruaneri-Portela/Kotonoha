# Pure Mail port status

The port runs the title and PRO1 gameplay, but **complete original-game parity
is not established**. Do not treat the smoke tests as a complete-route pass.

## Implemented and tested

- Separate Session and component lifecycles; 800×600 scene composition.
- Original CHAR sprites, BGR IMG white key/orientation and waku placement.
- All DVD ANIM assets decode. SYS 51 plays all 19 presets with original native
  timing, suspends the VM, retains the final image and preserves playback in saves.
- Character/background mouth regions follow actor IDs and the decoded voice;
  the normal text cursor uses its original ANIM frames.
- ADPS/ADP4 audio, separate BGM/SE/voice buses, loop blocks, fades, crossfades,
  effective dubbing assets and pause/save state.
- VM/host save/load with validation, backlog persistence, engine save slots and
  return to title. Translation lookup and current audio/assets are connected to
  the engine tools. Debug node display includes active animation/frame.
- Computer display groundwork: SYS 52 positioned text, SYS 80 clock, and
  SYS 160/162 chat accumulation (15-entry limit), SYS 161 reset and SYS 163
  redraw. UI is composited in the native 800×600 scene. Computer UI state
  persists in version 3 game saves; version 1/2 saves remain readable.
  Synthetic opcode tests do not require commercial game data.

## Remaining parity work

- Computer UI fidelity: original font palette, text layout/wrapping, mail and
  icon commands (SYS 50, 176–179, 192–195), plus remaining scene-specific
  presentation details.
- Original transition timing/composition and exact startup/logo/movie sequence.
- Exact textbox typography and complete original menu/backlog visual behavior.
- CURSOR02 automatic advance mode and full translation-export workflow.
- Sample-exact fade/lip scheduling and synchronized audio/visual comparison
  against a running original executable.
- Traversal and verification of every route and ending.

## Evidence and reproduction

[ANIMATIONS.md](docs/ANIMATIONS.md) records original addresses, formats,
presets and test coverage. [AUDIO_PARITY.md](docs/AUDIO_PARITY.md) records codec
and native audio findings. See [README.md](README.md) for build and CTest commands.
