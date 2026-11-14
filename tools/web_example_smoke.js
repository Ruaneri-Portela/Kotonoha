#!/usr/bin/env node
// Browser startup and input smoke for the bundled, copyright-safe example game.
// This is deliberately not a certificate for retail game playthrough parity.
'use strict';

const { spawn } = require('node:child_process');
const { createRequire } = require('node:module');
const path = require('node:path');
const fs = require('node:fs');
const { strict: assert } = require('node:assert');

const output = path.resolve(process.argv[2] || 'build-web/outputs/Kotonoha');
const port = Number(process.argv[3] || process.env.KOTONOHA_WEB_SMOKE_PORT || 8766);
const base = 'http://127.0.0.1:' + port;

function playwright() {
  try { return require('playwright'); } catch (_) {
    const fromCwd = createRequire(path.join(process.cwd(), 'noop.js'));
    return fromCwd('playwright');
  }
}
async function run() {
  assert(fs.existsSync(path.join(output, 'index.html')),
    'Example WASM output missing. Build the Kotonoha target.');
  const server = spawn('python3', [
    '-m', 'http.server', String(port), '--directory', output
  ], { stdio: 'ignore' });
  let browser;
  try {
    let online = false;
    for (let attempt = 0; attempt < 100; ++attempt) {
      try {
        const response = await fetch(base + '/index.html');
        if (response.ok) { online = true; break; }
      } catch (_) {}
      await new Promise(resolve => setTimeout(resolve, 100));
    }
    assert(online, 'Example HTTP server did not start');
    browser = await playwright().chromium.launch({ headless: true });
    for (const touch of [false, true]) {
      const context = await browser.newContext({
        hasTouch: touch, isMobile: touch,
        viewport: {width: 960, height: 640}
      });
      const page = await context.newPage();
      const pageErrors = [];
      page.on('pageerror', error => pageErrors.push(error.message));
      await page.goto(base + '/index.html', { waitUntil: 'domcontentloaded' });
      await page.waitForFunction(() => typeof Module !== 'undefined' &&
        !!document.getElementById('canvas') &&
        document.getElementById('canvas').width > 0,
        null, { timeout: 120000 });
      await page.locator('#loading-overlay')
        .waitFor({ state: 'detached', timeout: 120000 });
      const canvas = page.locator('#canvas');
      if (touch) {
        await page.touchscreen.tap(480, 320);
      } else {
        await canvas.click({ position: {x: 320, y: 240} });
      }
      await page.waitForTimeout(350);
      assert(pageErrors.length === 0,
        'Example (' + (touch ? 'touch' : 'mouse') +
          ') JS exception: ' + pageErrors.join('; '));
      const state = await page.evaluate(() => ({
        width: document.getElementById('canvas').width,
        height: document.getElementById('canvas').height,
        abort: document.getElementById('loading-status')?.textContent || ''
      }));
      assert(state.width > 0 && state.height > 0,
        'Example canvas did not initialize');
      assert(!state.abort.startsWith('Erro fatal:'),
        'WASM abort reported during example startup');
      console.log('EXAMPLE_SMOKE', touch ? 'touch' : 'mouse',
        JSON.stringify(state));
      await context.close();
    }
  } finally {
    if (browser) await browser.close();
    server.kill('SIGTERM');
  }
}
run().catch(error => { console.error(error); process.exitCode = 1; });
