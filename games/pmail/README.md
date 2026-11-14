# Pure Mail

**English (US)** | [Português (Brasil)](README.pt-BR.md)

Native DVD engine port with PRO1 story progression, original animation decoding, audio, and saves.

**Status:** Computer UI opcodes, transition fidelity, and complete endings still need verification.

## Build

```sh
cmake -S . -B build -DKOTONOHA_BUILD_PMAIL=ON
cmake --build build --target Pmail
```

## Game data

Use the original PMAILDVD directory, with data/pack/*.dat and data/movie.
Use the launcher or pass `--pmail /path/to/game-data` to the executable. Never commit commercial game files.

## Detailed research

[Original port status and evidence](PORT_STATUS.md). Historical results are specific to the reference assets/revision and may contain superseded implementation notes.

[Animation research](docs/ANIMATIONS.md), [audio parity](docs/AUDIO_PARITY.md), [textbox](docs/TEXTBOX.md).

[Build guides](../../docs/BUILD_GUIDES.md) · [Port index](../README.md)
