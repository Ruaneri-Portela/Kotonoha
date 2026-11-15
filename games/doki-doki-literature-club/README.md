# Doki Doki Literature Club!

**English (US)** | [Português (Brasil)](README.pt-BR.md)

Ren'Py-compatible scripting integration, media, translation, and optional VNX overlays.

**Status:** Compatibility depends on the installed game/patch and test coverage.

## Build

```sh
cmake -S . -B build -DKOTONOHA_BUILD_DDLC=ON
cmake --build build --target DokiDoki
```

## Game data

Supply original game data (RPA archives and other required resources); CPython is a target-specific dependency.
Use the launcher or pass `--ddlc /path/to/game-data` to the executable. Never commit commercial game files.

[Build guides](../../docs/BUILD_GUIDES.md) · [Port index](../README.md)
