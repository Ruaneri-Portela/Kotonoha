# LucaSystem / AIR — support summary

**English (US)** | [Português (Brasil)](STATUS.pt-BR.md)

**Implemented/tested scope:** Retail `AIR` (`AIR.exe` Steam), with 31 mounted `.PAK` archives and `files/movie/*.webm`:
- 140-entry opcode table (`index 135 = NULL`, `137 = SCENARIO`, `138 = HAIKEI_SET`) verified against `AIR.exe` `.rdata`.
- Canonical chapter entrypoints (`seen170` DREAM, `seen700` SUMMER, `seen163` AIR, `seen800` FIRST SKY). The title slides between two pages, and `NEW GAME` opens the original story selector.
- The copyright notice precedes the three silent startup logos. The title plays `nami` (`200014`, `SE.PAK`), matched against [Steam footage](https://www.youtube.com/watch?v=Xfal9fQxdxI). DREAM plays `trk22` (`100021`, Reminiscences) through the script's `BGM` command.
- Extended 36-byte CZ4 header (`cropX`, `cropY`, `anchorX`, `anchorY`), CZ1 RGBA palette order, virtual solid layers (`65000` white / `65001` black), and resource-class disambiguation (`voice:<id>`, `bgm:<id>`, `se:<id>`) eliminating all 1,255 numeric ID collisions.
- CZ2 decoding follows LuckSystem's variable-width LZW and BGRA palette, with block-local padding as in the Yoremi fork. The shared `VNTextBox` uses the original `info27` metrics and `ゴシック27` atlas for dialogue and speaker names. Runs with unsupported characters or an unavailable atlas fall back to the shared UI font.
- The provider reads both direct Ogg voices and OggPak bundles. `voice:<id>` selects clip zero, and `voice:<id>:<index>` selects a zero-based clip. The native audio loader supports replacements at `voice/<id>/<index>.ogg` in the asset overlay.
- Recursive-descent C-style expression evaluation (`IFN`, `IFY`, `EQU`), `EQUN` 4-byte immediate assignment, UTF-16LE `$d` `SELECT` choices writing `#6001`, `@Speaker@` / `$n` / `$[base$ruby$]` tag parsing, `MWIN0..4` window switching (`#6002`), persistent `global_system.kns` (`#12000..#19999`), and `SaveCodec` round-trip saves.

- `LucaPak` imports replacement payloads by name or ID and rebuilds named and numeric PAKs without changing the original archive.
- `LucaPak patch-raw` imports multiple same-size instruction payload edits across any structurally valid LucaSystem script without moving offsets, with synthetic rejection tests; non-AIR VM execution remains separate.
- `LucaPak patch-air` imports variable-length English `MESSAGE` text and relocates known local and cross-scenario jumps. Verified with a patched retail `SCRIPT.PAK` mounted through Kotonoha's asset overlay.
- `LucaPak edit-glyph` patches existing 8-bit CZ2 glyphs and their metrics in repacked PAKs. The `VNTextBox` rendered the edited glyph and advance with the retail AIR font overlay.
- `LucaPak add-glyph` adds an unmapped BMP character, growing the CZ2 atlas when necessary. The AIR `VNTextBox` rendered a new `U+E000` with the imported advance and Kotonoha's configured dialogue and speaker colors.
- The pause menu, manual and history use original AIR art. Save slots, settings, input capture and history voice replay use Kotonoha components. The dialogue window has working Menu, Skip, Auto and Voice buttons.
- Story choices use the original `SELWIN_s` and `SELWIN` art on Kotonoha's `ChoiceQuestionScene`. The original date badges in the upper-left corner follow `#6003`. Script waits freeze on pause; effects in different script channels can play together, and the opening uses its embedded audio.
- Save and Load show the original AIR backgrounds, card frames and paging art. `VNSaveMenu` owns selection and slot actions. The AIR VM stores its state and a story screenshot together in the native `.vn` envelope, without requiring a Kotonoha timeline node.
- Options uses the original AIR background, English tab atlas, text preview frame and slider sprites. Kotonoha's `SettingsMenu` still owns input, live audio, apply/cancel and persistence.
- The title's **Gallery** opens the 72 slots and grouped variants from `_cggallery` using original AIR backgrounds, category art, thumbnails and cursors. The VM stores each drawn CG unlock in `global_system.kns`; unrevealed variants stay locked across sessions.

The LucaInspect tool additionally supports external LuckSystem OPCODE.txt name catalogs (BOM/CRLF) for non-AIR inspection, with an SDL-free C++17 regression test. This does not execute non-AIR opcode handlers.

Save V6 preserves group/slot z-order, tint, transparency, scale, rotation and resumable MOVE offsets for each visual layer, with compatibility fallback for V2–V5 and synthetic malformed-state tests. This fixes a load-time visual discrepancy previously caused by reconstructing groups from asset ID ranges.

CZ4 facial overlays now use the parent portrait's canvas anchor and shared rotation pivot for a consistent pose even when the face asset has a different local anchor. Synthetic geometry tests verify expressions remain aligned during movement and scaling.

Implicit-position portrait replacements inherit the existing character transform; CZ4 expressions inherit body alpha/rotation/scale and active MOVE. Interrupted MOVE commands now start from the displayed position instead of snapping to their previous target. Synthetic C++17 tests cover these cases.

Obsolete scene images and GPU textures are now pruned after scene commits and completed transitions while keeping staged, visible and outgoing image sets alive. Title/Load resets drop stale transition state, with SDL-free cache lifetime regressions.

The renderer also uses atomic SDL3 RGBA texture uploads for sprites, dialogue assets, title art and splash logos, skips invalid sprite geometry and throttles repeated missing-image decodes. The upload path is covered by SDL3 software pixel tests.

The actual GPU compositor now has an SDL3 software-rendered pixel regression for DRAW/WIPE, validating midpoint colors, clip restoration and texture-alpha restoration. This is in addition to standalone scene geometry tests.

Opaque AIR backdrops are now retained below transparent or staging-only scenes across INIT/DRAW; a new opaque full-size image replaces the previous backdrop. This targets seen170 black backgrounds and cutout holes, with synthetic C++17 tests pending retail screenshot verification.

The renderer composes each expression fragment immediately after its matching portrait. DRAW now crossfades complete GPU scene frames (rather than per-sprite alpha) and WIPE uses disjoint scissor regions to prevent overlapping transparent layers, with SDL-free z-order/clip regression tests. Native gameplay text stays on the shared UI renderer.

**Limits:** Options uses Kotonoha's settings and input help rather than all the original per-device editors. Direct Ogg voices were verified with retail `AIR`; OggPak bundles were verified with synthetic data, not another retail title. Reference features still to port or verify include rebuilding fonts from scratch, script import beyond AIR English dialogue, and title-specific handlers outside `AIR`.

The more detailed [PORT_STATUS.md](PORT_STATUS.md) and [docs/AIR_REA_2026_11_18.md](docs/AIR_REA_2026_11_18.md) contain original-game binary analysis and historical notes.

[Port README](README.md) · [All ports](../README.md)
