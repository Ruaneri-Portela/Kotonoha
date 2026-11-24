// Copyright 2026 Kotonoha contributors. SPDX-License-Identifier: MIT
//
// ES module facade over the Emscripten exports in JsBridge.cpp. The WASM game
// shell must be built with -sEXPORTED_RUNTIME_METHODS=ccall,cstringToUTF8,
// UTF8ToString,stringToUTF8,lengthBytesUTF8,setValue,_malloc,_free (or export
// the full runtime). `getModule` is an async factory returning the resolved
// Emscripten Module (e.g. `() => createKotonohaModule()`).
//
// Every C ABI string result is allocated on the WASM heap. This facade always
// converts it with UTF8ToString and releases it with the exported freeString,
// so repeated calls do not leak WASM memory.

/** @param {() => Promise<any>} getModule */
export function createLauncherBridge(getModule) {
  let modulePromise = null;
  const module = async () => {
    if (!modulePromise) modulePromise = Promise.resolve(getModule());
    const m = await modulePromise;
    if (!m || typeof m.ccall !== 'function') {
      throw new Error('Emscripten Module.ccall is unavailable; enable EXPORTED_RUNTIME_METHODS');
    }
    return m;
  };

  /** Call a C function returning a heap UTF-8 pointer, then free it. */
  const stringResult = async (name, types, args) => {
    const m = await module();
    const ptr = m.ccall(name, 'number', types, args);
    if (!ptr) return '';
    try {
      return m.UTF8ToString(ptr);
    } finally {
      m.ccall('kotonohaLauncherJsFreeString', null, ['number'], [ptr]);
    }
  };

  /** Allocate a char*[] on the WASM heap for KotonohaLauncherScanRootsJson. */
  const allocRoots = (m, roots) => {
    const strings = roots.map((root) => String(root ?? ''));
    const owned = strings.map((text) => {
      const size = m.lengthBytesUTF8(text) + 1;
      const ptr = m._malloc(size);
      m.stringToUTF8(text, ptr, size);
      return ptr;
    });
    const array = m._malloc(Math.max(owned.length, 1) * 4);
    owned.forEach((ptr, index) => m.setValue(array + index * 4, ptr, 'i32'));
    return { array, owned };
  };

  const freeRoots = (m, allocated) => {
    allocated.owned.forEach((ptr) => m._free(ptr));
    m._free(allocated.array);
  };

  return {
    /** Scan library roots; returns the parsed JSON array of discovered games. */
    async scanRoots(roots = [], { splitSavesByGame = false, customSaveRoot = '' } = {}) {
      const m = await module();
      const allocated = allocRoots(m, roots);
      try {
        const json = await stringResult(
          'kotonohaLauncherJsScanRoots',
          ['number', 'number', 'string'],
          [allocated.array, roots.length, splitSavesByGame ? 1 : 0, String(customSaveRoot ?? '')],
        );
        return JSON.parse(json || '[]');
      } finally {
        freeRoots(m, allocated);
      }
    },

    /** Detect one game folder/disc image; returns its JSON entry or {}. */
    async detectPath(path, { splitSavesByGame = false, customSaveRoot = '' } = {}) {
      const json = await stringResult(
        'kotonohaLauncherJsDetectPath',
        ['string', 'number', 'string'],
        [String(path ?? ''), splitSavesByGame ? 1 : 0, String(customSaveRoot ?? '')],
      );
      return JSON.parse(json || '{}');
    },

    /** Load persisted launch parameters as a plain object. */
    async loadParams(saveProfileId) {
      const json = await stringResult(
        'kotonohaLauncherJsLoadParams', ['string'], [String(saveProfileId ?? '')]);
      return JSON.parse(json || '{}');
    },

    /** Persist launch parameters; resolves to true on success. */
    async saveParams(saveProfileId, params = {}) {
      const m = await module();
      const text = (value) => String(value ?? '');
      return m.ccall(
        'kotonohaLauncherJsSaveParams', 'number',
        Array(12).fill('string'),
        [
          text(saveProfileId), text(params.assetOverlayPath),
          text(params.translationPath), text(params.subtitleStylePath),
          text(params.customDocumentsDir), text(params.ktrfPath),
          text(params.scriptsDir), text(params.startScenario),
          text(params.rendererBackend), text(params.upscalerBackend),
          text(params.upscalerPreset), text(params.upscalerFactor),
        ],
      ) !== 0;
    },

    /** List upscaler presets as [{ index, name }, ...]. */
    async upscalePresets() {
      const json = await stringResult('kotonohaLauncherJsUpscalePresets', [], []);
      return JSON.parse(json || '[]');
    },

    /** Resolve the isolated save directory for a game profile. */
    resolveSaveDirectory(gamePath, engineSlug = '', saveProfileId = '', options = {}) {
      return stringResult(
        'kotonohaLauncherJsResolveSaveDirectory',
        ['string', 'string', 'string', 'number', 'string'],
        [
          String(gamePath ?? ''), String(engineSlug ?? ''),
          String(saveProfileId ?? ''), options.splitSavesByGame ? 1 : 0,
          String(options.customSaveRoot ?? ''),
        ],
      );
    },

    /** Release a raw C ABI string pointer (escaped for advanced callers). */
    async freeString(ptr) {
      const m = await module();
      if (ptr) m.ccall('kotonohaLauncherJsFreeString', null, ['number'], [ptr]);
    },
  };
}

export default createLauncherBridge;
