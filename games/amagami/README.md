# Amagami

**English (US)** | [Português (Brasil)](README.pt-BR.md)

SCF runtime and game-specific media/presentation. The prologue and an initial event map are playable.

**Status:** Full calendar, conversations, routes, and endings have not been verified.

## Build

```sh
cmake -S . -B build -DKOTONOHA_BUILD_AMAGAMI=ON
cmake --build build --target Amagami
```

## Game data

Provide a legally obtained Amagami PS2 disc/image or extracted game data as supported by the launcher.
Use the launcher or pass `--amagami /path/to/game-data` to the executable. Never commit commercial game files.

## Detailed research

[Original port status and evidence](PORT_STATUS.md). Historical results are specific to the reference assets/revision and may contain superseded implementation notes.

[Build guides](../../docs/BUILD_GUIDES.md) · [Port index](../README.md)
