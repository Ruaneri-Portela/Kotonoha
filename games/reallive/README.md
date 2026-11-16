# RealLive

**English (US)** | [Português (Brasil)](README.pt-BR.md)

Native engine executing RealLive scenarios, graphics and audio without launching the Windows executable.

**Status:** An initial reference edition has extensive coverage, but 1:1 route and UI parity is unverified.

## Build

```sh
cmake -S . -B build -DKOTONOHA_BUILD_REALLIVE=ON
cmake --build build --target RealLive
```

## Game data

Use an original installation or compatible ISO containing Seen.txt, Gameexe.ini, and resources.
Use the launcher or pass `--reallive /path/to/game-data` to the executable. Never commit commercial game files.

## Compatibility work in this branch

- VFS/ISO-backed SEEN archives now retain Gameexe.ini's REGNAME when
  selecting the RealLive script format. This fixes a concrete startup
  incompatibility affecting Full Voice-style second-level-encoded scripts.
- Title input and return-to-menu commands now use the configurable SEEN_MENU
  scenario instead of assuming that every game uses 9032.
- Empty/truncated synthetic compressed scripts receive bounds checks.

These changes aim to improve CLANNAD startup and planetarian's front-end
behavior. **They do not demonstrate complete playability:** both games still
need tests against the relevant editions' original content, including choices,
graphics, audio, saves and route transitions.

## Detailed research

[Concise support status](STATUS.md) · [Detailed port research](PORT_STATUS.md). Historical results are specific to the reference assets/revision and may contain superseded implementation notes.

[Build guides](../../docs/BUILD_GUIDES.md) · [Port index](../README.md)

## VM call/return safeguards

Local and cross-scenario calls now enforce a return-stack depth limit.
Missing target scenarios do not leave phantom return frames; invalid
returns keep the pending frame until validation succeeds. The session test
keeps its assertions enabled in `Release`, but requires original RealLive
game data.
