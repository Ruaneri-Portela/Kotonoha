# LucaSystem

**English (US)** | [Português (Brasil)](README.pt-BR.md)

Native PAK/bytecode interpretation and image/audio/text rendering tested against AIR.

**Status:** AIR-specific functionality is documented; do not assume all LucaSystem titles have equivalent coverage.

## Build

```sh
cmake -S . -B build -DKOTONOHA_BUILD_LUCA_SYSTEM=ON
cmake --build build --target LucaSystem
```

## Game data

Use original PAK files (including PARAM.PAK and SCRIPT.PAK); AIR is the reference title.
Use the launcher or pass `--luca-system /path/to/game-data` to the executable. Never commit commercial game files.

## Detailed research

[Concise support status](STATUS.md) · [Detailed port research](PORT_STATUS.md). Historical results are specific to the reference assets/revision and may contain superseded implementation notes.

[Build guides](../../docs/BUILD_GUIDES.md) · [Port index](../README.md)
