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
- **Server assets:** optional `tools/generate_manifest.py` emits a size-aware manifest; the browser mounts Emscripten Fetch as a read-only `IMount` and uses local game files first. Remote data is never staged into MEMFS.
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

Cross-origin hosting requires a correct CORS policy. The manifest is
**optional**: if unavailable, the browser switches to a direct HTTP Fetch
provider for known filenames. HTTP directories cannot be enumerated without
an index. The **128 MiB startup preload limit** only applies to the legacy
manifest flow. Direct Fetch uses HEAD and Range with Asyncify and a bounded
in-memory cache; it does not make arbitrary directory scanning possible.
A server without Range is only suitable for smaller files. There is no
truly blocking browser-main-thread network API.

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

## Custom local mounts in the browser

The top-right **+ Adicionar pasta** control remains available after startup.
Select any local directory and enter its virtual mount name, e.g.
`game-assets`, `forced_subtitles`, or `ovrlay`. Its contents become
available under `/<name>/` inside Emscripten's virtual filesystem.
The user-provided name is limited to safe filename characters; `/save` is
reserved for persistent saves. The page prevents accidental file overwrite.
The control can also be scripted through
`window.KotonohaMountBrowserFolder(fileList, "forced_subtitles")` once the
Emscripten filesystem is initialized.

**Important:** mounting an extra folder exposes its files to the VFS but does
not automatically instruct every game port to search that folder. Port-specific
path resolution and touchscreen gameplay must be tested separately.


### Manifest-free Fetch with Asyncify

When `game-assets/manifest.json` cannot be loaded, the HTML bootstrap
registers `game-assets/` as a direct Fetch mount without an index. Games can
access **known** relative paths without providing a manifest. The provider
first requests HTTP `HEAD` to determine the file size; reads then use bounded
`GET Range` requests and a byte cache. A small object from a server that
ignores Range can be accepted as a complete HTTP 200 response. No remote file
is staged in MEMFS.

The web linker enables `-sASYNCIFY=1` so the synchronous VFS `exists()`,
`open()`, and `FileHandle::read()` calls can yield to Fetch callbacks on
the main browser thread. This is **not** a synchronous browser network API.
An HTTP 404 is cached as missing; a network/CORS failure remains a load error.
Large files require correct HTTP Range (206 and Content-Range). The entire
HTTP object may still be returned by a misconfigured server, so enforce
hosting limits. Cross-origin servers must allow HEAD and GET and expose
Content-Length/Content-Range to the browser.

`await window.KotonohaFetchAsset("known-file.bin")` can request one known
relative path explicitly. The result is 1 on success, 0 if pending, or -1
on an invalid request. `window.KotonohaAssetBaseURL` can override the
default game-assets URL **before** initialization.

A directory cannot be enumerated over ordinary HTTP without an index. File
providers that discover unknown paths through directory enumeration or
depend on physical archive file paths still need migration. These changes
do not prove that every port can start or finish gameplay. Re-entrant
Asyncify/SDL operations and memory usage must be tested in real browsers.

### Browser validation

The source-only checks are:

```sh
node tools/check_web_shell.js
node --check tools/web_selftest.js
```

The Playwright suite also exercises manifest-free HTTP, local folder import,
VFS hot-import refresh, saves, media, and browser touch delivery, when built
with `-DKOTONOHA_WEB_SELFTEST=ON`. It does not certify each game's retail
playthrough, and it has not been executed in this environment.

### Failure: `_asyncify_start_unwind is not a function`

This means the generated JavaScript tries to unwind through Asyncify but
the matching WebAssembly module does not export the required functions.
A broken release optimization/link, mixed JS/WASM build artifacts, or stale
browser/CDN cache can cause this mismatch.

The final WebAssembly link now uses **-O2** with Asyncify and the build
executes `tools/check_wasm_asyncify.py` to check all four exports in the
generated `.wasm` before deployment. Clear the old build directory,
reconfigure, rebuild, deploy the `.js` and `.wasm` as a **matching pair**,
and invalidate browser/CDN caches:

```sh
rm -rf build-web
# Reconfigure using the Emscripten/vcpkg arguments above.
cmake --build build-web --target SummerRadish
python3 tools/check_wasm_asyncify.py build-web/outputs/SummerRadish/SummerRadish.js
```

This build-time export check does not replace actual Chromium tests. Replace
the target name if building another game.

### `Uncaught CppException` during Fetch and Asyncify

An `Uncaught CppException {excPtr: ...}` from an XHR callback means a C++
exception escaped the callback or resumed Asyncify continuation. It does
**not** identify Asyncify itself as the original cause: missing assets,
incorrect HTTP headers and archive validation may also lead to exceptions.

CMake now enables `-fexceptions` for the WebAssembly project's own C++
objects, not only the final executable. Fetch completion callbacks catch
C++ exceptions at the JS boundary and log failing asset paths/status codes.
`KotonohaWebSelfTestCatchAfterAsyncify` exercises C++ exception catching
after `emscripten_sleep`. The Summer Radish port probes the known CMP
archive filenames directly instead of depending on HTTP directory listings.

Because compile options changed, delete the previous Emscripten build,
reconfigure, rebuild and deploy matching JS/WASM artifacts. If the browser
still fails, inspect `[Kotonoha] HTTP asset unavailable` logs to identify
the failing path and HTTP status. This still requires actual browser
validation and is not proof of retail gameplay parity.
