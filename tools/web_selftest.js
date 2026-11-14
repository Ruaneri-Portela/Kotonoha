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
// The test exercises the real C++ provider, not a MEMFS HTTP staging path:
//   - VNSave::write -> IDBFS sync -> page reload -> VNSave::read
//   - HTTP manifest -> FetchAssetProvider -> FileHandle -> HTML5 Blob
//   - Local game folder -> FileHandle -> HTML5 Blob or FFmpeg.


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

const output = path.resolve(process.argv[2] || 'build-web/outputs/KotonohaWebSelfTest');
const port = Number(process.argv[3] || process.env.KOTONOHA_WEB_PORT || 8765);
const baseUrl = `http://127.0.0.1:${port}`;
const videoPath = path.join(output, 'game-assets', 'selftest.webm');
const audioPath = path.join(output, 'game-assets', 'selftest.mp3');

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

function ensureAudio() {
  const result = spawnSync('ffmpeg', [
    '-hide_banner', '-loglevel', 'error',
    '-f', 'lavfi', '-i', 'sine=frequency=440:sample_rate=48000',
    '-t', '2', '-c:a', 'libmp3lame', '-y', audioPath,
  ], { stdio: 'inherit' });
  if (result.error || result.status !== 0)
    throw new Error('Cannot generate browser audio fixture');
  fs.copyFileSync(audioPath,
    path.join(output, 'game-assets', 'selftest2.mp3'));
  const mp2 = spawnSync('ffmpeg', [
    '-hide_banner', '-loglevel', 'error',
    '-f', 'lavfi', '-i', 'sine=frequency=330:sample_rate=48000',
    '-t', '2', '-c:a', 'mp2', '-f', 'mp2', '-y',
    path.join(output, 'game-assets', 'selftest.mp2'),
  ], { stdio: 'inherit' });
  if (mp2.error || mp2.status !== 0)
    throw new Error('Cannot generate unsupported browser audio fixture');
}

function ensureLegacySidecar() {
  const source = process.env.KOTONOHA_WEB_TEST_MPEG;
  if (!source) return false;
  const target = path.join(output, 'game-assets', 'legacy.mpg.webm');
  const direct = path.join(output, 'game-assets', 'legacy-direct.mpg');
  const copy = spawnSync('ffmpeg', [
    '-hide_banner', '-loglevel', 'error', '-i', source, '-t', '2',
    '-c', 'copy', '-y', direct,
  ], { stdio: 'inherit' });
  if (copy.error || copy.status !== 0)
    throw new Error('Cannot create legacy MPEG fixture');
  const result = spawnSync('ffmpeg', [
    '-hide_banner', '-loglevel', 'error', '-i', source, '-t', '2',
    '-c:v', 'libvpx-vp9', '-b:v', '500k', '-c:a', 'libopus', '-y', target,
  ], { stdio: 'inherit' });
  if (result.error || result.status !== 0)
    throw new Error('Cannot transcode legacy video fixture');
  return true;
}

function ensureCustomFolderFixture() {
  const root = path.join(output, 'forced_subtitles');
  fs.mkdirSync(root, { recursive: true });
  fs.writeFileSync(path.join(root, 'web-test.ass'),
    '[Script Info]\\nTitle: Kotonoha web fixture\\n');
  const live = path.join(output, 'live_import');
  fs.mkdirSync(live, { recursive: true });
  fs.writeFileSync(path.join(live, 'live-extra.txt'), 'Kotonoha hot import');
}

function updateManifest() {
  // Fixture files are generated AFTER the CMake manifest custom command.
  // Recreate the HTTP index so the actual FetchAssetProvider sees these bytes.
  const root = path.join(output, 'game-assets');
  const entries = [];
  const traverse = (folder, prefix = '') => {
    for (const item of fs.readdirSync(folder, { withFileTypes: true })) {
      const relative = prefix ? prefix + '/' + item.name : item.name;
      const absolute = path.join(folder, item.name);
      if (item.isDirectory()) {
        traverse(absolute, relative);
      } else if (item.isFile() &&
                 item.name !== 'manifest.json' &&
                 item.name !== 'README.txt') {
        entries.push({path: relative, size: fs.statSync(absolute).size});
      }
    }
  };
  traverse(root);
  entries.sort((a, b) => a.path.localeCompare(b.path));
  fs.writeFileSync(path.join(root, 'manifest.json'),
                   JSON.stringify({entries, files: entries.map(e => e.path)}));
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
  ensureAudio();
  ensureCustomFolderFixture();
  const testLegacySidecar = ensureLegacySidecar();
  updateManifest();
  const server = spawn('python3', [
    '-m', 'http.server', String(port), '--directory', output,
  ], { stdio: ['ignore', 'ignore', 'pipe'] });
  let serverError = '';
  server.stderr.on('data', chunk => { serverError += chunk; });

  let browser;
  const logs = [];
  try {
    await waitForServer();
    browser = await chromium.launch({ headless: true });
    const page = await browser.newPage();
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
    await page.locator('#select-local-assets').waitFor({ state: 'visible' });
    await page.locator('#local-assets-folder').setInputFiles(
      path.join(output, 'game-assets'));
    await ready();
    const local = await page.evaluate(async () => {
      const bytes = Module.FS_readFile('/game-assets/selftest.webm');
      const opened = Module.ccall('KotonohaWebSelfTestOpenVideo', 'number', [], []);
      const element = document.getElementById('kotonoha-video-player');
      let hasFrame = 0;
      for (let i = 0; i < 50 && !hasFrame; ++i) {
        hasFrame = Module.ccall('KotonohaWebSelfTestPumpVideo', 'number', [], []) &&
          Module.ccall('KotonohaWebSelfTestVideoHasFrame', 'number', [], []);
        if (!hasFrame) await new Promise(resolve => setTimeout(resolve, 100));
      }
      const videoSrc = element.src;
      const audioOpened = Module.ccall('KotonohaWebSelfTestOpenAudioId',
        'number', ['string'], ['selftest.mp3']);
      let audioDuration = -1;
      for (let i = 0; i < 50 && audioDuration < 1.8; ++i) {
        Module.ccall('KotonohaWebSelfTestPumpVideo', 'number', [], []);
        audioDuration = Module.ccall('KotonohaWebSelfTestVideoDuration',
          'number', [], []);
        if (audioDuration < 1.8)
          await new Promise(resolve => setTimeout(resolve, 100));
      }
      return { bytes: bytes.length, opened, src: videoSrc, hasFrame,
        audioOpened, audioDuration };
    });
    console.log('LOCAL_FOLDER', JSON.stringify(local));
    assert(local.bytes > 0, 'local folder copied the movie into MEMFS');
    assert(local.opened === 1 && local.src.startsWith('blob:') &&
      local.hasFrame === 1, 'local movie decodes from browser File URL');
    assert(local.audioOpened === 1 && local.audioDuration > 1.8,
      'local audio decodes from selected folder');

    // The corner control mounts any requested root without assuming the
    // selected directory is named game-assets.
    page.once('dialog', dialog => dialog.accept('forced_subtitles'));
    await page.locator('#add-browser-folder-input').setInputFiles(
      path.join(output, 'forced_subtitles'));
    await page.waitForFunction(() =>
      document.getElementById('browser-folder-status').textContent
        .includes('arquivo(s) montado(s)'));
    const custom = await page.evaluate(() => ({
      contents: Module.FS_readFile('/forced_subtitles/web-test.ass',
        { encoding: 'utf8' }),
      status: document.getElementById('browser-folder-status').textContent
    }));
    assert(custom.contents.includes('Kotonoha web fixture'),
      'custom named directory is mounted in the Emscripten VFS');

    // Hold an existing DirectoryMount, add new files to the game-assets
    // root after SDL_AppInit, and verify refresh without re-creating mount.
    const beforeImport = await page.evaluate(() => Module.ccall(
      'KotonohaWebSelfTestLiveLocalSize', 'number',
      ['string'], ['selftest.webm']));
    assert(beforeImport > 0, 'live local mount was created');
    page.once('dialog', dialog => dialog.accept('game-assets'));
    await page.locator('#add-browser-folder-input').setInputFiles(
      path.join(output, 'live_import'));
    await page.waitForFunction(() =>
      document.getElementById('browser-folder-status').textContent
        .includes('/game-assets'));
    const afterImport = await page.evaluate(() => Module.ccall(
      'KotonohaWebSelfTestLiveLocalSize', 'number',
      ['string'], ['live-extra.txt']));
    assert(afterImport === Buffer.byteLength('Kotonoha hot import'),
      'already-mounted game-assets root detects new browser files');

    // Force a real manifest-free server boot, then read the asset directly
    // from the C++ mount. Python http.server ignores Range: the provider
    // should accept its complete HTTP 200 response for small files.
    const manifestPath = path.join(output, 'game-assets', 'manifest.json');
    const backupPath = manifestPath + '.testing';
    fs.renameSync(manifestPath, backupPath);
    try {
      await page.goto(baseUrl + '/index.html',
        { waitUntil: 'domcontentloaded' });
      await page.locator('#use-server-assets').click();
      await ready();
      const direct = await page.evaluate(async () => {
        const size = await Module.ccall('KotonohaWebSelfTestRemoteSize',
          'number', ['string'], ['selftest.webm']);
        const missing = await Module.ccall('KotonohaWebSelfTestRemoteSize',
          'number', ['string'], ['missing-selftest.bin']);
        let staged = false;
        try {
          Module.FS_readFile('/game-assets/selftest.webm');
          staged = true;
        } catch (_) { /* Remote Fetch must not be copied to MEMFS. */ }
        return {
          size, missing, staged,
          configured: typeof window.KotonohaFetchAsset === 'function'
        };
      });
      console.log('MANIFEST_FREE', JSON.stringify(direct));
      assert(direct.configured && direct.size > 0 &&
        direct.missing === -1 && !direct.staged,
        'manifest-free HEAD/Range Fetch works without MEMFS staging');
    } finally {
      fs.renameSync(backupPath, manifestPath);
    }

    await page.goto(baseUrl + '/index.html', { waitUntil: 'domcontentloaded' });
    await page.locator('#use-server-assets').click();
    await ready();

    const prepared = await page.evaluate(() => {
      let staged = false;
      try {
        Module.FS_readFile('/game-assets/selftest.webm');
        staged = true;
      } catch (error) { /* Fetch data does not live in MEMFS. */ }
      return {
        write: Module.ccall('KotonohaWebSelfTestWriteSave', 'number', [], []),
        remoteSize: Module.ccall('KotonohaWebSelfTestRemoteSize',
                                 'number', ['string'], ['selftest.webm']),
        staged
      };
    });
    console.log('PREPARE', JSON.stringify(prepared));
    assert(prepared.write === 1, 'VNSave::write succeeded');
    assert(prepared.remoteSize > 0,
      'HTTP fallback bytes are readable via the virtual FileHandle');
    assert(prepared.staged === false,
      'HTTP resource must not be silently duplicated into MEMFS');


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
    await page.locator('#use-server-assets').click();
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
        call('KotonohaWebSelfTestPumpVideo', 'number');
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
      const probe = document.createElement('canvas');
      probe.width = element.videoWidth;
      probe.height = element.videoHeight;
      const probeContext = probe.getContext('2d');
      probeContext.drawImage(element, 0, 0);
      const sourcePixel = Array.from(probeContext.getImageData(
        Math.floor(probe.width * 0.2), Math.floor(probe.height * 0.5),
        1, 1).data);
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
        rendered: call('KotonohaWebSelfTestRenderVideo', 'number'),
        videoSize: [element.videoWidth, element.videoHeight],
        sourcePixel,
      };
    });
    console.log('VIDEO', JSON.stringify(video));
    assert(video.duration > 0.9 && video.duration < 1.2,
      'MediaPlayer duration came from HTML5');
    assert(video.hasFrame === 1, 'HTML5 video produced a frame');
    assert(video.display === 'none', 'HTML5 decoder stays behind SDL');
    assert(video.rendered === 1, 'browser frame renders through SDL');
    assert(video.currentSrc.startsWith('blob:'),
      'MediaPlayer provided a Blob URL, not the original HTTP URL');
    assert(video.paused === false, 'HTML5 video is playing');
    assert(video.videoSize[0] === 320 && video.videoSize[1] === 240,
      'HTML5 video decoded the 4:3 source');
    assert(video.elapsedAfter >= video.elapsedBefore,
      'MediaPlayer clock follows HTML5 time');

    const screenshot = await page.locator('#canvas').screenshot();
    const composite = await page.evaluate(async base64 => {
      const image = new Image();
      image.src = 'data:image/png;base64,' + base64;
      await image.decode();
      const surface = document.createElement('canvas');
      surface.width = image.width;
      surface.height = image.height;
      const context = surface.getContext('2d');
      context.drawImage(image, 0, 0);
      const sample = (x, y) => Array.from(context.getImageData(
        Math.floor(x * image.width), Math.floor(y * image.height), 1, 1).data);
      return { size: [image.width, image.height], center: sample(0.3125, 0.4167),
        movie: sample(0.25, 0.4167) };
    }, screenshot.toString('base64'));
    console.log('SDL_COMPOSITE', JSON.stringify(composite));
    assert(composite.center[0] > 200 && composite.center[1] < 60 &&
      composite.center[2] < 60,
      'SDL overlay is drawn above the browser-decoded movie');
    assert(composite.movie[0] + composite.movie[1] + composite.movie[2] > 60,
      'browser-decoded movie is visible in the SDL canvas');

    const closed = await page.evaluate(() => {
      Module.ccall('KotonohaWebSelfTestCloseVideo', null, [], []);
      const element = document.getElementById('kotonoha-video-player');
      return { display: element.style.display, paused: element.paused };
    });
    console.log('CLOSED', JSON.stringify(closed));
    assert(closed.display === 'none',
      'closing MediaPlayer stops the HTML5 overlay');

    const fallback = await page.evaluate(async () => {
      const response = await fetch('game-assets/selftest.webm');
      const bytes = new Uint8Array(await response.arrayBuffer());
      try { Module.FS_createPath('/', 'game-assets', true, true); }
      catch (error) { /* already present */ }
      Module.FS_createDataFile('/game-assets', 'fallback.webm',
        bytes, true, true, true);
      const opened = Module.ccall('KotonohaWebSelfTestOpenVideoId',
        'number', ['string'], ['fallback.webm']);
      const originalFailed = window.KotonohaVideoFailed;
      window.KotonohaVideoFailed = () => true;
      const pumped = Module.ccall('KotonohaWebSelfTestPumpVideo',
        'number', [], []);
      window.KotonohaVideoFailed = originalFailed;
      const hasFrame = Module.ccall('KotonohaWebSelfTestVideoHasFrame',
        'number', [], []);
      const element = document.getElementById('kotonoha-video-player');
      const hidden = element.style.display === 'none';
      Module.ccall('KotonohaWebSelfTestCloseVideo', null, [], []);
      return { opened, pumped, hasFrame, hidden };
    });
    console.log('BROWSER_FALLBACK', JSON.stringify(fallback));
    assert(fallback.opened === 1 && fallback.pumped === 1 &&
      fallback.hasFrame === 1 && fallback.hidden,
      'HTML5 failure switches to local FFmpeg decoding');

    const initialFailure = await page.evaluate(() => {
      const originalPlay = window.KotonohaPlayVideo;
      window.KotonohaPlayVideo = undefined;
      const opened = Module.ccall('KotonohaWebSelfTestOpenVideoId',
        'number', ['string'], ['fallback.webm']);
      window.KotonohaPlayVideo = originalPlay;
      const finishedBeforePump = Module.ccall(
        'KotonohaWebSelfTestVideoFinished', 'number', [], []);
      const pumped = Module.ccall('KotonohaWebSelfTestPumpVideo',
        'number', [], []);
      const hasFrame = Module.ccall('KotonohaWebSelfTestVideoHasFrame',
        'number', [], []);
      Module.ccall('KotonohaWebSelfTestCloseVideo', null, [], []);
      return { opened, finishedBeforePump, pumped, hasFrame };
    });
    console.log('BROWSER_INIT_FALLBACK', JSON.stringify(initialFailure));
    assert(initialFailure.opened === 1 &&
      initialFailure.finishedBeforePump === 0 &&
      initialFailure.pumped === 1 && initialFailure.hasFrame === 1,
      'HTML5 initialization failure also switches to local FFmpeg');

    const audio = await page.evaluate(async () => {
      const response = await fetch('game-assets/selftest.mp3');
      const bytes = new Uint8Array(await response.arrayBuffer());
      for (const name of ['selftest.mp3', 'selftest2.mp3'])
        Module.FS_createDataFile('/game-assets', name, bytes,
          true, true, true);
      const first = Module.ccall('KotonohaWebSelfTestOpenAudioId',
        'number', ['string'], ['selftest.mp3']);
      const second = Module.ccall('KotonohaWebSelfTestOpenSecondAudioId',
        'number', ['string'], ['selftest2.mp3']);
      Module.ccall('KotonohaWebSelfTestSetAudioGain', null,
        ['number'], [0.25]);
      let firstDuration = -1, secondDuration = -1;
      for (let i = 0; i < 100; ++i) {
        Module.ccall('KotonohaWebSelfTestPumpVideo', 'number', [], []);
        Module.ccall('KotonohaWebSelfTestPumpSecondAudio', 'number', [], []);
        firstDuration = Module.ccall('KotonohaWebSelfTestVideoDuration',
          'number', [], []);
        secondDuration = Module.ccall(
          'KotonohaWebSelfTestSecondAudioDuration', 'number', [], []);
        if (firstDuration > 1.8 && secondDuration > 1.8) break;
        await new Promise(resolve => setTimeout(resolve, 50));
      }
      const firstBefore = Module.ccall('KotonohaWebSelfTestVideoElapsed',
        'number', [], []);
      const secondBefore = Module.ccall(
        'KotonohaWebSelfTestSecondAudioElapsed', 'number', [], []);
      await new Promise(resolve => setTimeout(resolve, 250));
      const firstAfter = Module.ccall('KotonohaWebSelfTestVideoElapsed',
        'number', [], []);
      const secondAfter = Module.ccall(
        'KotonohaWebSelfTestSecondAudioElapsed', 'number', [], []);
      return { first, second, firstDuration, secondDuration,
        firstBefore, firstAfter, secondBefore, secondAfter,
        gain: Module.ccall('KotonohaWebSelfTestAudioGain', 'number', [], []),
        hasVideoFrame: Module.ccall('KotonohaWebSelfTestVideoHasFrame',
          'number', [], []) };
    });
    console.log('BROWSER_AUDIO', JSON.stringify(audio));
    assert(audio.first === 1 && audio.second === 1 &&
      audio.firstDuration > 1.8 && audio.secondDuration > 1.8 &&
      audio.firstAfter > audio.firstBefore &&
      audio.secondAfter > audio.secondBefore &&
      Math.abs(audio.gain - 0.25) < 0.001 && audio.hasVideoFrame === 0,
      'two browser audio sessions play independently with channel gain');
    await page.evaluate(() => Module.ccall('KotonohaWebSelfTestCloseVideo',
      null, [], []));

    const audioFallback = await page.evaluate(() => {
      const opened = Module.ccall('KotonohaWebSelfTestOpenAudioId',
        'number', ['string'], ['selftest.mp3']);
      const originalFailed = window.KotonohaMediaFailed;
      window.KotonohaMediaFailed = id => id >= 1000000 ? 1 : originalFailed(id);
      const pumped = Module.ccall('KotonohaWebSelfTestPumpVideo',
        'number', [], []);
      window.KotonohaMediaFailed = originalFailed;
      const duration = Module.ccall('KotonohaWebSelfTestVideoDuration',
        'number', [], []);
      const hasVideoFrame = Module.ccall('KotonohaWebSelfTestVideoHasFrame',
        'number', [], []);
      Module.ccall('KotonohaWebSelfTestCloseVideo', null, [], []);
      return { opened, pumped, duration, hasVideoFrame };
    });
    console.log('AUDIO_FALLBACK', JSON.stringify(audioFallback));
    assert(audioFallback.opened === 1 && audioFallback.pumped === 1 &&
      audioFallback.duration > 1.8 && audioFallback.hasVideoFrame === 0,
      'browser audio failure switches to local FFmpeg decoding');

    const unsupportedAudio = await page.evaluate(async () => {
      const response = await fetch('game-assets/selftest.mp2');
      const bytes = new Uint8Array(await response.arrayBuffer());
      Module.FS_createDataFile('/game-assets', 'selftest.mp2',
        bytes, true, true, true);
      const opened = Module.ccall('KotonohaWebSelfTestOpenAudioId',
        'number', ['string'], ['selftest.mp2']);
      const pumped = Module.ccall('KotonohaWebSelfTestPumpVideo',
        'number', [], []);
      const duration = Module.ccall('KotonohaWebSelfTestVideoDuration',
        'number', [], []);
      Module.ccall('KotonohaWebSelfTestCloseVideo', null, [], []);
      return { opened, pumped, duration };
    });
    console.log('UNSUPPORTED_AUDIO', JSON.stringify(unsupportedAudio));
    assert(unsupportedAudio.opened === 1 &&
      unsupportedAudio.pumped === 1 && unsupportedAudio.duration > 1.8,
      'unsupported browser audio MIME opens through local FFmpeg');

    if (testLegacySidecar) {
      const direct = await page.evaluate(async () => {
        const response = await fetch('game-assets/legacy-direct.mpg');
        if (!response.ok) throw new Error('Cannot fetch MPEG fixture');
        const bytes = new Uint8Array(await response.arrayBuffer());
        try { Module.FS_createPath('/', 'game-assets', true, true); }
        catch (error) { /* already present */ }
        Module.FS_createDataFile('/game-assets', 'legacy-direct.mpg',
          bytes, true, true, true);
        const opened = Module.ccall('KotonohaWebSelfTestOpenVideoPath',
          'number', ['string'], ['/game-assets/legacy-direct.mpg']);
        let pumped = 0, hasFrame = 0;
        for (let i = 0; i < 100 && opened; ++i) {
          pumped = Module.ccall('KotonohaWebSelfTestPumpVideo',
            'number', [], []);
          hasFrame = Module.ccall('KotonohaWebSelfTestVideoHasFrame',
            'number', [], []);
          if (hasFrame || !pumped) break;
          await new Promise(resolve => setTimeout(resolve, 50));
        }
        Module.ccall('KotonohaWebSelfTestCloseVideo', null, [], []);
        return { opened, pumped, hasFrame };
      });
      console.log('DIRECT_MPEG', JSON.stringify(direct));
      assert(direct.opened === 1 && direct.pumped === 1 && direct.hasFrame === 1,
        'MPEG with audio decodes on WebAssembly without creating a thread');
      assert(logs.some(line => line.includes('Direct YUV420P video texture')),
        'FFmpeg YUV420P frame uploads directly to an SDL YUV texture');

      const legacy = await page.evaluate(async () => {
        try { Module.FS_createPath('/', 'game-assets', true, true); }
        catch (error) { /* already present */ }
        for (const name of ['legacy.mpg', 'legacy.mpg.webm']) {
          try {
            Module.FS_createDataFile('/game-assets', name,
              new Uint8Array(name === 'legacy.mpg' ? [0] : []),
              true, true, true);
          } catch (error) {
            if (!Module.FS_readFile('/game-assets/' + name)) throw error;
          }
        }
        const opened = Module.ccall('KotonohaWebSelfTestOpenVideoId',
          'number', ['string'], ['legacy.mpg']);
        let hasFrame = 0;
        for (let i = 0; i < 100; ++i) {
          Module.ccall('KotonohaWebSelfTestPumpVideo', 'number', [], []);
          hasFrame = Module.ccall('KotonohaWebSelfTestVideoHasFrame',
            'number', [], []);
          if (hasFrame) break;
          await new Promise(resolve => setTimeout(resolve, 100));
        }
        const element = document.getElementById('kotonoha-video-player');
        return { opened, hasFrame, src: element.currentSrc,
          duration: Module.ccall('KotonohaWebSelfTestVideoDuration',
            'number', [], []) };
      });
      console.log('LEGACY_VIDEO', JSON.stringify(legacy));
      assert(legacy.opened === 1 && legacy.hasFrame === 1,
        'MPEG asset opens and produces a WebM frame');
      assert(legacy.src.startsWith('blob:'),
        'MPEG sidecar is played from virtual bytes via Blob');
      assert(legacy.duration > 1.5,
        'sidecar duration is reported by MediaPlayer');
      await page.evaluate(() => Module.ccall('KotonohaWebSelfTestCloseVideo',
        null, [], []));
    }

    // Real browser touch events, not an artificial SDL_PushEvent. A fresh
    // touch-enabled page reaches the SDL3 callback through the canvas.
    const touchContext = await browser.newContext({
      hasTouch: true, isMobile: true, viewport: { width: 800, height: 600 }
    });
    try {
      const touchPage = await touchContext.newPage();
      touchPage.on('pageerror', error =>
        logs.push('touch-pageerror: ' + error.message));
      await touchPage.goto(baseUrl + '/index.html',
        { waitUntil: 'domcontentloaded' });
      await touchPage.locator('#use-server-assets').click();
      await touchPage.waitForFunction(() =>
        typeof Module === 'object' && Module.KotonohaSaveReady === true &&
        document.getElementById('canvas').width > 320, null,
        { timeout: 60000 });
      await touchPage.locator('#loading-overlay')
        .waitFor({ state: 'detached', timeout: 60000 });
      await touchPage.touchscreen.tap(400, 300);
      await touchPage.waitForFunction(() =>
        Module.ccall('KotonohaWebSelfTestTouchDownCount',
          'number', [], []) > 0 &&
        Module.ccall('KotonohaWebSelfTestTouchUpCount',
          'number', [], []) > 0, null, { timeout: 5000 });
      console.log('TOUCH SDL events delivered');
    } finally {
      await touchContext.close();
    }

    console.log('RESULT PASS');
    const knownTeardownError = logs.some(line =>
      line.startsWith('pageerror: null function'));
    if (knownTeardownError)
      console.log('NOTE: ignored a pre-existing SDL teardown error on reload');
  } catch (error) {
    console.error('BROWSER_LOGS', logs.join('\n'));
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
