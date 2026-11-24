// Copyright 2026 Kotonoha contributors. SPDX-License-Identifier: MIT
// Emscripten bridge for KotonohaLauncherCore. The thin EMSCRIPTEN_KEEPALIVE
// wrappers keep the C ABI exported in the final WASM binary so bridge.js can
// call them with Module.ccall(). No engine/SDL types cross this boundary:
// every result is a newly allocated UTF-8 string released with freeString().
#include <KotonohaLauncher/LauncherBridge.h>

#include <emscripten/emscripten.h>

extern "C" {

EMSCRIPTEN_KEEPALIVE char *kotonohaLauncherJsScanRoots(
    const char *const *roots, int rootCount, int splitSavesByGame,
    const char *customSaveRoot) {
  return KotonohaLauncherScanRootsJson(roots, rootCount, splitSavesByGame,
                                       customSaveRoot);
}

EMSCRIPTEN_KEEPALIVE char *kotonohaLauncherJsDetectPath(
    const char *path, int splitSavesByGame, const char *customSaveRoot) {
  return KotonohaLauncherDetectPathJson(path, splitSavesByGame, customSaveRoot);
}

EMSCRIPTEN_KEEPALIVE char *kotonohaLauncherJsLoadParams(
    const char *saveProfileId) {
  return KotonohaLauncherLoadGameParamsJson(saveProfileId);
}

EMSCRIPTEN_KEEPALIVE int kotonohaLauncherJsSaveParams(
    const char *saveProfileId, const char *assetOverlayPath,
    const char *translationPath, const char *subtitleStylePath,
    const char *customDocumentsDir, const char *ktrfPath,
    const char *scriptsDir, const char *startScenario,
    const char *rendererBackend, const char *upscalerBackend,
    const char *upscalerPreset, const char *upscalerFactor) {
  return KotonohaLauncherSaveGameParams(
      saveProfileId, assetOverlayPath, translationPath, subtitleStylePath,
      customDocumentsDir, ktrfPath, scriptsDir, startScenario, rendererBackend,
      upscalerBackend, upscalerPreset, upscalerFactor);
}

EMSCRIPTEN_KEEPALIVE char *kotonohaLauncherJsUpscalePresets(void) {
  return KotonohaLauncherUpscalePresetsJson();
}

EMSCRIPTEN_KEEPALIVE char *kotonohaLauncherJsResolveSaveDirectory(
    const char *gamePath, const char *engineSlug, const char *saveProfileId,
    int splitSavesByGame, const char *customSaveRoot) {
  return KotonohaLauncherResolveSaveDirectory(
      gamePath, engineSlug, saveProfileId, splitSavesByGame, customSaveRoot);
}

EMSCRIPTEN_KEEPALIVE void kotonohaLauncherJsFreeString(char *ptr) {
  KotonohaLauncherFreeString(ptr);
}

} // extern "C"
