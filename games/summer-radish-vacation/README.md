# Summer Radish Vacation!!

**English (US)** | [Português (Brasil)](README.pt-BR.md)

Native port using the original CMP archives, with dedicated script execution and UI.

**Status:** Complete original-game behavior and all routes have not been certified.

## Build

```sh
cmake -S . -B build -DKOTONOHA_BUILD_SUMMER=ON
cmake --build build --target SummerRadish
```

## Game data

Provide the original CMP archives from a lawful game installation.
Use the launcher or pass `--summer /path/to/game-data` to the executable. Never commit commercial game files.

## Detailed research

[Concise support status](STATUS.md) · [Detailed port research](PORT_STATUS.md). Historical results are specific to the reference assets/revision and may contain superseded implementation notes.

[Build guides](../../docs/BUILD_GUIDES.md) · [Port index](../README.md)
