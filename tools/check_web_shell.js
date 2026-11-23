#!/usr/bin/env node
// Source-only guard for the configured Emscripten HTML shell.
// This does NOT replace the Playwright/Emscripten integration suite.
'use strict';

const fs = require('node:fs');
const path = require('node:path');
const vm = require('node:vm');
const assert = require('node:assert/strict');
const os = require('node:os');
const {spawnSync} = require('node:child_process');

const htmlPath = path.join(__dirname, '..', 'buildsystems', 'cmake', 'packaging', 'index.html.in');
const html = fs.readFileSync(htmlPath, 'utf8');
const wasmBuild = fs.readFileSync(path.join(__dirname, '..',
  'buildsystems', 'cmake', 'games', 'KotonohaApp.cmake'), 'utf8');
const webBridge = fs.readFileSync(path.join(__dirname, '..',
  'lib', 'src', 'core', 'WebAssetFetch.cpp'), 'utf8');
const browserTransport = fs.readFileSync(path.join(__dirname, '..',
  'lib', 'src', 'filesystem', 'sources', 'FetchAssetProvider.cpp'), 'utf8');
const persistentCache = fs.readFileSync(path.join(__dirname, '..',
  'lib', 'web', 'KotonohaPreloadCache.js'), 'utf8');
new vm.Script(persistentCache, {filename: 'KotonohaPreloadCache.js'});
// Each Emscripten game must package a valid standalone PWA, with a scoped
// Service Worker which leaves HTTP Range/game-assets to IndexedDB.
const pwaTemplate = fs.readFileSync(path.join(__dirname, '..',
  'buildsystems', 'cmake', 'packaging', 'pwa.webmanifest.in'), 'utf8');
const serviceWorker = fs.readFileSync(path.join(__dirname, '..',
  'buildsystems', 'cmake', 'packaging', 'service-worker.js.in'), 'utf8');
new vm.Script(serviceWorker, {filename: 'service-worker.js.in'});
const pwa = JSON.parse(pwaTemplate.replaceAll('@APP_TITLE@', 'RealLive'));
assert(pwa.id === './' && pwa.scope === './' &&
  pwa.start_url === './index.html' && pwa.display === 'standalone',
  'PWA identity, scope and URL must be relative to each game directory');
assert(pwa.icons.some(icon => icon.sizes === '192x192') &&
       pwa.icons.some(icon => icon.sizes === '512x512') &&
       pwa.icons.every(icon => icon.purpose.includes('maskable')),
  'PWA needs 192/512 installable maskable icons');
assert(serviceWorker.includes('request.headers.has("range")') &&
       serviceWorker.includes('relative.startsWith("game-assets/")') &&
       serviceWorker.includes('request.method !== "GET"') &&
       serviceWorker.includes('new URL(self.registration.scope)') &&
       serviceWorker.includes('CACHE_NAME'),
  'Worker must be game-scoped and leave HTTP Range/assets to IndexedDB');
assert(wasmBuild.includes('pwa.webmanifest.in') &&
       wasmBuild.includes('service-worker.js.in') &&
       wasmBuild.includes('tools/generate_pwa_icons.py'),
  'CMake must package PWA files independently for every WebAssembly target');
assert(html.includes('rel="manifest" href="./manifest.webmanifest"') &&
       html.includes('rel="apple-touch-icon" href="./pwa-icon-180.png"') &&
       html.includes('serviceWorker.register("./service-worker.js"') &&
       html.includes('type: "KOTONOHA_CACHE_RUNTIME"'),
  'Each game shell must register its own installable scoped PWA');
const iconDir = fs.mkdtempSync(path.join(os.tmpdir(), 'kotonoha-pwa-test-'));
try {
  const generator = path.join(__dirname, 'generate_pwa_icons.py');
  // The CI runners provide python3; local Windows environments may only
  // expose the installed interpreter as python (python3 is a Store alias).
  const python = process.env.KOTONOHA_PYTHON ||
    (process.platform === 'win32' ? 'python' : 'python3');
  for (const title of ['RealLive', 'Kirikiri']) {
    const output = path.join(iconDir, title);
    const result = spawnSync(python, [generator, title, output],
      {encoding: 'utf8'});
    assert(result.status === 0,
      'PWA icon generation failed: ' + result.stderr);
    for (const size of [180, 192, 512]) {
      const png = fs.readFileSync(path.join(output, 'pwa-icon-' + size + '.png'));
      assert(png.subarray(0, 8).toString('hex') ===
          '89504e470d0a1a0a' && png.readUInt32BE(16) === size &&
          png.readUInt32BE(20) === size,
        'Per-game PWA icon must be a valid PNG of the requested size');
    }
  }
  assert(!fs.readFileSync(path.join(iconDir, 'RealLive', 'pwa-icon-192.png'))
    .equals(fs.readFileSync(path.join(iconDir, 'Kirikiri', 'pwa-icon-192.png'))),
  'Different game PWAs should have visually distinct icons');
} finally {
  fs.rmSync(iconDir, {recursive: true, force: true});
}
const scripts = [...html.matchAll(/<script(?![^>]*\bsrc=)[^>]*>([\s\S]*?)<\/script>/gi)];
assert(scripts.length >= 2, 'HTML shell must contain bootstrap and folder scripts');
for (let i = 0; i < scripts.length; ++i) {
  new vm.Script(scripts[i][1], {
    filename: 'index.html.in:inline-script-' + String(i + 1),
  });
}

for (const id of [
  'canvas', 'loading-overlay', 'asset-source-actions', 'local-assets-folder',
  'select-local-assets', 'use-server-assets', 'add-browser-folder',
  'add-browser-folder-input', 'browser-folder-status',
]) {
  assert(html.includes('id="' + id + '"'), 'Missing web control: ' + id);
}
for (const symbol of [
  'KotonohaConfigureRemoteAssetIndexedManifest',
  'KotonohaMountBrowserFolder', 'KotonohaRequestRemoteAsset',
]) {
  assert(html.includes(symbol), 'Missing web bootstrap interface: ' + symbol);
}
assert(html.includes('id="loading-build"') &&
       html.includes('@KOTONOHA_VERSION_FULL@'),
  'Every browser game must display its CMake/Git version on the asset picker');
assert(html.includes('touch-action: none'), 'Touch gesture handling must stay active');
assert(html.includes('id="browser-folder-tools" data-i18n-aria-label="gameFolders"'),
  'Pre-game toolbar must be an ordinary fixed element, not a Safari popover');
assert(!html.includes('popover="manual"') && !html.includes('showPopover('),
  'Pre-game controls must not enter the browser popover top layer');
assert(!html.includes('inset: auto;'),
  'CSS inset shorthand must not reset the explicit top/right positions');
assert(html.includes('top: max(8px, env(safe-area-inset-top, 0px));') &&
  html.includes('right: max(8px, env(safe-area-inset-right, 0px));'),
  'Pre-game toolbar must stay anchored to the safe-area top-right corner');
assert(!html.includes('showPersistentTools'),
  'Fullscreen must not reopen the folder toolbar after gameplay starts');
assert(html.includes('window.KotonohaCloseStartupFolderTools'),
  'The startup selector must have an explicit close operation');
assert(html.includes('#browser-folder-tools[hidden] { display: none !important; }'),
  'The hidden folder toolbar must not be displayed over gameplay');
assert(html.includes('toolbar.hidden = true'),
  'Folder toolbar must be hidden when the source is chosen');
assert(!html.includes('KotonohaAutoServerAssets'),
  'Browser startup must not automatically select remote assets');
assert(html.includes('new URL("manifest.json", document.baseURI)'),
  'HTTP startup must read the page-root asset manifest');
assert(html.includes('KotonohaConfigureRemoteAssetIndexedManifest') &&
       !html.includes('KotonohaConfigureRemoteAssetUrl') &&
       !webBridge.includes('KotonohaConfigureRemoteAssetUrl') &&
       html.includes('parsed.entries.length === 0'),
  'All HTTP games require non-empty manifest entries without direct fallback');
assert(html.includes('window.KotonohaRemoteManifestIndexed = true'),
  'Only a validated indexed manifest may start HTTP mode');
assert(html.includes('KotonohaAllocateRemoteManifest') &&
       html.includes('KotonohaFreeRemoteManifest') &&
       html.includes('new TextEncoder().encode(manifest)') &&
       html.includes('Module.HEAPU8') &&
       html.includes('["string", "number"]') &&
       !html.includes('["string", "string"],\n                                    [origin.href, manifest]'),
  'Large manifest JSON must be marshalled via bounded WASM heap, not ccall stack');
assert(html.includes('error instanceof WebAssembly.RuntimeError') &&
       html.includes('serverButton.disabled = true'),
  'A fatal WASM memory trap must not allow repeated broken startup attempts');
assert(wasmBuild.includes("'HEAPU8'"),
  'WebAssembly link must export the current heap view under memory growth');
assert(webBridge.includes('KotonohaAllocateRemoteManifest') &&
       webBridge.includes('KotonohaFreeRemoteManifest') &&
       webBridge.includes('std::free(memory)'),
  'WASM bridge must allocate and free bounded manifest transfers');
assert(html.includes('if (!indexed)') &&
       html.includes('throw new Error(window.KotonohaText("manifestInvalid"))'),
  'Invalid manifests must block startup instead of probing HTTP');
assert(html.includes('window.KotonohaAssetCache.configure(origin.href, manifest)') &&
       html.includes('await window.KotonohaAssetCache.preloadIfPresent(') &&
       html.includes('window.KotonohaInitialPreloadReady = preloaded') &&
       html.includes('<script src="KotonohaPreloadCache.js"></script>'),
  'HTTP startup must finish mandatory preload before releasing the engine');
assert(persistentCache.includes('window.KotonohaDumpPreload = dumpPreload') &&
       persistentCache.includes('link.download = "preload.json"') &&
       persistentCache.includes('entries: await listDownloaded()') &&
       persistentCache.includes('await storeRange(path, start, bytes)'),
  'Console dump must export actually downloaded ranges to the preload guide');
assert(persistentCache.includes('await readRange(path, start, length)') &&
       persistentCache.includes('await fetchRequiredRange(path, start, length)'),
  'Mandatory preload must reuse IndexedDB and fetch missing ranges');
assert(browserTransport.includes('KotonohaCompleteBrowserFetch') &&
       browserTransport.includes('window.KotonohaAssetCache.fetchJob') &&
       webBridge.includes('KotonohaAllocateRemoteFetchBuffer'),
  'The C++ Fetch provider must delegate HTTP Range reads to the persistent cache');
assert(wasmBuild.includes('lib/web/KotonohaPreloadCache.js'),
  'Every WASM output must bundle its cache manager');
assert(html.includes('setStatus("chooseSource");'),
  'Boot must wait for an explicit source choice');
assert(html.includes('setStatus("localReady");') &&
       persistentCache.includes('!indexedSizes.has(canonical(path))'),
  'Local startup must stay independent and remote files must be allow-listed');
assert(html.includes('id="kotonoha-lang-en"') &&
       html.includes('id="kotonoha-lang-pt"') &&
       html.includes('window.KotonohaSetLanguage') &&
       html.includes('localStorage.setItem("kotonoha-language"') &&
       html.includes('setStatus("preloadProgress"') &&
       html.includes('setStatus("searchingIndex"') &&
       html.includes('setStatus("memoryFatal"') &&
       html.includes('data-i18n-title="mountFolder"') &&
       html.includes('data-i18n-aria-label="gameCanvas"'),
  'Startup UI, errors and preload progress must be localized in EN/PT');
assert(persistentCache.includes('records.index("byLastAccess").openCursor()') &&
       persistentCache.includes('isBootProtected(row)') &&
       persistentCache.includes('cacheStats') &&
       persistentCache.includes('isQuotaError') &&
       persistentCache.includes('touchChunk(row.key)'),
  'The persistent asset cache must implement bounded transactional LRU');

console.log('Kotonoha HTML shell static checks passed (' + scripts.length + ' scripts).');
