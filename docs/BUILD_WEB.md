# WebAssembly (Emscripten)

**English (US)** | [Português (Brasil)](BUILD_WEB.pt-BR.md)

Kotonoha builds selected game ports for the browser with Emscripten. Install and activate **emsdk**, plus CMake, vcpkg, Python 3, and the required overlay ports.

```sh
source /path/to/emsdk/emsdk_env.sh
emcmake cmake -S . -B build-web \
  -DCMAKE_TOOLCHAIN_FILE="$VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake" \
  -DVCPKG_CHAINLOAD_TOOLCHAIN_FILE="$EMSDK/upstream/emscripten/cmake/Modules/Platform/Emscripten.cmake" \
  -DVCPKG_TARGET_TRIPLET=wasm32-emscripten \
  -DVCPKG_OVERLAY_PORTS=vcpkg-overlays/ports \
  -DKOTONOHA_BUILD_REALLIVE=ON \
  -DCMAKE_BUILD_TYPE=Release
cmake --build build-web --target RealLive
```

Toolchain locations vary by installation. `KOTONOHA_BUILD_ALL=ON` enables supported web ports but excludes DDLC. Serve the generated output through **HTTP**, not `file://`.

## Browser storage and assets

- **Saves:** an IDBFS mount at `/save` synchronizes with IndexedDB; writes are flushed through the web storage adapter.
- **Server assets:** `tools/generate_manifest.py` creates `game-assets/manifest.json`; `WebAssetFetch.cpp` fetches data with `emscripten_fetch` and stages it in MEMFS. A URL is **not** a local MEMFS path.
- **Local folder:** the page's folder picker imports chosen game files into the virtual filesystem. Browser audio/video may use `File` object URLs where supported.
- **Media:** the shared player prefers browser decoding for supported formats, then uses the WebAssembly FFmpeg fallback. Optional `.webm` sidecars can help with legacy codecs; make them only from material you may legally use.

To test the optional web integration route, enable `-DKOTONOHA_WEB_SELFTEST=ON`, build `KotonohaWebSelfTest` and run `node tools/web_selftest.js build-web/outputs/KotonohaWebSelfTest`. For source debugging, compile with `-g3` and use Chrome's C/C++ DevTools DWARF support.

**Known constraints:** browser permissions, asynchronous downloads, browser codec support, and single-threaded execution differ from native desktop behavior. Never rely on direct POSIX access to a browser user's disk.
