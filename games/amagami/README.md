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

[Concise support status](STATUS.md) · [Detailed port research](PORT_STATUS.md). Historical results are specific to the reference assets/revision and may contain superseded implementation notes.

[Build guides](../../docs/BUILD_GUIDES.md) · [Port index](../README.md)

## Amagami in WebAssembly

Select **Use HTTP(S)** to open a legally supplied PS2 disc image using
on-demand HTTP byte ranges. Serve the file at `game-assets/Amagami.bin`,
or use a recognized alternate name (`Amagami.iso` or
`Amagami (Japan).iso`). The build-time `manifest.json` can be empty:
direct HTTP discovery uses HEAD and Range instead of the manifest.
Do not package retail game data into the repository.

In WASM, `SCENARIO.ARC` is read per scene instead of holding the whole
archive in linear memory. Initial scene enumeration can still take time over
a remote connection; full game compatibility is not claimed.

Check the served disc without downloading it:

```sh
python3 tools/check_wasm_disc.py \
  "http://localhost/games/wasm/Amagami/game-assets/Amagami.bin" \
  --local /var/www/games/wasm/Amagami/game-assets/Amagami.bin
python3 -m unittest discover -s tools -p 'test_check_wasm_disc.py'
```

The HTTP host must serve HEAD with `Content-Length` and
`Accept-Ranges: bytes`, plus valid `206 Partial Content` responses to
GET with a `Range` header. Ensure the Nginx user can read the file and
traverse all ancestor directories. A symlink whose target points back to
itself is a loop, not a usable disc image.
