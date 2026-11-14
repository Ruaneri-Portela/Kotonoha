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
- **Server assets:** `tools/generate_manifest.py` emits a size-aware manifest; the browser mounts Emscripten Fetch as a read-only `IMount` and uses local game files first. Remote data is never staged into MEMFS.
- **Local folder:** the page's folder picker imports chosen game files into the virtual filesystem. Browser audio/video may use `File` object URLs where supported.
- **Media:** the shared player prefers browser decoding for supported formats, then uses the WebAssembly FFmpeg fallback. Optional `.webm` sidecars can help with legacy codecs; make them only from material you may legally use.

To test the optional web integration route, enable `-DKOTONOHA_WEB_SELFTEST=ON`, build `KotonohaWebSelfTest` and run `node tools/web_selftest.js build-web/outputs/KotonohaWebSelfTest`. For source debugging, compile with `-g3` and use Chrome's C/C++ DevTools DWARF support.

**Known constraints:** browser permissions, asynchronous downloads, browser codec support, and single-threaded execution differ from native desktop behavior. Never rely on direct POSIX access to a browser user's disk.


## HTTP(S) game-assets fallback (Filesystem V2 draft)

The web page now attempts to use the server manifest automatically when no local
game-assets folder has been selected. Local assets are compared against the
manifest by **case-insensitive logical path and file size**; missing or
size-mismatched files are supplied through the Fetch-backed virtual mount.
Neither HTTP data nor remote placeholders are copied to MEMFS.

Publish `game-assets/manifest.json` with a size-aware `entries` array:

```json
{
  "entries": [
    {"path": "DATA/GRAPHIC.CMP", "size": 12345},
    {"path": "OPENING.WEBM", "size": 67890}
  ]
}
```

Use `python3 tools/generate_manifest.py ./game-assets ./game-assets/manifest.json`
to generate the manifest at deployment time. It also retains the `files` array
for older downstream consumers of the manifest format.

By default, the browser resolves `game-assets/` relative to the page. You can
configure an HTTPS CDN **before** the app script initializes:

```html
<script>
  window.KotonohaAssetBaseURL = "https://cdn.example.org/my-game/game-assets/";
</script>
```

Cross-origin hosting requires a correct CORS policy. A remote manifest **must**
exist; HTTP directories are not enumerable. HTTP errors, malformed manifests,
or assets exceeding the current **128 MiB automatic preload limit** leave the
source selection open rather than booting with incomplete data. The underlying
Fetch backend also supports bounded HTTP Range requests, but large remote
installations still need a worker/Asyncify-compatible suspension strategy.
There is no fully synchronous network read on the browser main thread.

Use `window.KotonohaAutoServerAssets = false` before initialization to keep
the manual local/server source choice. The bundled web self-test retains this
manual choice automatically.

```sh
python3 tools/audit_filesystem_v2.py
python3 tools/ValidateBuildMetadata.py
node --check tools/web_selftest.js
# An actual Emscripten build and Playwright-enabled browser run are required:
node tools/web_selftest.js build-web/outputs/KotonohaWebSelfTest
```

This is an **unverified integration draft** until the Emscripten/WebAssembly
build and browser tests pass. Some format-specific game providers still use
physical archive paths and are not yet equivalent to the shared virtual mount.
