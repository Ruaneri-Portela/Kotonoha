#pragma once

#include <stdbool.h>
#include <stddef.h>

// Scalar settings are atomic because the debug UI, the main loop and the
// worker threads (demuxer, event manager) read and write them concurrently.
#ifdef __cplusplus
extern "C++" {
#include <atomic>
}
#define KOTONOHA_ATOMIC(T) std::atomic<T>
#else
#include <stdatomic.h>
#define KOTONOHA_ATOMIC(T) _Atomic T
#endif

#if defined(__ANDROID__)
#define KOTONOHA_MOBILE
#elif defined(__APPLE__)
#include <TargetConditionals.h>
#define KOTONOHA_MOBILE
#endif

#ifdef __cplusplus
extern "C" {
#endif

#if defined(KOTONOHA_MOBILE)
void KotonohaMobileSetup(void);
// Directory holding the user-provided game files (.gpk): the app Documents
// folder on iOS. NULL when the platform has no such convention.
const char *KotonohaMobileGameDataDir(void);
const char *KotonohaMobilePreferredAssetDir(const char *mode);
#endif

typedef struct Kotonoha_Config {
  char *assetsPath;
  char *assStyles;
  char *assStylesData;
  size_t assStylesSize;

  char *preferredRenderer;
  char *mpvUpscalerFiles;
  char *splashFiles;
  char *promptSelectSound;
  char *promptViewSound;
  char *titleMusic;
  char *iconPath;
  char *cursorPath;
  KOTONOHA_ATOMIC(int) cursorSize;
  char *fontsPath;
  char *configFilePath;

  char **scriptsForced;
  KOTONOHA_ATOMIC(int) scriptCount;
  KOTONOHA_ATOMIC(int) scriptCapacity;

  KOTONOHA_ATOMIC(int) enableVsync;
  KOTONOHA_ATOMIC(int) windowSize[2];
  KOTONOHA_ATOMIC(float) scalerFactor;
  // Maximum buffered seconds per audio/video stream. The demuxer reads this
  // value on every use, so changes apply immediately.
  KOTONOHA_ATOMIC(float) mediaCacheSeconds;
  KOTONOHA_ATOMIC(float) debugUiScale;
  KOTONOHA_ATOMIC(float) splashSeconds;
  KOTONOHA_ATOMIC(float) splashFadeSeconds;
  // How long before an event starts its media is opened (script timeline).
  KOTONOHA_ATOMIC(int) prefetchHorizonMs;
  KOTONOHA_ATOMIC(int) seekBigMs;
  KOTONOHA_ATOMIC(int) seekSmallMs;
  KOTONOHA_ATOMIC(int) loadingDebounceMs;
  KOTONOHA_ATOMIC(int) minLoadingPauseMs;

  KOTONOHA_ATOMIC(bool) fullScreen;
  KOTONOHA_ATOMIC(bool) showFps;
  KOTONOHA_ATOMIC(bool) showTimestamp;
  KOTONOHA_ATOMIC(bool) enableDebugUi;
  KOTONOHA_ATOMIC(bool) skipFiles;
  KOTONOHA_ATOMIC(bool) upscalingEnabled;

  KOTONOHA_ATOMIC(bool) dirtyConfig;
  KOTONOHA_ATOMIC(bool) paused;
  KOTONOHA_ATOMIC(bool) back;
  KOTONOHA_ATOMIC(bool) next;
  KOTONOHA_ATOMIC(bool) cancelOpen;

  KOTONOHA_ATOMIC(int) scene;
} Kotonoha_Config;

void KotonohaConfigFree(Kotonoha_Config *config);
void KotonohaConfigSave(Kotonoha_Config *config);

bool KotonohaConfigRegisterImGuiHandler(Kotonoha_Config *config);
void KotonohaConfigSetIniFilename(const char *path);

// Texto de "About" compartilhado entre a linha de comando e a Debug UI
const char *KotonohaAboutText(void);

void KotonohaConfigApplyAndSave(Kotonoha_Config *config);

void KotonohaConfigLoadFromDisk(const char *path);

bool KotonohaConfigLoad(Kotonoha_Config *config, const char *path, int argc,
                        char *argv[]);
#ifdef __cplusplus
}
#endif
