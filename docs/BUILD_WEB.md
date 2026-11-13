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

## Asset Loading Architecture (Manifest & Lazy Loading)

To prevent browser crashes from synchronous XHR restrictions (`Cannot do synchronous binary XHRs outside webworkers`) and memory exhaustion (`OOM`), Kotonoha uses an asynchronous **Manifest-based Lazy Loading** architecture for WebAssembly:

1. **Manifest Generation**: During build time, CMake automatically invokes [`tools/generate_manifest.py`](file:///Users/nero/Codes/Kotonoha/tools/generate_manifest.py) to scan the game's `game-assets/` directory and produce a `manifest.json` file inside `outputs/<Game>/game-assets/manifest.json`.
2. **Required Manifest**: **A valid `manifest.json` file is required in the game's `game-assets/` folder** for the web application to properly map and load game assets.
3. **Progress UI**: When opening `index.html`, the browser displays an overlay with a real-time progress bar (`Carregando (X/Y): ...`) while fetching and mounting assets.
4. **Asynchronous Fetch & VFS Population**: The `index.html` loader fetches `manifest.json`, loops through the files, creates virtual directory trees (`FS.mkdirTree`), and asynchronously downloads each file via `fetch()` and writes it into Emscripten's MEMFS (`FS.writeFile`).
5. **Runtime Initialization**: Once all assets are mapped, the loading overlay smoothly fades out, and the C++ engine starts up with full access to files via standard file APIs (`std::filesystem`, `fopen`).

---

## C++ Debugging in Browser (DWARF)

When compiled with `-g3`, Emscripten embeds DWARF debug information into the build. To debug C++ source code in Google Chrome:
1. Install the official **C/C++ DevTools Support (DWARF)** extension from the Chrome Web Store.
2. Open Chrome DevTools (`F12`) and navigate to the **Sources** tab.
3. Locate your C++ source files (`.cpp`, `.hpp`), set breakpoints, and debug C++ code directly.
