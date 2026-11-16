# Kirikiri2 / KirikiriZ

**English (US)** | [Português (Brasil)](README.pt-BR.md)

Kotonoha's C++17 KiriKiri port: XP3 asset mounting, an experimental KAG interpreter, and basic image, dialogue, menu, choice and audio presentation.

## Compatibility currently implemented

- XP3 archives use a virtual provider. The reader supports canonical raw and zlib-compressed indexes, chained indexes, multiple raw/zlib file segments, Unicode names, case-insensitive ASCII lookups and layered patch precedence. Individual files are extracted on demand.
- Standard SDL/FFmpeg images, **TLG5 RGB/RGBA** and **TLG6 Golomb-coded images** can be decoded, including TLG0 wrappers. The TLG6 decoder includes bounded Golomb runs, LZSS filter types, color filters and MED/AVG prediction; synthetic tests cover basic pixels, wrappers and malformed inputs.
- KAG commands including `[bg storage=...]`, `[image layer=... storage=...]`, `[freeimage]` and `[layopt visible=false]` update renderer image layers. Some transitions are displayed immediately rather than using original timing.
- A **Kotonoha fallback title menu** appears even when no executable `.ks` exists. In addition to the static scanner, an **experimental bounded TJS2 source subset interpreter** runs variable assignments, arithmetic and string expressions, conditionals, loops and `Scripts.execStorage` / `Scripts.evalStorage` calls across source scripts. This selects a referenced KAG entry ahead of the alphabetical fallback when the user chooses Start; it does not create the native TJS2 menu. Plugin requests are only recorded and never executed.
- Damaged indexes, invalid segment ranges and cyclic index chains are rejected. A failed archive auto-mount is logged without suppressing unrelated XP3s.

## Known limitations

**A fallback title is not the original NEKOPARA menu.** The original KiriKiri startup path executes `startup.tjs` and depends on the TJS2 runtime, plugins and title-specific UI. This port does not yet execute general TJS2 or all compiled PSB/SCN semantics. **TLG6 method 3 raw planes are handled experimentally only when precisely sized. Gamma (method 1), modified LZSS (method 2), and game-specific XP3 filters remain unsupported.** The upstream KiriKiri Z decoder itself rejects non-Golomb TLG6 modes. Support for save states, effects, translations and audio is not equivalent to an original retail game.

Class definitions, methods, native objects/windows, native plugins and compiled TJS2 bytecode **are not implemented**; the subset stops on unhandled syntax, with an explicit diagnostic. These changes have not been validated against a commercial NEKOPARA installation; successful XP3 indexing or a fallback title does not establish full playability. Only synthetic data is used in repository tests.

## Build and test

```sh
cmake -S . -B build -DKOTONOHA_BUILD_KIRIKIRI=ON -DBUILD_TESTING=ON
cmake --build build --target KirikiriXp3Tests Kirikiri
ctest --test-dir build -R '^KirikiriXp3$' --output-on-failure
```

Use a lawful game folder with KAG scripts or XP3 packages. Start the launcher or pass `--kirikiri /path/to/game-data`. Do not commit commercial assets.

[Build guides](../../docs/BUILD_GUIDES.md) · [Port index](../README.md)

## KAG choice order

The supported KAG subset's `[select]`/`[choices]` tags now sort numbered `option1`, `option2`, `option10` attributes numerically rather than lexicographically, and exclude unrelated fields such as `optionColor`. A synthetic regression test checks choice order and VM resume. This does not provide a TJS2 interpreter or prove NEKOPARA's original menu works.
