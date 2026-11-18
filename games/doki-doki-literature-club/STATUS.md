# Doki Doki Literature Club! — support summary

**English (US)** | [Português (Brasil)](STATUS.pt-BR.md)

**Implemented/tested scope:**
- **Archives & Overlays (`DdlcAssets`, `Archive`):** Retail `RPA-3.0` (`scripts.rpa`, `images.rpa`, `audio.rpa`, `fonts.rpa`) and VNX `RPE-1.0` (`vnx.rpx`, `scripts.rpa`, `gui.rpa`, `font.rpa`) containers, `--overlay` / `--asset-overlay` CLI mounting, `vnx/game/` priority discovery, and virtualized `vnxsurvival.rpyc` (`os.path.isdir`, `os.makedirs`, `os.listdir`, `shutil.copyfile`, `renpy.quit(relaunch=True)`) without mutating read-only retail installations.
- **Ren'Py 6.99 VM & AST (`Vm`, `Script`):** `Say`, `Show`, `Scene`, `Hide`, `With`, `Jump`, `Call`, `Return`, `Python`, `EarlyPython`, `UserStatement`, `Menu`, `If`, `Label`, `Define`, `Default`, `Image`, `Transform`, `Init`, `Screen`, `Style` (`style.say_dialogue = style.default_monika`), and `ShowLayer` (`show layer master`).
- **Presentation & Effects (`Display`, `DdlcSession`):** ATL transforms (including `size (w, h)`, `crop`, `rotate`, warpers, `parallel`, `choice`, `on` handlers), `im.Composite`, `LiveComposite`, `im.Crop`, `im.FactorScale`, `im.Flip`, `im.Grayscale`, `AnimatedMask`, `SpriteManager`, `ImageDissolve`, `MultipleTransition`, `Tear`, `Invert`, `rigged_choice`, `poem` (`poem_dismiss` Z-ordering) and `poemgame`, `{size=...}` rich text tags, and per-glyph `TTF_FontHasGlyph` fallback (`y3.ttf` / `RifficFree-Bold.ttf` -> `Aller_Rg.ttf` / `Vera.ttf`).
- **Flow, Audio & Save/Load Guards (`DdlcSession`):** Keyboard (`Enter`/`Space`) progression through `splashscreen`, automatic `Phase::Splash -> Phase::Play` transition on `persistent.autoload` jumps (`ch30_autoload`, `yuri_kill_2`, `credits`, `postcredits_loop`), audio spec tags (`<from X>`, `<to Y>`, `<loop Z>`, `<silence S>`), track playlists and `fadeout` crossfading, `FileActionMod` save/load guards in Acts 2 and 3, and localized PT-BR UI + VNX `Créditos` (`screen credits`) screen.

**Limits:** Automated full-act playthroughs and presentation tests verify Acts 1–4 and the VNX PT-BR patch, but do not certify every platform-specific edge case.

[Port README](README.md) · [All ports](../README.md)
