# RealLive

**English (US)** | [Português (Brasil)](README.pt-BR.md)

Native engine executing RealLive scenarios, graphics and audio without launching the Windows executable.

**Status:** An initial reference edition has extensive coverage, but 1:1 route and UI parity is unverified.

## Build

```sh
cmake -S . -B build -DKOTONOHA_BUILD_REALLIVE=ON
cmake --build build --target RealLive
```

## Game data

Use an original installation or compatible ISO containing Seen.txt, Gameexe.ini, and resources.
Use the launcher or pass `--reallive /path/to/game-data` to the executable. Never commit commercial game files.

## Detailed research

[Concise support status](STATUS.md) · [Detailed port research](PORT_STATUS.md). Historical results are specific to the reference assets/revision and may contain superseded implementation notes.

[Build guides](../../docs/BUILD_GUIDES.md) · [Port index](../README.md)
