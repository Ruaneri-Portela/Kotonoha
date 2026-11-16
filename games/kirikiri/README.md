# Kirikiri2 / KirikiriZ

**English (US)** | [Português (Brasil)](README.pt-BR.md)

Kotonoha's C++17 KiriKiri port: XP3 asset mounting, an experimental KAG interpreter, and basic image, dialogue, menu, choice and audio presentation.

## Compatibility currently implemented

- XP3 archives use a virtual provider. The reader supports canonical raw and zlib-compressed indexes, chained indexes, multiple raw/zlib file segments, Unicode names, case-insensitive ASCII lookups and layered patch precedence. Individual files are extracted on demand.
- Standard SDL/FFmpeg image formats and **TLG5 RGB/RGBA** images can be decoded, including TLG5 inside a TLG0 structured-data wrapper. The TLG5 implementation has synthetic pixel, LZSS and truncation tests.
- KAG commands including `[bg storage=...]`, `[image layer=... storage=...]`, `[freeimage]` and `[layopt visible=false]` update renderer image layers. Some transitions are displayed immediately rather than using original timing.
- A **Kotonoha fallback title menu**, with optional compatible title image, appears even when a game contains no executable `.ks` scenario. It explains the missing script support rather than presenting a silent blank screen.
- Damaged indexes, invalid segment ranges and cyclic index chains are rejected. A failed archive auto-mount is logged without suppressing unrelated XP3s.

## Known limitations

**A fallback title is not the original NEKOPARA menu.** The original KiriKiri startup path executes `startup.tjs` and depends on the TJS2 runtime, plugins and title-specific UI. This port does not yet execute general TJS2 or all compiled PSB/SCN semantics. **TLG6 images and game-specific XP3 encryption/filter algorithms remain unsupported.** Support for save states, effects, translations and audio is not equivalent to an original retail game.

These changes have not been validated against a commercial NEKOPARA installation; successful XP3 indexing or a fallback title does not establish full playability. Only synthetic data is used in repository tests.

## Build and test

```sh
cmake -S . -B build -DKOTONOHA_BUILD_KIRIKIRI=ON -DBUILD_TESTING=ON
cmake --build build --target KirikiriXp3Tests Kirikiri
ctest --test-dir build -R '^KirikiriXp3$' --output-on-failure
```

Use a lawful game folder with KAG scripts or XP3 packages. Start the launcher or pass `--kirikiri /path/to/game-data`. Do not commit commercial assets.

[Build guides](../../docs/BUILD_GUIDES.md) · [Port index](../README.md)
