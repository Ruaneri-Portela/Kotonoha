# School Days HQ

**English (US)** | [Português (Brasil)](README.pt-BR.md)

Original media/script playback, native menus, save flow, and focused retail-parity work.

**Status:** Retail parity and complete route/ending validation are still incomplete.

## Build

```sh
cmake -S . -B build -DKOTONOHA_BUILD_SCHOOL_DAYS=ON
cmake --build build --target SchoolDays
```

## Game data

Provide the original licensed GPK packs. The custom Windows Display Options UI uses separate user-authored artwork.
Use the launcher or pass `--school-days /path/to/game-data` to the executable. Never commit commercial game files.

## Detailed research

[Original port status and evidence](PORT_STATUS.md). Historical results are specific to the reference assets/revision and may contain superseded implementation notes.

[Research index](docs/README.md) and [custom Display Options assets](display-options/README.md).

[Build guides](../../docs/BUILD_GUIDES.md) · [Port index](../README.md)
