# Zero no Tsukaima — support summary

**English (US)** | [Português (Brasil)](STATUS.pt-BR.md)

**Implemented/tested scope:**
- Automatic 3-edition PS2 disc detection and per-edition save/system isolation (`concerto`, `nocturne`, `symphony`) across:
  - **Concerto (`ZeroNoTsukaima0.bin` / `SLPS_257.09`, volume `ZERO`)**: 1,921 plain Squirrel scene scripts (`0000.KS..1920.KS`), 26 `.PSS` movies from `NORMAL.BIN[69]`, layer animations (`LayerVibrateActionModule`, `LayerJumpActionModule`, `LayerWaveActionModule`, `LayerDecelMoveModule`, `LayerAccelMoveModule`), affection (`like`), tsundere meter (`tndl`, `tndlStart`, `tndlEnd`), items (`item`), route/kiss evaluation (`checkRoute`, `checkKiss`, `isMax`), map navigation (`moveInit`, `moveItem`, `move`), date selection (`dateItem`, `date`, `resetSelect`, `resetSelect2`), and turn-based battle hooks (`battleInit`, `battleAdd`, `battleEnemy`, `battle`).
  - **Nocturne (`ZeroNoTsukaima1.bin` / `SLPS_258.30`, volume `ZERO2`)**: 434 compiled `.NUT` KAG scenes, full `SQFunctionProto` footer parsing (`OVERRIDE.NUT` with 230 child functions), `CG*` and `ECG*` gallery unlocking, `notrans`/`stopaction`/`zoom`/`origin` parameter handling, `[時間経過暗転]`, map move (`moveinit`/`moveadd`/`move`), roulette (`tndlslot`), and coffee-break transitions.
  - **Symphony (`ZeroNoTsukaima2.bin` / `SLPS_258.98`, volume `ZERO3`)**: 461 compiled `.NUT` KAG scenes, `SOUND_ID.LST` `.PSH` vs `.STV` resolution (all 27 `BGMB01..BGMB27` stereo BGM streams), `[hideall]`, 3-letter character affection icons (`riz`, `tav`, `sie`, `anl`, `syl`, `tef`, `kyu`, `ere`), `[tndlcheck]` chapter transitions, 18-book Magic Grimoire sub-story selector (`[book]`, `[bookjump]`, `[bookdone]`), `[tndlbingo]`, `multilayers.nut` dual-panel CG composition (`ev30d`, `ev73b`), and `EV*`/`TUN*` gallery unlocking.

**Limits:** Concerto's interactive turn-based battle UI and full multi-route longplay verification across all three editions remain ongoing.

The more detailed [PORT_STATUS.md](PORT_STATUS.md) is an investigation log containing original-game analysis, older implementation notes, and tests tied to specific revisions or local assets. Treat it as historical evidence, not a guarantee for every platform.

[Port README](README.md) · [All ports](../README.md)
