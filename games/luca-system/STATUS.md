# LucaSystem / AIR — support summary

**English (US)** | [Português (Brasil)](STATUS.pt-BR.md)

**Implemented/tested scope:** Full 1:1 retail `AIR` (`AIR.exe` Steam) parity verified against all 31 `.PAK` archives and `files/movie/*.webm`:
- 140-entry opcode table (`index 135 = NULL`, `137 = SCENARIO`, `138 = HAIKEI_SET`) verified against `AIR.exe` `.rdata`.
- Canonical chapter entrypoints (`seen170` DREAM New Game, `seen700` SUMMER, `seen163` AIR, `seen800` FIRST SKY) and Title Screen button hitboxes (`EXIT` at button 6, `SELECT STORY`, `MUSIC`).
- Extended 36-byte CZ4 header (`cropX`, `cropY`, `anchorX`, `anchorY`), CZ1 RGBA palette order, CZ2 support, virtual solid layers (`65000` white / `65001` black), and resource-class disambiguation (`voice:<id>`, `bgm:<id>`, `se:<id>`) eliminating all 1,255 numeric ID collisions.
- Recursive-descent C-style expression evaluation (`IFN`, `IFY`, `EQU`), `EQUN` 4-byte immediate assignment, UTF-16LE `$d` `SELECT` choices writing `#6001`, `@Speaker@` / `$n` / `$[base$ruby$]` tag parsing, `MWIN0..4` window switching (`#6002`), persistent `global_system.kns` (`#12000..#19999`), and `SaveCodec` round-trip saves.

**Limits:** Evidence comes from retail `AIR` and does not establish compatibility for uninspected LucaSystem releases.

The more detailed [PORT_STATUS.md](PORT_STATUS.md) and [docs/AIR_REA_2026_11_18.md](docs/AIR_REA_2026_11_18.md) contain original-game binary analysis and historical notes.

[Port README](README.md) · [All ports](../README.md)
