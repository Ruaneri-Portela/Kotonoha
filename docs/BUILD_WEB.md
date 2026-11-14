# Building for WebAssembly (Wasm)

This guide provides instructions for compiling the Kotonoha engine for WebAssembly using Emscripten.

## Prerequisites

- **Emscripten SDK (emsdk)**: Must be installed and activated in your current shell environment.
- **vcpkg**: Required for dependency management.
- **Python 3**: Required for manifest generation and asset bundling tools.

## Build Steps

### 1. Environment Setup

To avoid compiler detection errors (such as invalid architecture flags or toolchain mismatches), you must ensure your environment variables are correctly set to point to your Emscripten installation.

Run the following commands in your terminal (adjusting the paths if necessary):

```bash
# 1. Add Homebrew to your PATH
export PATH="/opt/homebrew/bin:$PATH"

# 2. Set Emscripten paths
export EMSCRIPTEN_ROOT="/opt/homebrew/opt/emscripten/libexec"
export EMSDK="$EMSCRIPTEN_ROOT"
export EMSCRIPTEN="$EMSCRIPTEN_ROOT"
export EMSDK_PYTHON="/opt/homebrew/bin/python3"
```

### 2. Configure the Build

Create a build directory named `build-web` and configure the project using `emcmake` and CMake. You must specify the `wasm32-emscripten` triplet, chainload the Emscripten toolchain, and point to the `ffmpeg` overlay ports.

To enable C++ debugging symbols (DWARF) in browser DevTools, you can optionally include `-DCMAKE_CXX_FLAGS="-g3" -DCMAKE_C_FLAGS="-g3"`.

```bash
emcmake cmake -S . -B build-web \
  -DCMAKE_TOOLCHAIN_FILE=<path_to_vcpkg>/scripts/buildsystems/vcpkg.cmake \
  -DVCPKG_CHAINLOAD_TOOLCHAIN_FILE=$EMSCRIPTEN_ROOT/cmake/Modules/Platform/Emscripten.cmake \
  -DVCPKG_TARGET_TRIPLET=wasm32-emscripten \
  -DVCPKG_OVERLAY_PORTS=vcpkg-overlays/ports \
  -DKOTONOHA_BUILD_ALL=ON \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_CXX_FLAGS="-g3" \
  -DCMAKE_C_FLAGS="-g3"
```

*Replace `<path_to_vcpkg>` with your actual vcpkg installation path.*

### 3. Compile

Run the build command for a specific game target (e.g., `RealLive`):

```bash
cmake --build build-web --target RealLive
```

---

## Runtime Architecture (Saves, Assets & Video)

### Persistent saves (IDBFS)

`index.html` mounts an Emscripten IDBFS volume at `/save` before `main()` runs and
populates it from IndexedDB with `FS.syncfs(true, ...)`. The mount and flush are
wrapped by `KotonohaMountSaveStorage`/`KotonohaSyncSaveStorage` in
`WebAssetFetch.cpp`: `--closure 1` minifies the FS object exposed to page
JavaScript, so the page must reach these operations through the C++ `EM_ASM`
wrappers instead of calling `Module.FS.mkdirTree/FS.mount/FS.syncfs` directly.
On WebAssembly, `documentsDirectory()` and `preferredDocumentsDirectory()` map to
that volume. `VNSave` calls `FS.syncfs(false, ...)` after writes, renames and
removals, and the page also flushes the volume on
`visibilitychange`/`pagehide`, so saves survive a browser reload.

### Asset loading

On the loading screen, choose **Selecionar pasta do jogo** to load assets from
a folder on your computer, or **Usar assets do servidor** to use the HTTP
manifest. For a local folder, select the game's asset root (the folder whose
contents have the paths expected by that game). The selector accepts nested
folders and also a folder containing a `game-assets` subfolder. The browser
copies the selected files into WASM MEMFS before starting the engine. Local
audio and video use browser `File` object URLs when supported; FFmpeg can read
the same files from MEMFS when browser decoding fails. The page itself still
needs to be served over HTTP for the WASM bundle.

For server assets, Kotonoha uses a manifest-driven loader implemented in
`lib/src/core/WebAssetFetch.cpp`:

1. **Manifest generation**: CMake invokes [`tools/generate_manifest.py`](file:///Users/nero/Codes/Kotonoha/tools/generate_manifest.py) to write `outputs/<Game>/game-assets/manifest.json`.
2. **Manifest**: a valid `manifest.json` maps server assets into the virtual filesystem.
3. **Emscripten Fetch**: the preRun hook calls `KotonohaStartAssetFetch`; C++ downloads up to four files concurrently with `emscripten_fetch` and writes them into MEMFS at `/game-assets`.
4. **Progress UI**: the page polls `KotonohaAssetFetch*` through `ccall` and updates the loading overlay.
5. **Media fallback**: audio and video files are kept in MEMFS even when the
   browser can play their MIME type. If HTML media playback fails,
   `MediaPlayer` opens those local bytes with FFmpeg in WASM. Initial loading
   includes media bytes; large deployments may need an on-demand byte source.

### Browser audio and video

The shared `MediaPlayer` tries browser decoding first for supported MIME types,
then uses FFmpeg for unsupported types or browser playback failures. Audio uses
independent `<audio>` sessions, so music, voice and effects can overlap; each
session follows pause, playback rate and channel gain. This behavior applies
to every port that uses `MediaPlayer`. Providers that decode proprietary game
formats to raw PCM for `PcmPlayer` continue using their format-specific decoder.
School Days processes one media prefetch job per update on the single-threaded
web build; its native build keeps the background worker.

Browser-playable video uses a hidden `<video>` element for decoding. Each frame
is copied to an SDL texture and rendered in the game's normal draw order, so
SDL UI remains above the movie. Loose assets are opened from their
`game-assets/...` URL; packed/archive assets are read through the provider and
assembled into a Blob. Browser audio uses hidden `<audio>` elements.

Legacy MPEG/AVI video is generally unavailable to the browser's `<video>`
decoder, so Kotonoha decodes it locally with FFmpeg in WASM. A WebM sidecar
can optionally use the browser decoder instead. Place it beside the asset,
keeping its full original name and adding `.webm` (for example,
`opening.mpg.webm`). Include the sidecar in the generated manifest. The web
`MediaPlayer` selects it when the browser cannot play the original MIME type;
native builds continue to use the original file. Generate sidecars from media
you are licensed to distribute, for example with
`ffmpeg -i opening.mpg -c:v libvpx-vp9 -c:a libopus opening.mpg.webm`.
The sidecar must be visible through the same `AssetProvider` ID or as a loose
file beside a direct path. Browser support is checked at runtime, so a WebM
sidecar does not force a particular codec on browsers that cannot decode it.

With `-DKOTONOHA_WEB_SELFTEST=ON`, build `KotonohaWebSelfTest` and exercise this route
using a short, temporary transcode of an existing MPEG file:

```bash
cmake --build build-web --target KotonohaWebSelfTest
KOTONOHA_WEB_TEST_MPEG=/path/to/opening.mpg node tools/web_selftest.js build-web/outputs/KotonohaWebSelfTest
```

The optional MPEG fixture also checks FFmpeg decoding with audio in the
single-threaded WASM build. `MediaPlayer` fills its SDL audio buffer during
`update()` there; the web build does not start a decoder `std::thread`.

---

## C++ Debugging in Browser (DWARF)

When compiled with `-g3`, Emscripten embeds DWARF debug information into the build. To debug C++ source code in Google Chrome:
1. Install the official **C/C++ DevTools Support (DWARF)** extension from the Chrome Web Store.
2. Open Chrome DevTools (`F12`) and navigate to the **Sources** tab.
3. Locate your C++ source files (`.cpp`, `.hpp`), set breakpoints, and debug C++ code directly.
