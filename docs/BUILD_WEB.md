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

### Asset loading (Emscripten Fetch)

To avoid synchronous XHR restrictions and memory exhaustion, Kotonoha uses a
manifest-driven loader implemented in `lib/src/core/WebAssetFetch.cpp`:

1. **Manifest generation**: CMake invokes [`tools/generate_manifest.py`](file:///Users/nero/Codes/Kotonoha/tools/generate_manifest.py) to write `outputs/<Game>/game-assets/manifest.json`.
2. **Required manifest**: a valid `manifest.json` must exist for assets to be mapped into the virtual filesystem.
3. **Emscripten Fetch**: the preRun hook calls `KotonohaStartAssetFetch`; C++ downloads up to four files concurrently with `emscripten_fetch` and writes them into MEMFS at `/game-assets`.
4. **Progress UI**: the page polls `KotonohaAssetFetch*` through `ccall` and updates the loading overlay.
5. **Video deferral**: browser-playable video extensions are not downloaded by the bulk loader (`KotonohaShouldDeferAsset`); they receive a zero-byte placeholder in MEMFS so asset indexes still resolve their logical ids, while the HTML5 bridge streams the real file from its `game-assets/...` URL.

### HTML5 video

`MediaPlayer` delegates browser-playable video to a `<video>` overlay
(`#kotonoha-video-player`). Loose assets are played from their `game-assets/...`
URL through `KotonohaPlayVideo(path, loop)`; packed/archive assets are read from
their `AssetProvider` in chunks and appended to a Blob, so no archive-internal
video needs a public URL. The overlay is sized to the SDL canvas drawable area,
so 4:3 content stays 4:3 even on a wider browser window. Audio mixer delegation
is intentionally out of scope.

---

## C++ Debugging in Browser (DWARF)

When compiled with `-g3`, Emscripten embeds DWARF debug information into the build. To debug C++ source code in Google Chrome:
1. Install the official **C/C++ DevTools Support (DWARF)** extension from the Chrome Web Store.
2. Open Chrome DevTools (`F12`) and navigate to the **Sources** tab.
3. Locate your C++ source files (`.cpp`, `.hpp`), set breakpoints, and debug C++ code directly.
