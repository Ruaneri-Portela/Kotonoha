# PONscripter (`Saya no Uta PT-BR`)

**English (US)** | [Português (Brasil)](README.pt-BR.md)

Clean-room C++17 PONscripter (`20090118` / `20091015`) virtual machine, SAR/NSA archive reader, proportional text formatter, and SDL3 runtime port for *Saya no Uta* (`SayaNoUta_PT-BR`).

## Build

```sh
cmake -S . -B build -DKOTONOHA_BUILD_PONSCRIPTER=ON
cmake --build build --target PonscripterGame
```

## Browser and HTTP(S) game data

The WebAssembly build uses the same HTTP(S) Fetch-backed virtual filesystem as
other ports. Without an HTTP directory listing or `manifest.json`, detection
probes known names such as `00.utf`, `0.utf`, `arc.nsa` and `arc.sar` through
HEAD/Range requests. Loose scripts and media can then be read on demand.
Sparse script sets with unusual filenames may still need a manifest or local
folder. Unlike the XP3 provider, the current SAR/NSA reader loads an archive
into memory, so very large archives can exceed browser memory limits.
A successful source probe does not guarantee that a full title runs.

See [WebAssembly assets](../../docs/BUILD_WEB.md) for HTTP Range/CORS
requirements and the manual **Use HTTP(S)** startup option.

## Detailed Status

[Status summary](STATUS.md) | [Status resumo em português](STATUS.pt-BR.md)

[Build guides](../../docs/BUILD_GUIDES.md) · [Port index](../README.md)
