# Pure Mail for Kotonoha

Independent port of the Pure Mail DVD engine using Kotonoha scenes, text,
audio, save menus, translation and dubbing infrastructure. Supply the original
PMAILDVD directory containing `data/pack/*.dat` and `data/movie`.

The port now decodes and plays the DVD's `.anim` files, including the FORIS OS
boot sequence, password effects and character mouths. See
[animation investigation](docs/ANIMATIONS.md),
[audio investigation](docs/AUDIO_PARITY.md) and
[remaining parity work](PORT_STATUS.md).

## Build and run

```sh
cmake -S . -B build -DKOTONOHA_BUILD_PMAIL=ON -DBUILD_TESTING=ON
cmake --build build --target Pmail -j4
build/outputs/Pmail.app/Contents/MacOS/Pmail \
  --pmail '/path/to/PMAILDVD' --no-launcher
```

On macOS the app is `build/outputs/Pmail.app`. The game uses an 800×600 canvas
with 4:3 letterboxing. Enter/Space/left click completes or advances dialogue;
Escape/right click opens the in-game menu; B/Page Up/mouse wheel up opens the
backlog. Save/load and return to title are available from the menu. The title's
second button opens load slots.

## Tests with the retail assets

The asset directory is an opt-in CMake cache setting; game data is never copied
into the source repository.

```sh
cmake -S . -B build -DBUILD_TESTING=ON \
  -DPMAIL_TEST_ASSET_DIR='/path/to/PMAILDVD'
cmake --build build --target PmailAudioCheck PmailGraphicsCheck \
  PmailAudioCommandsCheck PmailSaveStateCheck PmailRuntimeAudioCheck PmailAnimationFormatCheck \
  PmailStoryFlowCheck PmailAnimationPlaybackCheck PmailAnimationRenderCheck -j4
ctest --test-dir build -R '^Pmail\.' --output-on-failure
```

CTest runs the graphical/audio integration tests with SDL dummy drivers.
These verify decoding, composition and state transitions; they do not replace
listening tests or synchronized comparison with the Windows executable.
