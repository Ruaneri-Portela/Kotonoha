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

## GPK index compatibility

The GPK reader now accepts zero padding after a decoded PIDX terminator and
unused physical gaps between archive entries or before the index. It continues
to reject overlapping entry ranges, nonzero terminator garbage, and out-of-bounds
data. This behavior was cross-checked against the MIT-licensed
[DaysEngine GPK reader](https://github.com/superboo07/DaysEngine/tree/master/crates/days-gpk)
without introducing a Rust dependency.

These regression tests use synthetic archives. Compatibility with other
FILMEngine games (Shiny Days/Cross Days) is **not** implied: key discovery, pack
layout and runtime differences remain separate work. Retail-game verification
must still be performed with the user's own legally obtained data.

## Detailed research

[Concise support status](STATUS.md) · [Detailed port research](PORT_STATUS.md). Historical results are specific to the reference assets/revision and may contain superseded implementation notes.

[Research index](docs/README.md) and [custom Display Options assets](display-options/README.md).

[Build guides](../../docs/BUILD_GUIDES.md) · [Port index](../README.md)
