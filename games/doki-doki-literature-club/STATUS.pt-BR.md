# Doki Doki Literature Club! — resumo de suporte

[English (US)](STATUS.md) | **Português (Brasil)**

**Escopo implementado/testado:**
- **Pacotes e Overlays (`DdlcAssets`, `Archive`):** Contêineres `RPA-3.0` originais (`scripts.rpa`, `images.rpa`, `audio.rpa`, `fonts.rpa`) e `RPE-1.0` do patch VNX (`vnx.rpx`, `scripts.rpa`, `gui.rpa`, `font.rpa`), montagem via `--overlay` / `--asset-overlay`, descoberta prioritária de `vnx/game/` e virtualização do instalador `vnxsurvival.rpyc` (`os.path.isdir`, `os.makedirs`, `os.listdir`, `shutil.copyfile`, `renpy.quit(relaunch=True)`) sem modificar instalações originais somente leitura.
- **VM Ren'Py 6.99 e AST (`Vm`, `Script`):** `Say`, `Show`, `Scene`, `Hide`, `With`, `Jump`, `Call`, `Return`, `Python`, `EarlyPython`, `UserStatement`, `Menu`, `If`, `Label`, `Define`, `Default`, `Image`, `Transform`, `Init`, `Screen`, `Style` (`style.say_dialogue = style.default_monika`) e `ShowLayer` (`show layer master`).
- **Apresentação e Efeitos (`Display`, `DdlcSession`):** Transformações ATL (incluindo `size (w, h)`, `crop`, `rotate`, warpers, `parallel`, `choice`, handlers `on`), `im.Composite`, `LiveComposite`, `im.Crop`, `im.FactorScale`, `im.Flip`, `im.Grayscale`, `AnimatedMask`, `SpriteManager`, `ImageDissolve`, `MultipleTransition`, `Tear`, `Invert`, `rigged_choice`, `poem` (ordenação Z de `poem_dismiss`) e `poemgame`, tags de texto `{size=...}` e fallback por glifo via `TTF_FontHasGlyph` (`y3.ttf` / `RifficFree-Bold.ttf` -> `Aller_Rg.ttf` / `Vera.ttf`).
- **Fluxo, Áudio e Proteções de Save/Load (`DdlcSession`):** Avanço por teclado (`Enter`/`Space`) durante `splashscreen`, transição automática `Phase::Splash -> Phase::Play` em saltos via `persistent.autoload` (`ch30_autoload`, `yuri_kill_2`, `credits`, `postcredits_loop`), tags de áudio (`<from X>`, `<to Y>`, `<loop Z>`, `<silence S>`), listas de reprodução e `fadeout`, proteções `FileActionMod` nos Atos 2 e 3, além de interface traduzida em PT-BR e tela `Créditos` (`screen credits`) do patch VNX.

**Limitações:** Testes automatizados de campanha completa (Atos 1–4) e de apresentação validam o jogo base e o patch PT-BR VNX, mas não substituem validação manual em todas as plataformas.

[README do port](README.pt-BR.md) · [Todos os ports](../README.pt-BR.md)
