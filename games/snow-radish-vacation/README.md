# Snow Radish Vacation!!

**English (US)** | [Português (Brasil)](README.pt-BR.md)

Playable native ADV3 port with graphics, audio, map, dialogue, and save components.

**Status:** Effect timing and complete scenario parity remain under investigation.

## Build

```sh
cmake -S . -B build -DKOTONOHA_BUILD_SNOW=ON
cmake --build build --target SnowRadish
```

## Game data

Supply the original DATA/PACK archives, including SCRIPT.DAT.
Use the launcher or pass `--snow /path/to/game-data` to the executable. Never commit commercial game files.

## Detailed research

[Original port status and evidence](PORT_STATUS.md). Historical results are specific to the reference assets/revision and may contain superseded implementation notes.

[Build guides](../../docs/BUILD_GUIDES.md) · [Port index](../README.md)
