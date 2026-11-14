# Debug UI, build version, and SDL diagnostics

**English (US)** | [Português (Brasil)](DEBUG_TOOLS.pt-BR.md)

## Git-based build identity

The root CMake project includes `buildsystems/cmake/BuildVersion.cmake`
before compiling the engine and game ports. At configure time it reads the
current Git branch, full and short commit SHA, and tracked-file dirty state.
It generates `build/generated/Kotonoha/core/BuildVersion.hpp` (use your
actual CMake binary directory). The source tree remains unchanged.

A build version looks like
`0.1.0+feature-wasm-custom-folders-20261114.0123456789ab`;
a dirty checkout adds `.dirty`. The base version remains the
`project(... VERSION ...)` number. Git-less source archives explicitly
report `unknown`. The active Git branch/HEAD ref is tracked in CMake
configure dependencies, so normal Ninja/Make rebuilds refresh the header
after a commit or branch switch. Reconfigure manually if your build system
does not monitor CMake configure dependencies.

Every game uses the shared `CommandLine` parser:

```sh
./build/outputs/Kotonoha/Kotonoha --version
./build/outputs/Kotonoha/Kotonoha -V
```

These print the build version, branch, commit and tracked checkout status
and exit before loading game assets or creating a rendering window.
`--help` remains unchanged.

## Interface locations

The common launcher displays the build identity in its header and footer.
Its **System info** tab provides detailed SDL diagnostics. The WebAssembly
HTML asset-source chooser also displays the generated full build version
before the game starts.

In a running game, open the Debug UI (**F10**), then choose
**System info** in the Applets menu, click the **System info** tile, or
use the **System info** tab in Options. The information applet is read-only
and can be moved independently; use **Refresh information** to update the
snapshot or **Copy diagnostic report** to copy key/value data to the clipboard.

The Info panel reports what SDL3 can actually provide: compiled/linked SDL
version, Git build, platform, logical CPU cores, system RAM total (MiB),
CPU cache line and SIMD features, rendering backend, maximum texture size,
VSync, window size/density, connected displays and refresh rates, battery
state, gamepads, touch devices, and playback device names.

SDL3 does **not** provide a portable CPU model name, per-process/free RAM
usage, or graphics VRAM usage through these APIs. A physical GPU name is
shown only when the active SDL GPU renderer exposes an `SDL_GPUDevice`
with a name property; on OpenGL/Metal/other SDL_Renderer paths it may be
unavailable. No CPU/GPU model is guessed. These are local diagnostics,
not telemetry.

## Validation

```sh
cmake -S . -B build -DBUILD_TESTING=ON
cmake --build build --target KotonohaBuildVersionTests KotonohaLauncherTests
ctest --test-dir build --output-on-failure -R 'Kotonoha\.(BuildVersion|Launcher)'
```

On headless Linux, set `SDL_VIDEO_DRIVER=dummy SDL_AUDIO_DRIVER=dummy`
for direct execution; CTest already configures these environments. The
Linux workflow runs CLI/version, launcher, Debug UI and VFS regressions.
Browser and real GPU/phone validation remain separate from these native tests.
