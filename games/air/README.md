# AIR — Kotonoha native port

Initial native port for the Portuguese AIR Standard Edition distributed as
`SekaiVN - AIR [BR - V2.0]`. Reads the installed game directly; no Windows
executable, Wine, or conversion step is used at runtime.

This is **not yet a complete or verified 1:1 replacement**. See
[PORT_STATUS.md](PORT_STATUS.md) for the tested scope and remaining work.

## Run on this Mac

Build from the Kotonoha repository root:

```sh
cmake -S . -B build -DKOTONOHA_BUILD_AIR=ON
cmake --build build --target Air -j 6
./games/air/run.command
```

The launcher locates this edition in Downloads, bundled `game-assets`, or the
`AIR_DIR` environment variable. An explicit directory takes precedence:

```sh
./games/air/run.command --air '/Users/nero/Downloads/SekaiVN - AIR [BR - V2.0]'
```

An ISO containing `Seen.txt`, `Gameexe.ini` and the resource folders is also
accepted, either with the launcher's **ISO...** button or directly:

```sh
build/games/air/Air.app/Contents/MacOS/Air --air '/Users/nero/Downloads/AIR Standard Edition.ISO'
```

AIR reads ISO9660/Joliet directly through the engine's disc asset provider.
No extraction, temporary copy or operating-system mount is used. Scripts,
images, voices and media are read into memory as requested; overlays still
replace logical asset paths. Installer-only images are not supported.

The application is `build/games/air/Air.app`. Keep the original directory or ISO
in place. The application does not modify the game's files or existing saves.
Kotonoha saves go into its own AIR preference directory.

Click or Enter/Space advances dialogue. Escape opens the Kotonoha pause menu,
whose dark background covers the full 4:3 canvas. Save/load is available during
dialogue. Original RealLive save files are not compatible.

## Validate

```sh
cmake -S . -B build -DAIR_TEST_DATA='/path/to/AIR'
cmake --build build --target AirImagesTests AirVoiceTests AirScriptTests AirSessionTests AirSystemTests AirEngineTests
ctest --test-dir build -R '^Air' --output-on-failure
```

Corpus tests inspect all original images, voice indexing, NWA payloads and all
96 scenarios. The integration test runs the original boot sequence to the title,
then enters scene 170 after its lengthy animated introduction to test Portuguese
dialogue, save/load and pause. This debug entry is not proof of complete route
execution. Synthetic decoder and interpreter tests also ran under ASan/UBSan.

`AirInspect Seen.txt [scenario]` validates or disassembles the native bytecode.
Debug node IDs accept `scenario` or `scenario:instruction`.

Original game and translation assets are not redistributed. Parser provenance
and retained MIT notices are in [vendor/libreallive/ORIGIN.md](vendor/libreallive/ORIGIN.md).
