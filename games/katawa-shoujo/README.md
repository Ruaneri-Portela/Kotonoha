# Katawa Shoujo

**English (US)** | [Português (Brasil)](README.pt-BR.md)

Katawa Shoujo uses the shared Kotonoha Ren'Py runtime and RPA asset provider. Standalone CMake target: `KatawaShoujo`; bundle identifier: `me.hirameki.kotonoha.katawashoujo`.

## Build and start

Configure with `-DKOTONOHA_BUILD_KATAWA=ON` and build `KatawaShoujo`. Provide legally obtained game data using `--katawa DIRECTORY|ISO`, `KATAWA_DIR`, or the folder launcher. Local assets belong under `games/katawa-shoujo/game-assets/`. The data validator looks for `game/script-a1-monday.rpyc` (or an equivalent selected game subfolder).

## Status

The Ren'Py runtime is shared with DDLC; correct title and bundle identity depend on which game data the provider identifies. CPython setup varies by platform. No complete playthrough or parity certification is implied by a successful build or smoke test.
