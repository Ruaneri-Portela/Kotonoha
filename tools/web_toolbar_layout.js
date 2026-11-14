#!/usr/bin/env node
// Cross-engine Safari/WebKit and Chromium regression for the pre-game picker.
// The picker must be in the top-right safe area, never a centered popover.
// Run after building KotonohaWebSelfTest and installing Playwright browsers.
'use strict';

const assert = require('node:assert/strict');
const { spawn } = require('node:child_process');
const { createRequire } = require('node:module');
const fs = require('node:fs');
const path = require('node:path');

const output = path.resolve(process.argv[2] || 'build-web/outputs/KotonohaWebSelfTest');
const port = Number(process.argv[3] || process.env.KOTONOHA_WEB_TOOLBAR_PORT || 8767);
const baseUrl = 'http://127.0.0.1:' + port;

function browsers() {
  try { return require('playwright'); } catch (_) {
    return createRequire(path.join(process.cwd(), 'noop.js'))('playwright');
  }
}

async function checkViewport(page, name, size) {
  await page.setViewportSize(size);
  await page.waitForFunction(() => {
    const button = document.getElementById('add-browser-folder');
    const toolbar = document.getElementById('browser-folder-tools');
    return button && toolbar && !toolbar.hidden &&
      getComputedStyle(toolbar).display !== 'none';
  }, null, { timeout: 30000 });
  const geometry = await page.evaluate(() => {
    const toolbar = document.getElementById('browser-folder-tools');
    const button = document.getElementById('add-browser-folder');
    const style = getComputedStyle(toolbar);
    const rect = button.getBoundingClientRect();
    const toolbarRect = toolbar.getBoundingClientRect();
    const x = rect.left + rect.width / 2;
    const y = rect.top + rect.height / 2;
    const topmost = document.elementFromPoint(x, y);
    return {
      fixed: style.position === 'fixed',
      noPopover: !toolbar.hasAttribute('popover'),
      top: toolbarRect.top,
      right: window.innerWidth - toolbarRect.right,
      cssTop: parseFloat(style.top),
      cssRight: parseFloat(style.right),
      width: rect.width,
      height: rect.height,
      clickable: topmost === button || button.contains(topmost),
      viewWidth: window.innerWidth,
      viewHeight: window.innerHeight
    };
  });
  assert(geometry.fixed && geometry.noPopover,
    name + ' toolbar must be a normal fixed element');
  assert(geometry.top >= 0 && geometry.right >= 0 &&
    Math.abs(geometry.top - geometry.cssTop) <= 4 &&
    Math.abs(geometry.right - geometry.cssRight) <= 4,
    name + ' top-right positioning mismatch: ' + JSON.stringify(geometry));
  assert(geometry.top <= 85 && geometry.right <= 85,
    name + ' Safari placed folder button away from the corner: ' +
    JSON.stringify(geometry));
  assert(geometry.width >= 100 && geometry.height >= 44 && geometry.clickable,
    name + ' picker must remain accessible: ' + JSON.stringify(geometry));
  console.log('[Kotonoha] ' + name + ' ' + size.width + 'x' + size.height +
    ' top=' + geometry.top + ' right=' + geometry.right);
}

async function run() {
  assert(fs.existsSync(path.join(output, 'index.html')),
    'Build KotonohaWebSelfTest before running browser UI checks.');
  const server = spawn('python3', [
    path.join(__dirname, 'serve_web.py'), '--root', output, '--port', String(port)
  ], { stdio: 'ignore' });
  try {
    let online = false;
    for (let i = 0; i < 100; ++i) {
      try {
        const response = await fetch(baseUrl + '/index.html');
        if (response.ok) { online = true; break; }
      } catch (_) {}
      await new Promise(resolve => setTimeout(resolve, 100));
    }
    assert(online, 'HTTP server did not start');
    const { chromium, webkit } = browsers();
    for (const [name, driver] of [['Chromium', chromium], ['WebKit', webkit]]) {
      const browser = await driver.launch({ headless: true });
      try {
        const context = await browser.newContext({
          viewport: { width: 390, height: 844 },
          isMobile: true,
          hasTouch: true
        });
        try {
          const page = await context.newPage();
          await page.goto(baseUrl + '/index.html',
            { waitUntil: 'domcontentloaded' });
          for (const size of [
            { width: 390, height: 844 },
            { width: 844, height: 390 },
            { width: 1440, height: 900 }
          ]) {
            await checkViewport(page, name, size);
          }
        } finally {
          await context.close();
        }
      } finally {
        await browser.close();
      }
    }
  } finally {
    server.kill('SIGTERM');
  }
}

run().catch(error => {
  console.error('[Kotonoha] Browser toolbar regression FAILED', error);
  process.exitCode = 1;
});
