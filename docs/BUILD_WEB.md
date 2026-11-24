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

## CMake presets (`wasm-vcpkg-debug` / `wasm-vcpkg-release`)

WASM has exactly two presets: `<name>-<provider>-<config>` with `vcpkg`
mandatory (no system variant). Both use Ninja, the vcpkg toolchain with the
Emscripten chainload file `$EMSCRIPTEN_ROOT/cmake/Modules/Platform/Emscripten.cmake`,
the `wasm32-emscripten` triplet, `VCPKG_OVERLAY_PORTS=vcpkg-overlays/ports`,
`VCPKG_MANIFEST_FEATURES=sdl-vcpkg;imgui-vcpkg` and
`KOTONOHA_IMGUI_PROVIDER=VCPKG` (the port supports the SDL3 bindings). Build
directories are `out/build/wasm-vcpkg-debug` and
`out/build/wasm-vcpkg-release`; the classic `build-web` directory above is not
modified. Asyncify flags remain in `buildsystems/cmake/games/KotonohaApp.cmake`.

```sh
source /path/to/emsdk/emsdk_env.sh
cmake --preset wasm-vcpkg-release
cmake --build --preset wasm-vcpkg-release --target RealLive

cmake --preset wasm-vcpkg-debug
cmake --build --preset wasm-vcpkg-debug --target RealLive
```

Some installations (for example Homebrew Emscripten) do not export
`EMSCRIPTEN_ROOT`, which the vcpkg triplet needs. Use the helper instead; it
derives the root from `emcc` on `PATH` and then runs the release preset by
default (set `WASM_PRESET=wasm-vcpkg-debug` for the debug one):

```sh
./tools/configure_wasm.sh
cmake --build --preset wasm-vcpkg-release --target RealLive

WASM_PRESET=wasm-vcpkg-debug ./tools/configure_wasm.sh
cmake --build --preset wasm-vcpkg-debug --target RealLive
```

Always source `emsdk_env.sh` (or set `EMSCRIPTEN_ROOT`) before the first
configure: the Emscripten toolchain is selected at configure time.

## One installable PWA per browser game

Each Emscripten target is packaged as its own installable Progressive Web App
under `build-web/outputs/<target>/`. The directory includes
`index.html`, `manifest.webmanifest`, `service-worker.js`,
`pwa-icon-180.png`, `pwa-icon-192.png`, `pwa-icon-512.png`,
`KotonohaPreloadCache.js`, and the matching game JS/WASM/data artifacts.
The install name, identity, icon accent and Service Worker scope are specific
to that target. For example, `/RealLive/` and `/Kirikiri/` install separately.
The generated icons need no additional Python packages.

Serve each directory over **HTTPS**, or use `http://localhost` while
developing; Service Workers are unavailable for ordinary insecure HTTP sites.
Install from your browser's menu (or **Add to Home Screen** on iOS). The PWA
worker caches the application shell and, after the runtime initializes, its
JS/WASM/data for later offline use. It **never intercepts `game-assets/`**,
`HEAD`, or HTTP `Range`: game assets and preloading are handled by the
separate, persistent IndexedDB byte-range cache. Offline gameplay requires all
assets the engine needs to have been cached previously; installing a PWA alone
does not download an entire game's archives. Storage quotas and eviction still
apply.

### Console-driven initial preload

With an HTTP(S) source selected, play through the menus and initial scenes to
record the actual fetched byte ranges. In DevTools, run:

```javascript
await KotonohaDumpPreload();
```

The browser downloads `preload.json`. Put it next to the source manifest at
`game-assets/preload.json`. This dump includes **only successfully downloaded
files and ranges**, not every file listed by `manifest.json`. The runtime
also records current story nodes, which assets were downloaded while visiting
each node, and observed transitions. These are exported in the optional
`nodes` map of the guide. On later runs the shared C++ runtime notifies the
browser when the node changes; the JavaScript scheduler prefetches current and
neighboring node assets, limited to an 8 MiB budget and two concurrent
requests. Speculative prefetching can be cancelled on another scene change.
The exported `entries` remain the **mandatory boot set**: all listed byte
ranges must finish before the engine starts. If you only want boot menus and
the first scene to block startup, trim `entries` to those ranges while keeping
the other scenes and their relations under `nodes`. If that preload
file exists, the HTTP(S) startup waits until each required range is in
IndexedDB, fetching any missing chunks before releasing the WASM startup
dependency. The dump includes a manifest fingerprint to prevent mixing two
asset versions. A cached guide and cached index can also be restored when
offline; missing required chunks prevent the startup barrier from completing.
No HTTP preload is attempted when choosing a local folder.

## Browser language and bounded asset cache

Every generated game page provides **English / Português** buttons in the
startup overlay. Browser language is the initial default; the selected language
is remembered with `localStorage` and applies to the asset picker, progress,
folder mounting, error messages, and accessibility labels. The choice does
not change in-game script localization.

Asset chunks use an **oldest-first (LRU)** IndexedDB cache shared by games
hosted on the same web origin. The default upper bound is **2 GiB** of cached
binary chunks (not game saves or the PWA runtime). A successful read refreshes
its last-access time. Before adding a chunk, the manager attempts to free old
chunks to remain under the cap and `navigator.storage.estimate()` headroom.
If a browser returns `QuotaExceededError`, older chunks are evicted and the
write retried. Deletions are transactional: the binary data, metadata, and
stored byte count change together. The boot guide's required byte ranges are
protected from eviction during the session; if those ranges cannot fit, the
preload fails rather than launching with incomplete required assets.

Optionally set a per-origin cache size **before starting an HTTP game**:

```javascript
window.KotonohaCacheMaxBytes = 512 * 1024 * 1024; // 512 MiB
// Once the HTTP source has been configured:
await KotonohaAssetCache.getCacheStats();
```

Changing this value applies to subsequent writes, not an immediate purge.
Only the asset chunk cache is evicted; `/save` IDBFS and PWA shell Cache
Storage are not affected. If persistent caching cannot write, normal online
asset reads can still continue. Offline use requires that the needed chunks
remain cached.

## Browser storage and assets

- **Saves:** an IDBFS mount at `/save` synchronizes with IndexedDB; writes are flushed through the web storage adapter.
- **Server assets:** HTTP(S) requires a non-empty `manifest.json` beside `index.html`. Build it with `python3 tools/generate_manifest.py build-web/outputs/<target>/game-assets build-web/outputs/<target>/manifest.json`. Paths are relative to `game-assets/`; remote data remains lazy and is never copied into MEMFS.
- **Local folder:** the page's folder picker imports chosen game files into the virtual filesystem. Browser audio/video may use `File` object URLs where supported.
- **Media:** the shared player prefers browser decoding for supported formats, then uses the WebAssembly FFmpeg fallback. Optional `.webm` sidecars can help with legacy codecs; make them only from material you may legally use.

To test the optional web integration route, enable `-DKOTONOHA_WEB_SELFTEST=ON`, build `KotonohaWebSelfTest` and run `node tools/web_selftest.js build-web/outputs/KotonohaWebSelfTest`. For source debugging, compile with `-g3` and use Chrome's C/C++ DevTools DWARF support.

**Known constraints:** browser permissions, asynchronous downloads, browser codec support, and single-threaded execution differ from native desktop behavior. Never rely on direct POSIX access to a browser user's disk.


## Required HTTP(S) asset manifest

Every browser port waits for an explicit asset-source selection. **Local-folder
mode** requires no manifest and never requests it. **HTTP(S) mode** must load a
non-empty, size-aware `manifest.json` next to the page's `index.html`. If the
manifest is missing, empty, invalid or rejected by the C++ provider, the game
does not boot. Only a previously validated cached manifest may be reused
when the network itself is unavailable.

The Python tool accepts **two positional arguments**:
`[pasta] [arquivo]` (source asset directory and exact output filename):

```sh
python3 tools/generate_manifest.py build-web/outputs/N2SystemGame/game-assets build-web/outputs/N2SystemGame/manifest.json
python3 tools/generate_manifest.py /Volumes/Server/data2/Games/SteinsGate /Volumes/Server/data2/Games/manifest.json
```

`[pasta]` is indexed **directly and recursively**, including retail `.npa`
archives and nested paths such as `dx/`; the tool does not silently switch to
a `game-assets/` child. `[arquivo]` determines the exact JSON output location
(absolute or relative to the current working directory), including a file
one directory above the indexed assets. The selected output file is never
included in its own index. For WASM, publish the manifest beside `index.html`
and expose the indexed paths under `game-assets/` (or map URLs via the
CDN/server). CMake calls the generator with both paths after copying the
assets for each app.

**The manifest is an allow-list.** Unindexed files return not found
immediately; there is **no HEAD/GET discovery**, even for explicitly requested
names. Only indexed files are fetched lazily using bounded HTTP Range.
Optional `game-assets/preload.json` may refer only to indexed assets.

By default, the browser resolves `game-assets/` relative to the page. You can
configure an HTTPS CDN **before** the app script initializes:

```html
<script>
  window.KotonohaAssetBaseURL = "https://cdn.example.org/my-game/game-assets/";
</script>
```

Cross-origin hosts need correct CORS. Direct Fetch uses HEAD, GET Range,
Asyncify and a bounded cache without blocking the browser main thread.
Ordinary HTTP does not enumerate arbitrary filenames without an index;
the port needs to know the filenames it wants. PONscripter and Kirikiri
now probe common scripts and `arc.nsa` / `data.xp3` names explicitly rather
than requiring a remote directory listing. For archives with unusual
names, provide a local folder or an indexed asset source. PONscripter's
SAR/NSA decoder currently buffers entire archives and may not fit large
files in browser memory; Kirikiri's XP3 reader uses Range access.

Local/remote startup is always manual. The legacy
`window.KotonohaAutoServerAssets` switch is no longer needed.

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

The top-right **+ Adicionar pasta** button is **only available on the
initial asset-selection screen**. It disappears when the local or HTTP(S)
source is confirmed, stays hidden during gameplay, and is never re-opened
by canvas fullscreen. Import optional folders before starting SDL.
The toolbar is ordinary `position: fixed` HTML (not a Popover), anchored
to the upper-right safe area consistently in Safari/WebKit and Chrome.
Check `node tools/web_toolbar_layout.js build-web/outputs/KotonohaWebSelfTest`
to compare Chromium/WebKit at desktop, portrait and landscape sizes.
Select any local directory and enter its virtual mount name, e.g.
`game-assets`, `forced_subtitles`, or `ovrlay`. Its contents become
available under `/<name>/` inside Emscripten's virtual filesystem.
The user-provided name is limited to safe filename characters; `/save` is
reserved for persistent saves. The page prevents accidental file overwrite.
The internal `window.KotonohaMountBrowserFolder(fileList, "forced_subtitles")`
API remains available for programmatic integrations. This does not expose
the folder button in-game.

**Important:** mounting an extra folder exposes its files to the VFS but does
not automatically instruct every game port to search that folder. Port-specific
path resolution and touchscreen gameplay must be tested separately.


### Manifest-indexed Fetch with Asyncify

Indexed directory listings and file sizes come from the manifest without
network probing. HTTP GET Range fetches actual file bytes only when needed.
Asyncify lets synchronous VFS callers yield until a requested range becomes
available; it does not use blocking XMLHttpRequest. Large files need proper
206 Content-Range support. A correctly sized whole HTTP 200 response is
accepted only for small files (up to 8 MiB).

`await window.KotonohaFetchAsset("indexed-file.bin")` checks the manifest
(1 = indexed, 0 = absent, -1 = invalid) without fetching an absent file.
Set `window.KotonohaAssetBaseURL` before startup to use a CDN, while serving
the index from the page root.

### Browser validation

The source-only checks are:

```sh
node tools/check_web_shell.js
node --check tools/web_selftest.js
```

The Playwright suite also exercises strict-manifest HTTP, local folder import,
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

### Controller, touch and mobile browser validation

The web shell uses `viewport-fit=cover`, dynamic viewport units, 44 px
minimum startup controls, safe-area padding and `touch-action: none` on the
SDL canvas. The source picker and optional folder picker are **pre-game only**.
When a source is chosen, the canvas receives focus. The SDL runtime initializes
`SDL_INIT_GAMEPAD` and handles hotplugged devices; the shared input layer
normalizes D-pad/left-stick navigation and A/B swapping and translates touch
into safe pointer/gesture input. The Pure Mail port also translates controller
buttons for its title and selection screens.

`Kotonoha.InputActions`, `Kotonoha.NativeInput` and the web Playwright
self-test cover input mapping, touch gesture safety, keyboard events, mobile
portrait/landscape layout and the browser SDL gamepad subsystem. A physical
gamepad is **not emulated** by these browser tests; gamepad buttons, axes,
iOS Safari folder selection and complete retail gameplay still require
real-device/manual validation.

**Pre-merge checklist:** run the Linux C++ tests, build every enabled WASM
game, verify Asyncify exports for each generated JS/WASM pair, pass the
Chromium Playwright suite and check startup/assets/audio/video/gameplay for
each retail port with legitimately provided game data. For mobile parity,
also test a physical Android touch device and iPhone/iPad Safari (portrait
and landscape), Bluetooth/USB gamepads and browser background/resume.
Unsupported directory pickers may allow only individual files at the root
of `game-assets`; use HTTP(S) Fetch if nested local directories cannot be
selected. The CI proves the automated subset, **not** 100% device parity.

### Expected browser asset paths

| Port | Known files to publish in `game-assets/` |
|---|---|
| Amagami | `Amagami.bin` or `Amagami.iso` |
| Zero no Tsukaima | `ZeroNoTsukaima.bin` or `ZeroNoTsukaima.iso` |
| Snow Radish | `SCRIPT.DAT` and `GRAPHIC.DAT` at root, `pack/` or `data/pack/` |
| Pure Mail | `data/pack/script.dat`, other DATs and `data/movie/PM01L_I5.avi` (or an `.avi.webm` sidecar) |
| School Days | KTRF route embedded by the build; 29 retail GPKs at root or `Packs/` |

BIN/ISO readers now use VFS and HTTP Range without staging a whole disc in
MEMFS. Video opens from a FileHandle; AVI playback still depends on decoder
support or a WebM sidecar. Startup success cannot prove absent files exist.

### School Days: large GPK, HTTP 200, missing trailer

If `HTTP asset unavailable: event01.gpk (HTTP 200)` is followed by
`GPK trailer could not be read`, the web server likely ignored `Range`
and returned the entire GPK instead of the requested trailer bytes.
**Increasing the cache budget is not a fix.** The server must respond to
ranged GET with `206 Partial Content` and `Content-Range`.

For local development, stop the previous server listening on port 3000,
then serve the WASM output using the included HTTP Range server:

```sh
python3 tools/serve_web.py --root build-web/outputs --port 3000
curl -I http://localhost:3000/SchoolDays/game-assets/Event01.GPK
curl -H 'Range: bytes=0-15' -o /dev/null -D - \
  http://localhost:3000/SchoolDays/game-assets/Event01.GPK
```

The HEAD response must advertise `Accept-Ranges: bytes` and
`Content-Length`. Ranged GET must return `206`,
`Content-Range: bytes 0-15/TOTAL` and `Content-Length: 16`.
Adjust path capitalization to match the actual files hosted.
Production hosts need the same behavior; cross-origin hosts must expose
`Accept-Ranges`, `Content-Length`, and `Content-Range` through CORS.

Fetch now uses a bounded, evictable LRU byte-range cache rather than
permanently failing an archive after cumulative reads exceed the cache
budget. For objects larger than 8 MiB, it rejects an HTTP HEAD that does
not advertise byte-range support, avoiding an avoidable full-body download.
A misconfigured host can still incorrectly advertise Range support and
send a full HTTP 200 response; configure the actual file server.

Test the Range server independently:

```sh
python3 -m unittest discover -s tools -p test_serve_web.py -v
```

The test creates a sparse 320 MiB synthetic GPK and fetches only its tail.
The Chromium/Playwright suite also checks a real VFS trailer read and
observes HTTP 206 (when the WASM test target is compiled).

