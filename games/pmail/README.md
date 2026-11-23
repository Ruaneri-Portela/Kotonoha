# Pure Mail

**English (US)** | [Português (Brasil)](README.pt-BR.md)

Native DVD engine port with PRO1 story progression, original animation decoding, audio, and saves.

**Status:** Positioned computer text, clock and chat-history opcodes are now modeled and rendered, with synthetic tests. Original color/layout parity, mail/icon opcodes, transition fidelity and complete endings still need verification.

## Build

```sh
cmake -S . -B build -DKOTONOHA_BUILD_PMAIL=ON
cmake --build build --target Pmail
```

## Game data

Use the original PMAILDVD directory, with data/pack/*.dat and data/movie.
Use the launcher or pass `--pmail /path/to/game-data` to the executable. Never commit commercial game files.

## Customization, saves and upscaling

Choose **New Game** to edit the two names substituted for `$NO` and `$HO`.
Names are stored in a per-user `pmail-names.txt` profile and in game saves.
The save-name dialog now accepts keyboard input and displays above the save
list. Existing `PMAIL-GAME-1/2/3` data remains readable by the port.

The original `OF_ADV2.INI` keys `MesWin_R`, `MesWin_G`, `MesWin_B`
control the tinted window background. Optional
`PMAIL_MESWIN_R/G/B` environment overrides support color comparison.

Artworks and the native title go through the standard Kotonoha upscaler.
Enable a factor above 1.0 in graphics settings; the active backend depends
on the SDL renderer (Linear is the compatible fallback).

The pre-Overflow animation still requires comparison against the running DVD
with its original assets; its exact content and timing are not yet verified.

**Script progression fix:** child scripts that terminate now return to a
live parent interpreter rather than leaving the VM marked as finished.
The synthetic `Pmail.ComputerSave` fixture covers return-to-title through
`SYS 213` and save/load while a child script is active. Lip-animation
frames are constrained to each decoded character region. The complete DVD
story still needs real-asset validation.

**Quick access:** F5 writes `pmail-quick.vn` to the game's save directory,
and F9 loads it. The regular save-slot UI remains available. The keyboard
shortcuts are a cross-platform Kotonoha addition, not verified original keys.

## Story route auditing

After supplying your legally obtained game directory, build the real test
runner with `-DBUILD_TESTING=ON`. `PmailRouteAuditCheck` traverses dialogue,
script branches and animation barriers while printing visited scripts and
native opcode counts. Pass optional zero-based choice indices such as
`0,1,2` and an optional time budget in seconds:

```sh
cmake -S . -B build -DBUILD_TESTING=ON -DKOTONOHA_BUILD_PMAIL=ON \
  -DPMAIL_TEST_ASSET_DIR=/path/to/PMAILDVD
cmake --build build --target PmailRouteAuditCheck
ctest --test-dir build -R 'Pmail.RouteAudit' --output-on-failure
# Or invoke the compiled PmailRouteAuditCheck with:
# /path/to/PMAILDVD 0,1,2 300
```

The CI only runs synthetic tests, including `Pmail.Expression32`,
`Pmail.ComputerSave` and `Pmail.TransitionTrace`. The
retail-only route auditor reports missing objects and unimplemented native
commands as failures; this does not imply any ending is already verified.

## Large PonPon retail gate

`Pmail.LargePonPon.Retail` is labelled `real` and is always registered, but
returns 77 (CTest "Skipped") until a retail directory is supplied via
`LARGEPONPON_TEST_DATA`. The test mounts `PACK/script.dat`, `PACK/GRAPHIC.DAT`
and `PACK/SOUND.DAT`, decodes real graphics and audio, and boots the runtime
headlessly from the title screen into the opening dialogue. The game directory
is only ever read.

```sh
cmake -S . -B build -DBUILD_TESTING=ON -DKOTONOHA_BUILD_PMAIL=ON \
  -DLARGEPONPON_TEST_DATA=/path/to/LargePonPon
cmake --build build --target LargePonPonCheck
ctest --test-dir build -R '^Pmail\.LargePonPon' --output-on-failure
```

Without the gate the same CTest command still runs the synthetic
`Pmail.LargePonPon` test and reports the retail test as skipped.


## Detailed research

[Concise support status](STATUS.md) · [Detailed port research](PORT_STATUS.md). Historical results are specific to the reference assets/revision and may contain superseded implementation notes.

[Startup research](docs/STARTUP.md), [native SYS coverage](docs/NATIVE_COVERAGE.md), [transition evidence](docs/TRANSITIONS.md), [animations](docs/ANIMATIONS.md), [audio parity](docs/AUDIO_PARITY.md), [textbox](docs/TEXTBOX.md).

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
