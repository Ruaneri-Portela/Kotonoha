#!/usr/bin/env node
// WebAssembly integration self-test.
//
// Prerequisites:
//   1. Build with -DKOTONOHA_WEB_SELFTEST=ON.
//   2. Install the dev-only browser driver:
//        npm install --no-save playwright
//        npx playwright install chromium
//   3. ffmpeg and python3 must be available.
//
// Usage:
//   node tools/web_selftest.js [output-dir] [port]
//
// The test exercises the real C++ paths, not the JS bridge directly:
//   - VNSave::write -> IDBFS sync -> page reload -> VNSave::read
//   - MediaPlayer::open(assets, id) -> provider localPath -> KotonohaPlayVideo
// The MEMFS video is a zero-byte placeholder (as produced by deferred manifest
// loading); the actual cutscene bytes come from the HTTP game-assets URL.

const { spawn, spawnSync } = require('node:child_process');
const fs = require('node:fs');
const path = require('node:path');
const { createRequire } = require('node:module');

function loadChromium() {
  try {
    return require('playwright').chromium;
  } catch (error) {
    // Also accept a Playwright install in the current working directory when
    // the script lives in the source tree.
    try {
      const requireFromCwd = createRequire(path.join(process.cwd(), 'noop.js'));
      return requireFromCwd('playwright').chromium;
    } catch (fallbackError) {
      console.error('Playwright is required. Run:\n' +
        '  npm install --no-save playwright\n' +
        '  npx playwright install chromium');
      process.exit(1);
    }
  }
}

const chromium = loadChromium();

const output = path.resolve(process.argv[2] || 'build-web/outputs/RealLive');
const port = Number(process.argv[3] || process.env.KOTONOHA_WEB_PORT || 8765);
const baseUrl = `http://127.0.0.1:${port}`;
const videoPath = path.join(output, 'game-assets', 'selftest.webm');

function assert(condition, message) {
  if (!condition)
    throw new Error('ASSERT FAILED: ' + message);
}

function ensureVideo() {
  fs.mkdirSync(path.dirname(videoPath), { recursive: true });
  if (fs.existsSync(videoPath) && fs.statSync(videoPath).size > 0)
    return;
  const result = spawnSync('ffmpeg', [
    '-hide_banner', '-loglevel', 'error',
    '-f', 'lavfi', '-i', 'testsrc=size=320x240:rate=30', '-t', '1',
    '-c:v', 'libvpx-vp9', '-b:v', '500k', '-an', '-y', videoPath,
  ], { stdio: 'inherit' });
  if (result.error || result.status !== 0)
    throw new Error('ffmpeg is required to generate ' + videoPath);
}

async function waitForServer() {
  for (let i = 0; i < 80; ++i) {
    try {
      const response = await fetch(baseUrl + '/index.html');
      if (response.ok)
        return;
    } catch (error) {
      // The server may not have bound the port yet.
    }
    await new Promise(resolve => setTimeout(resolve, 100));
  }
  throw new Error('HTTP server did not start on ' + baseUrl);
}

async function main() {
  ensureVideo();
  const server = spawn('python3', [
    '-m', 'http.server', String(port), '--directory', output,
  ], { stdio: ['ignore', 'ignore', 'pipe'] });
  let serverError = '';
  server.stderr.on('data', chunk => { serverError += chunk; });

  let browser;
  try {
    await waitForServer();
    browser = await chromium.launch({ headless: true });
    const page = await browser.newPage();
    const logs = [];
    page.on('console', message =>
      logs.push(`${message.type()}: ${message.text()}`));
    page.on('pageerror', error =>
      logs.push(`pageerror: ${error.message}\n${error.stack || ''}`));

    const ready = async () => {
      await page.waitForFunction(
        () => typeof Module === 'object' && Module.KotonohaSaveReady === true,
        null, { timeout: 60000 });
      await page.waitForFunction(
        () => document.getElementById('canvas').width > 320,
        null, { timeout: 60000 });
    };

    await page.goto(baseUrl + '/index.html', { waitUntil: 'domcontentloaded' });
    await ready();

    const prepared = await page.evaluate(() => {
      try {
        Module.FS_createPath('/', 'game-assets', true, true);
      } catch (error) {
        // The manifest loader may have created the directory already.
      }
      let placeholderError = null;
      try {
        Module.FS_createDataFile('/game-assets', 'selftest.webm',
          new Uint8Array(0), true, true, true);
      } catch (error) {
        placeholderError = String(error);
      }
      let placeholderSize = -1;
      try {
        placeholderSize = Module.FS_readFile('/game-assets/selftest.webm').length;
      } catch (error) {
        placeholderError = String(error);
      }
      return {
        write: Module.ccall('KotonohaWebSelfTestWriteSave', 'number', [], []),
        placeholderSize,
        placeholderError,
      };
    });
    console.log('PREPARE', JSON.stringify(prepared));
    assert(prepared.write === 1, 'VNSave::write succeeded');
    assert(prepared.placeholderSize === 0,
      'deferred video placeholder is zero bytes');

    const beforeReload = await page.evaluate(async () => {
      const savePath =
        Module.ccall('KotonohaWebSelfTestSavePath', 'string', [], []);
      const raw = Module.FS_readFile(savePath, { encoding: 'utf8' });
      // VNSave::read flushes too, so read before the barrier. The final
      // awaited sync then covers every operation queued by write/read.
      const parsed =
        Module.ccall('KotonohaWebSelfTestReadSave', 'string', [], []);
      const syncError = await window.KotonohaSyncSave();
      return {
        savePath,
        raw,
        parsed,
        syncError: syncError ? String(syncError) : null,
      };
    });
    console.log('BEFORE_RELOAD', JSON.stringify(beforeReload));
    assert(beforeReload.raw.includes('KOTONOHA_SAVE 1'),
      'save file exists in /save');
    assert(beforeReload.syncError === null, 'IDBFS sync completed');
    assert(beforeReload.parsed ===
      'node=4242;mode=timed;position=12.5;choices=3;data=web-selftest',
      'VNSave::read returned the written state');

    await page.reload({ waitUntil: 'domcontentloaded' });
    await ready();

    const afterReload = await page.evaluate(() => {
      const savePath =
        Module.ccall('KotonohaWebSelfTestSavePath', 'string', [], []);
      let raw = '';
      try {
        raw = Module.FS_readFile(savePath, { encoding: 'utf8' });
      } catch (error) {
        raw = 'ERR ' + error;
      }
      return {
        savePath,
        raw,
        parsed: Module.ccall('KotonohaWebSelfTestReadSave', 'string', [], []),
      };
    });
    console.log('AFTER_RELOAD', JSON.stringify(afterReload));
    assert(afterReload.raw.includes('KOTONOHA_SAVE 1'),
      'save file restored from IndexedDB after reload');
    assert(afterReload.parsed ===
      'node=4242;mode=timed;position=12.5;choices=3;data=web-selftest',
      'VNSave::read restored the state after reload');

    const opened = await page.evaluate(() =>
      Module.ccall('KotonohaWebSelfTestOpenVideo', 'number', [], []));
    console.log('OPEN_VIDEO', opened);
    assert(opened === 1, 'MediaPlayer::open(assets, videoId) succeeded');

    const video = await page.evaluate(async () => {
      const call = (name, type) => Module.ccall(name, type, [], []);
      let duration = -1, hasFrame = 0;
      for (let i = 0; i < 100; ++i) {
        duration = call('KotonohaWebSelfTestVideoDuration', 'number');
        hasFrame = call('KotonohaWebSelfTestVideoHasFrame', 'number');
        if (duration > 0.9 && hasFrame === 1)
          break;
        await new Promise(resolve => setTimeout(resolve, 100));
      }
      const elapsedBefore = call('KotonohaWebSelfTestVideoElapsed', 'number');
      await new Promise(resolve => setTimeout(resolve, 300));
      const elapsedAfter = call('KotonohaWebSelfTestVideoElapsed', 'number');
      const element = document.getElementById('kotonoha-video-player');
      return {
        duration,
        hasFrame,
        elapsedBefore,
        elapsedAfter,
        finished: call('KotonohaWebSelfTestVideoFinished', 'number'),
        display: element.style.display,
        paused: element.paused,
        muted: element.muted,
        readyState: element.readyState,
        currentSrc: element.currentSrc,
        videoSize: [element.videoWidth, element.videoHeight],
      };
    });
    console.log('VIDEO', JSON.stringify(video));
    assert(video.duration > 0.9 && video.duration < 1.2,
      'MediaPlayer duration came from HTML5');
    assert(video.hasFrame === 1, 'HTML5 video produced a frame');
    assert(video.display === 'block', 'HTML5 overlay is visible');
    assert(video.currentSrc.endsWith('/game-assets/selftest.webm'),
      'MediaPlayer passed the game-assets URL to KotonohaPlayVideo');
    assert(video.paused === false, 'HTML5 video is playing');
    assert(video.videoSize[0] === 320 && video.videoSize[1] === 240,
      'HTML5 video decoded the 4:3 source');
    assert(video.elapsedAfter >= video.elapsedBefore,
      'MediaPlayer clock follows HTML5 time');

    const closed = await page.evaluate(() => {
      Module.ccall('KotonohaWebSelfTestCloseVideo', null, [], []);
      const element = document.getElementById('kotonoha-video-player');
      return { display: element.style.display, paused: element.paused };
    });
    console.log('CLOSED', JSON.stringify(closed));
    assert(closed.display === 'none',
      'closing MediaPlayer stops the HTML5 overlay');

    console.log('RESULT PASS');
    const knownTeardownError = logs.some(line =>
      line.startsWith('pageerror: null function'));
    if (knownTeardownError)
      console.log('NOTE: ignored a pre-existing SDL teardown error on reload');
  } catch (error) {
    if (serverError)
      console.error(serverError);
    throw error;
  } finally {
    if (browser)
      await browser.close();
    server.kill('SIGTERM');
  }
}

main().catch(error => {
  console.error('WEB SELFTEST FAILED:', error);
  process.exit(1);
});
