# Pure Mail port status

The port runs the title and PRO1 gameplay, but **complete original-game parity
is not established**. Do not treat the smoke tests as a complete-route pass.

## Implemented and tested

- Separate Session and component lifecycles; 800×600 scene composition.
- Startup now requires the original pmail.obj/start.obj pair, initializes
  the VM PRNG with the retail seed 1 and recognizes all four original movie
  variants, including PM01L_I3. Pmail.Startup checks these without game assets.
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
- Computer display groundwork: SYS 52 positioned text, SYS 80 clock,
  SYS 160/162 chat accumulation (15-entry limit), SYS 161 reset and SYS 163
  redraw. Scene transitions now clear computer overlays, avoiding ghost text
  over the visual-novel dialogue. No commercial assets are needed for tests.
- A native pre-New-Game customization panel accepts $NO and $HO, expands
  their tokens in dialogue, speakers, choices and chat, and persists them
  in a player profile and version 4 game saves. Versions 1-3 remain readable.
- Save-name/rename TextInputDialog now gets input and renders above the save
  screen. Reopening saves restores its scene registration after closing.
- Message window now reads MesWin_R/G/B from OF_ADV2.INI (defaults 0/0/150),
  preserves original CHAR alpha and exposes diagnostics through
  PMAIL_MESWIN_R/G/B. Default text uses darker colors for light backing;
  user text-color overrides are respected.
- Game art and title textures now use the engine's configurable upscaling
  pipeline (factor > 1; Linear, MetalFX or MPV/OpenGL where available).

## Remaining parity work

- Computer UI fidelity: original font palette, text layout/wrapping, mail and
  icon commands (SYS 50, 176–179, 192–195), plus remaining scene-specific
  presentation details.
- Original CPU/configuration-dependent AVI selection, timing/composition,
  pre-Overflow animation, logo presentation and full movie/title sequence.
  A matching original-game animation trace and legal assets are needed to
  reproduce the missing pre-Overflow effect without guessing a sequence.
- Exact textbox typography, original configured color interpretation,
  name-entry appearance and complete original menu/backlog visual behavior.
- CURSOR02 automatic advance mode and full translation-export workflow.
- Sample-exact fade/lip scheduling and synchronized audio/visual comparison
  against a running original executable.
- Traversal and verification of every route and ending.

## Evidence and reproduction

[ANIMATIONS.md](docs/ANIMATIONS.md) records original addresses, formats,
presets and test coverage. [AUDIO_PARITY.md](docs/AUDIO_PARITY.md) records codec
and native audio findings. See [README.md](README.md) for build and CTest commands.
