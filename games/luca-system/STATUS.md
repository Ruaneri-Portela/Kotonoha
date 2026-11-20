# LucaSystem / AIR — support summary

**English (US)** | [Português (Brasil)](STATUS.pt-BR.md)

**Implemented/tested scope:** Retail `AIR` (`AIR.exe` Steam), with 31 mounted `.PAK` archives and `files/movie/*.webm`:
- 140-entry opcode table (`index 135 = NULL`, `137 = SCENARIO`, `138 = HAIKEI_SET`) verified against `AIR.exe` `.rdata`.
- Canonical chapter entrypoints (`seen170` DREAM New Game, `seen700` SUMMER, `seen163` AIR, `seen800` FIRST SKY) and Title Screen button hitboxes (`EXIT` at button 6, `SELECT STORY`, `MUSIC`).
- Extended 36-byte CZ4 header (`cropX`, `cropY`, `anchorX`, `anchorY`), CZ1 RGBA palette order, virtual solid layers (`65000` white / `65001` black), and resource-class disambiguation (`voice:<id>`, `bgm:<id>`, `se:<id>`) eliminating all 1,255 numeric ID collisions.
- CZ2 decoding follows LuckSystem's variable-width LZW and BGRA palette, with block-local padding as in the Yoremi fork. The shared `VNTextBox` uses the original `info27` metrics and `ゴシック27` atlas for dialogue and speaker names. Runs with unsupported characters or an unavailable atlas fall back to the shared UI font.
- The provider reads both direct Ogg voices and OggPak bundles. `voice:<id>` selects clip zero, and `voice:<id>:<index>` selects a zero-based clip. The native audio loader supports replacements at `voice/<id>/<index>.ogg` in the asset overlay.
- Recursive-descent C-style expression evaluation (`IFN`, `IFY`, `EQU`), `EQUN` 4-byte immediate assignment, UTF-16LE `$d` `SELECT` choices writing `#6001`, `@Speaker@` / `$n` / `$[base$ruby$]` tag parsing, `MWIN0..4` window switching (`#6002`), persistent `global_system.kns` (`#12000..#19999`), and `SaveCodec` round-trip saves.

- `LucaPak` imports replacement payloads by name or ID and rebuilds named and numeric PAKs without changing the original archive.
- `LucaPak patch-air` imports variable-length English `MESSAGE` text and relocates known local and cross-scenario jumps. Verified with a patched retail `SCRIPT.PAK` mounted through Kotonoha's asset overlay.
- `LucaPak edit-glyph` patches existing 8-bit CZ2 glyphs and their metrics in repacked PAKs. The `VNTextBox` rendered the edited glyph and advance with the retail AIR font overlay.
- `LucaPak add-glyph` adds an unmapped BMP character, growing the CZ2 atlas when necessary. The AIR `VNTextBox` rendered a new `U+E000` with the imported advance and Kotonoha's configured dialogue and speaker colors.
- The pause menu, manual and history use original AIR art. Save slots, settings, input capture and history voice replay use Kotonoha components.

**Limits:** The original CG gallery and the original layouts of the save and settings screens are not implemented. Direct Ogg voices were verified with retail `AIR`; OggPak bundles were verified with synthetic data, not another retail title. Reference features still to port or verify include rebuilding fonts from scratch, script import beyond AIR English dialogue, and title-specific handlers outside `AIR`.

The more detailed [PORT_STATUS.md](PORT_STATUS.md) and [docs/AIR_REA_2026_11_18.md](docs/AIR_REA_2026_11_18.md) contain original-game binary analysis and historical notes.

[Port README](README.md) · [All ports](../README.md)
