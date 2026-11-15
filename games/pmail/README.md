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

[Concise support status](STATUS.md) · [Detailed port research](PORT_STATUS.md). Historical results are specific to the reference assets/revision and may contain superseded implementation notes.

[Animation research](docs/ANIMATIONS.md), [audio parity](docs/AUDIO_PARITY.md), [textbox](docs/TEXTBOX.md).

[Build guides](../../docs/BUILD_GUIDES.md) · [Port index](../README.md)

## WebAssembly game assets

When choosing **Use HTTP(S)**, the published server tree must include
your original game files, for example:

```text
Pmail/
  game-assets/
    data/
      pack/
        script.dat
        graphic.dat
        sound.dat
        voice.dat
```

Verify that `http://localhost:3000/Pmail/game-assets/data/pack/script.dat`
is reachable (adjust host/port). Kotonoha **does not ship** those game
files. An empty `manifest.json` does **not** supply the game; the web
shell falls back to direct Fetch for known filenames. If a mixed/uppercase
logical name produces a 404, direct Fetch now retries the lowercase URL.
The script and its compressed package index are read through the virtual
filesystem without a physical path.

A successful HEAD on the directories `data` or `data/pack` does not
prove that `data/pack/script.dat` exists; check the actual asset URL.
