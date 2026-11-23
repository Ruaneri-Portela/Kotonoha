// Copyright 2026 Kotonoha contributors. SPDX-License-Identifier: MIT
// Shared C ABI bridge between C++ KotonohaLauncher core (engine detection,
// save-profile isolation, per-game parameter persistence, route validation)
// and native mobile UI frontends (iOS SwiftUI Liquid Glass & Android Material
// 3).
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

typedef struct KotonohaGameLaunchRequest {
  const char *gamePath;
  const char *engineSlug;
  const char *saveProfileId;
  int splitSavesByGame;
  const char *customSaveRoot;
  // Common optional parameters
  const char *assetOverlayPath;
  const char *translationPath;
  const char *subtitleStylePath;
  const char *customDocumentsDir;
  // Engine-specific optional parameters
  const char *ktrfPath;
  const char *scriptsDir;
  const char *startScenario;
  // Graphics & Upscaling overrides (empty = Auto / game default)
  const char *rendererBackend;
  const char *upscalerBackend;
  const char *upscalerPreset;
  const char *upscalerFactor;
} KotonohaGameLaunchRequest;

// Scans an array of library roots / single games / disc images / SAF URIs and
// returns a newly-allocated UTF-8 JSON array of discovered games.
// Caller must release the returned string with KotonohaLauncherFreeString().
char *KotonohaLauncherScanRootsJson(const char *const *roots, int rootCount,
                                    int splitSavesByGame,
                                    const char *customSaveRoot);

// Detects a single game folder or disc image and returns a newly-allocated
// UTF-8 JSON object (or "{}" if unrecognized).
char *KotonohaLauncherDetectPathJson(const char *path, int splitSavesByGame,
                                     const char *customSaveRoot);

// Loads persisted per-game launch parameters for `saveProfileId` as JSON.
char *KotonohaLauncherLoadGameParamsJson(const char *saveProfileId);

// Saves per-game launch parameters for `saveProfileId`. Returns 1 on success.
int KotonohaLauncherSaveGameParams(
    const char *saveProfileId, const char *assetOverlayPath,
    const char *translationPath, const char *subtitleStylePath,
    const char *customDocumentsDir, const char *ktrfPath,
    const char *scriptsDir, const char *startScenario,
    const char *rendererBackend, const char *upscalerBackend,
    const char *upscalerPreset, const char *upscalerFactor);

// Returns a newly-allocated UTF-8 JSON array of upscaler shader presets
// ([{"index":0,"name":"Anime4K: Mode A (HQ)"}, ...]).
// Caller must release with KotonohaLauncherFreeString().
char *KotonohaLauncherUpscalePresetsJson(void);

// Validates a School Days routing graph (.ktnroute / .ktrf).
// Returns 1 if valid, 0 otherwise; writes diagnostic info into `infoBuf`.
int KotonohaLauncherValidateRoute(const char *ktrfPath, char *infoBuf,
                                  int infoBufSize);

// Returns the bundled default School Days route path (or empty string).
// Caller must release with KotonohaLauncherFreeString().
char *KotonohaLauncherDefaultSchoolDaysRoute(void);

// Resolves the isolated save directory for a game profile & engine slug.
// Caller must release with KotonohaLauncherFreeString().
char *KotonohaLauncherResolveSaveDirectory(const char *gamePath,
                                           const char *engineSlug,
                                           const char *saveProfileId,
                                           int splitSavesByGame,
                                           const char *customSaveRoot);

// Frees any heap string returned by KotonohaLauncher* functions.
void KotonohaLauncherFreeString(char *ptr);

#ifdef __APPLE__
#include <TargetConditionals.h>
#if TARGET_OS_IPHONE
typedef void (*KotonohaIOSLaunchCallback)(
    const KotonohaGameLaunchRequest *request, void *userData);
typedef void (*KotonohaIOSQuitCallback)(void *userData);

// Presents the native SwiftUI Liquid Glass launcher over SDL's window.
void KotonohaIOSPresentNativeLauncher(KotonohaIOSLaunchCallback onLaunch,
                                      KotonohaIOSQuitCallback onQuit,
                                      void *userData);

// Dismisses the native SwiftUI launcher cleanly before starting a game.
void KotonohaIOSDismissNativeLauncher(void);

// Notifies the iOS bookmark store of an incoming file/folder URL (e.g.
// SDL_EVENT_DROP_FILE / Files.app "Open In").
void KotonohaIOSNotifyOpenedURL(const char *pathOrUrl);

// Presents a native UIAlertController over the active iOS launcher/window so
// startup and runtime errors are surfaced clearly instead of silently closing.
void KotonohaIOSPresentError(const char *title, const char *message);
#endif
#endif

#ifdef __cplusplus
} // extern "C"
#endif
