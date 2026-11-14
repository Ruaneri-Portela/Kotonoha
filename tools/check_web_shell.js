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
  'KotonohaConfigureRemoteAssetUrl', 'KotonohaConfigureRemoteAssetManifest',
  'KotonohaMountBrowserFolder', 'KotonohaRequestRemoteAsset',
]) {
  assert(html.includes(symbol), 'Missing web bootstrap interface: ' + symbol);
}
assert(html.includes('touch-action: none'), 'Touch gesture handling must stay active');
assert(html.includes('id="browser-folder-tools" popover="manual"'),
  'Persistent folder controls must live in the browser top layer');
assert(html.includes('document.addEventListener("fullscreenchange", showPersistentTools)'),
  'Fullscreen must re-promote the browser folder toolbar');
assert(!html.includes('KotonohaAutoServerAssets'),
  'Browser startup must not automatically select remote assets');
assert(html.includes('setStatus("Escolha pasta local ou HTTP(S) para iniciar.")'),
  'Boot must wait for an explicit source choice');
assert(html.includes('setStatus("Pasta local montada. Iniciando engine...");'),
  'Local startup must finish without a server manifest probe');

console.log('Kotonoha HTML shell static checks passed (' + scripts.length + ' scripts).');
