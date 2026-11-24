// Copyright 2026 Kotonoha contributors. SPDX-License-Identifier: MIT
// Shared C ABI bridge between C++ KotonohaLauncher core (engine detection,
// save-profile isolation, per-game parameter persistence, route validation)
// and native mobile UI frontends (iOS SwiftUI Liquid Glass & Android Material
// 3).
#pragma once

// Export the C ABI from the shared KotonohaLauncherCore library on every
// platform (dylib / .so / .dll). Consumers only need the declarations; the
// core build defines KOTONOHA_LAUNCHER_CORE_BUILD=1.
#if defined(KOTONOHA_LAUNCHER_CORE_BUILD) && defined(_WIN32)
#define KOTONOHA_LAUNCHER_EXPORT __declspec(dllexport)
#elif defined(_WIN32)
#define KOTONOHA_LAUNCHER_EXPORT __declspec(dllimport)
#elif defined(__GNUC__) || defined(__clang__)
#define KOTONOHA_LAUNCHER_EXPORT __attribute__((visibility("default")))
#else
#define KOTONOHA_LAUNCHER_EXPORT
#endif

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
  // When non-zero, validate detection/params/registry and return a JSON result
  // without starting SDL or the game runtime ("Test launcher only" toggle).
  // The KOTONOHA_LAUNCHER_LAUNCH_DRY_RUN environment flag is also honored.
  // Appended last so existing callers keep their field offsets.
  int dryRun;
} KotonohaGameLaunchRequest;

// Scans an array of library roots / single games / disc images / SAF URIs and
// returns a newly-allocated UTF-8 JSON array of discovered games.
// Caller must release the returned string with KotonohaLauncherFreeString().
KOTONOHA_LAUNCHER_EXPORT char *KotonohaLauncherScanRootsJson(const char *const *roots, int rootCount,
                                    int splitSavesByGame,
                                    const char *customSaveRoot);

// Detects a single game folder or disc image and returns a newly-allocated
// UTF-8 JSON object (or "{}" if unrecognized).
KOTONOHA_LAUNCHER_EXPORT char *KotonohaLauncherDetectPathJson(const char *path, int splitSavesByGame,
                                     const char *customSaveRoot);

// Loads persisted per-game launch parameters for `saveProfileId` as JSON.
KOTONOHA_LAUNCHER_EXPORT char *KotonohaLauncherLoadGameParamsJson(const char *saveProfileId);

// Saves per-game launch parameters for `saveProfileId`. Returns 1 on success.
KOTONOHA_LAUNCHER_EXPORT int KotonohaLauncherSaveGameParams(
    const char *saveProfileId, const char *assetOverlayPath,
    const char *translationPath, const char *subtitleStylePath,
    const char *customDocumentsDir, const char *ktrfPath,
    const char *scriptsDir, const char *startScenario,
    const char *rendererBackend, const char *upscalerBackend,
    const char *upscalerPreset, const char *upscalerFactor);

// Launches one game in-process (the game runtime factories are linked into the
// core). With SDL dummy drivers this also works headlessly for integration
// harnesses. By default the launch uses the real video/audio drivers and the
// game window stays open until the user quits. Set request->dryRun=1 ("Test
// launcher only") or KOTONOHA_LAUNCHER_LAUNCH_DRY_RUN=1 to only validate the
// request and the resolved parameters, without starting SDL/gameplay. CI
// harnesses that need an offscreen run must opt in explicitly with
// KOTONOHA_LAUNCHER_LAUNCH_HEADLESS=1 (installs SDL dummy drivers and caps the
// run at KOTONOHA_LAUNCHER_LAUNCH_FRAMES frames, default 2). Returns a
// newly-allocated UTF-8 JSON object:
// {"ok":bool,"playable":bool,"phase":string,"message":string,"frames":int,
// "dryRun":bool,"path":string,"engineSlug":string}.
// `playable` is true when the validated request reached the iterate phase and
// no fatal SDL error occurred; in a dry-run it is the prediction for the
// validated request (phase == "validated", dryRun == true). Caller must release
// with KotonohaLauncherFreeString().
KOTONOHA_LAUNCHER_EXPORT char *KotonohaLauncherLaunchGame(
    const KotonohaGameLaunchRequest *request);

// Returns a newly-allocated UTF-8 JSON array of upscaler shader presets
// ([{"index":0,"name":"Anime4K: Mode A (HQ)"}, ...]).
// Caller must release with KotonohaLauncherFreeString().
KOTONOHA_LAUNCHER_EXPORT char *KotonohaLauncherUpscalePresetsJson(void);

// Validates a School Days routing graph (.ktnroute / .ktrf).
// Returns 1 if valid, 0 otherwise; writes diagnostic info into `infoBuf`.
KOTONOHA_LAUNCHER_EXPORT int KotonohaLauncherValidateRoute(const char *ktrfPath, char *infoBuf,
                                  int infoBufSize);

// Returns the bundled default School Days route path (or empty string).
// Caller must release with KotonohaLauncherFreeString().
KOTONOHA_LAUNCHER_EXPORT char *KotonohaLauncherDefaultSchoolDaysRoute(void);

// Resolves the isolated save directory for a game profile & engine slug.
// Caller must release with KotonohaLauncherFreeString().
KOTONOHA_LAUNCHER_EXPORT char *KotonohaLauncherResolveSaveDirectory(const char *gamePath,
                                           const char *engineSlug,
                                           const char *saveProfileId,
                                           int splitSavesByGame,
                                           const char *customSaveRoot);

// Frees any heap string returned by KotonohaLauncher* functions.
KOTONOHA_LAUNCHER_EXPORT void KotonohaLauncherFreeString(char *ptr);

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
