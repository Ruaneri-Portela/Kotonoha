# Zero no Tsukaima

**English (US)** | [Português (Brasil)](README.pt-BR.md)

PS2 archive, KAG-style scene, media and menu support. The Nocturne port is playable; Concerto and Symphony coverage differs.

**Status:** Not all three games or routes have complete interpreter/rendering parity.

## Build

```sh
cmake -S . -B build -DKOTONOHA_BUILD_ZERO=ON
cmake --build build --target ZeroNoTsukaima
```

## Game data

Use your own compatible original disc image.
Use the launcher or pass `--zero /path/to/game-data` to the executable. Never commit commercial game files.

## Detailed research

[Concise support status](STATUS.md) · [Detailed port research](PORT_STATUS.md). Historical results are specific to the reference assets/revision and may contain superseded implementation notes.

[Build guides](../../docs/BUILD_GUIDES.md) · [Port index](../README.md)
