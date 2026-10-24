# Doki Doki Literature Club! — port status (proof of concept)

The port runs the original Ren'Py 6.99 game data without Python or Ren'Py:

- `Archive` reads the `.rpa` archives (RPA-3.0, zlib + pickle index).
- `Script` unpickles the compiled `.rpyc` ASTs.
- `PyParse`/`PyExec`/`PyBuiltins` are a small Python 2 interpreter that runs the
  game's `init python` blocks, conditions and expressions.
- `Display` implements displayables, ATL and transitions; `Vm` runs the Ren'Py
  statements (say, menu, show/scene/hide, with, pause, call screen, jump/call…).
- `src/Main.cpp` is the Kotonoha host: FFmpeg image/audio decoding, SDL_ttf text,
  PcmPlayer channels (music → BGM mixer, others → SE), and the DDLC screens drawn
  natively (textbox, namebox, quick menu, choices, main menu, name input,
  confirm dialogs, poem reader and poem game). The game layer renders into a
  Kotonoha canvas so the upscaler and debug UI work as for the other ports.

## Running

```
DokiDoki -r opengl --ddlc /path/to/DDLC-1.1.1-pc/game
```

`DDLC_DIR` can be used instead of `--ddlc`. Persistent data is stored in the
SDL pref path (`Kotonoha/ddlc/persistent.txt`).

The headless check `DdlcScriptTest <game dir> [interactions]` runs the splash and
Act 1 without rendering.

## Working

- Splash/warning, "I agree", Team Salvato intro, animated main menu.
- New Game with name entry, Act 1 dialogue, sprites (composites), backgrounds,
  transitions, music and sound effects.
- Choices, poem game (words, chibi reactions), poem reading screens.
- Skip (Ctrl hold / Tab), auto mode (A), pause menu (Esc / right click).

## Not implemented

- Save/Load, Settings, History and Help screens (a notice is shown).
- `SpriteManager` particles (menu sparkles) are not drawn.
- Glitch/tear/screen effects that need custom Ren'Py renderers.
- Skip does not track already-read lines.
- Acts beyond Act 1 were not verified.
