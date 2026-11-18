#!/usr/bin/env node
// Source-only guard for the configured Emscripten HTML shell.
// This does NOT replace the Playwright/Emscripten integration suite.
'use strict';

const fs = require('node:fs');
const path = require('node:path');
const vm = require('node:vm');
const assert = require('node:assert/strict');

const htmlPath = path.join(__dirname, '..', 'buildsystems', 'cmake', 'index.html.in');
const html = fs.readFileSync(htmlPath, 'utf8');
const wasmBuild = fs.readFileSync(path.join(__dirname, '..',
  'buildsystems', 'cmake', 'KotonohaApp.cmake'), 'utf8');
const webBridge = fs.readFileSync(path.join(__dirname, '..',
  'lib', 'src', 'core', 'WebAssetFetch.cpp'), 'utf8');
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
  'KotonohaConfigureRemoteAssetUrl',
  'KotonohaMountBrowserFolder', 'KotonohaRequestRemoteAsset',
]) {
  assert(html.includes(symbol), 'Missing web bootstrap interface: ' + symbol);
}
assert(html.includes('id="loading-build"') &&
       html.includes('@KOTONOHA_VERSION_FULL@'),
  'Every browser game must display its CMake/Git version on the asset picker');
assert(html.includes('touch-action: none'), 'Touch gesture handling must stay active');
assert(html.includes('id="browser-folder-tools" aria-label="Pastas do jogo"'),
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
assert(html.includes('new URL("manifest.json", origin)'),
  'HTTP startup must probe the optional manifest after explicit source selection');
assert(html.includes('KotonohaConfigureRemoteAssetIndexedManifest') &&
       html.includes('KotonohaConfigureRemoteAssetUrl'),
  'HTTP bootstrap must support indexed listings and direct Fetch fallback');
assert(html.includes('window.KotonohaRemoteManifestIndexed = indexed'),
  'The source selection must report whether the optional index was used');
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
       html.includes('const loadServerAssets = async function()'),
  'Manifest failures must fall back to direct Fetch asynchronously');
assert(html.includes('setStatus("Escolha pasta local ou HTTP(S) para iniciar.")'),
  'Boot must wait for an explicit source choice');
assert(html.includes('setStatus("Pasta local montada. Iniciando engine...");'),
  'Local startup must finish without a server manifest probe');

console.log('Kotonoha HTML shell static checks passed (' + scripts.length + ' scripts).');
