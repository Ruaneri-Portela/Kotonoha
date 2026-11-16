# Kirikiri2 / KirikiriZ

**English (US)** | [Português (Brasil)](README.pt-BR.md)

XP3 assets and basic KAG dialogue, character, choice, and audio tags.

**Status:** General TJS2 bytecode, external plugins, and some native graphics remain unsupported.

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
