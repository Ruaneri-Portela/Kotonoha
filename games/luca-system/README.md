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

## Save and resume behavior

`LUCA_SYSTEM_SAVE_3` stores the instruction pointer, numbers, strings
(including whitespace, newlines and binary bytes), flags, GOSUB return
stack, visible dialogue and remaining wait duration. Resuming a save does
not execute additional bytecode before restoring the saved state.

Valid `LUCA_SYSTEM_SAVE_2` files are still readable, but text already
truncated by the old whitespace-delimited format cannot be recovered.
Malformed payloads are rejected before the VM state is applied. Complete
visual-scene restoration and full AIR compatibility still require
retail-data verification.

Dependency-free codec regression test:

```sh
cmake -S . -B build -DBUILD_TESTING=ON -DKOTONOHA_BUILD_LUCA_SYSTEM=ON
cmake --build build --target LucaSystemSaveCodecTests
ctest --test-dir build -R LucaSystem.SaveCodec --output-on-failure
```
