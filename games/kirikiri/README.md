# Kirikiri2 / KirikiriZ

**English (US)** | [Português (Brasil)](README.pt-BR.md)

XP3 assets and basic KAG dialogue, character, choice, and audio tags.

**Status:** General TJS2 bytecode, external plugins, and some native graphics remain unsupported.

## KAG title and graphics compatibility

The engine-owned fallback title now appears even with no executable `.ks`
scenario, avoiding a blank screen when only TJS2 startup data is available.
When a recognizable picture exists in a standard format, it is drawn
behind the menu. KAG `[bg storage=...]`, `[image layer=... storage=...]`,
`[freeimage]` and hiding `[layopt]` now affect actual image layers.
XP3 UTF-16 filenames (including Japanese characters) are decoded correctly.

**Limitations:** This is not the original game's TJS title/menu. Kirikiri
normally begins with `startup.tjs`; full TJS2 execution, complex compiled
SCN/PSB, authentic transitions, TLG5/TLG6 decoding and game-specific filters
are not implemented. PNG/JPEG/BMP/WebP use the existing decoder.
Opening the fallback menu does not prove NEKOPARA playability.

Test with synthetic data:

```sh
cmake -S . -B build -DKOTONOHA_BUILD_KIRIKIRI=ON -DBUILD_TESTING=ON
cmake --build build --target KirikiriXp3Tests Kirikiri
ctest --test-dir build -R '^KirikiriXp3

```sh
cmake -S . -B build -DKOTONOHA_BUILD_KIRIKIRI=ON
cmake --build build --target Kirikiri
```

## Archive compatibility

The XP3 provider now resolves paths with Windows or Unix separators and
unique ASCII case-insensitive matches while rejecting ambiguous folded paths.
Malformed MDF/zlib streams report an error instead of endlessly retrying
decompression. Synthetic tests cover these behaviors.

This improves the reusable Kirikiri2/Z asset layer. It does **not** implement
the general TJS2 runtime or all compiled PSB/SCN behaviors, so support for
complex commercial Kirikiri titles must not be inferred from successful archive
mounting or text extraction.

## Game data

Use a lawful game folder with .ks scripts or an .xp3 archive.
Use the launcher or pass `--kirikiri /path/to/game-data` to the executable. Never commit commercial game files.

[Build guides](../../docs/BUILD_GUIDES.md) · [Port index](../README.md)
 --output-on-failure
```

## Build

```sh
cmake -S . -B build -DKOTONOHA_BUILD_KIRIKIRI=ON
cmake --build build --target Kirikiri
```

## Archive compatibility

The XP3 provider now resolves paths with Windows or Unix separators and
unique ASCII case-insensitive matches while rejecting ambiguous folded paths.
Malformed MDF/zlib streams report an error instead of endlessly retrying
decompression. Synthetic tests cover these behaviors.

This improves the reusable Kirikiri2/Z asset layer. It does **not** implement
the general TJS2 runtime or all compiled PSB/SCN behaviors, so support for
complex commercial Kirikiri titles must not be inferred from successful archive
mounting or text extraction.

## Game data

Use a lawful game folder with .ks scripts or an .xp3 archive.
Use the launcher or pass `--kirikiri /path/to/game-data` to the executable. Never commit commercial game files.

[Build guides](../../docs/BUILD_GUIDES.md) · [Port index](../README.md)
