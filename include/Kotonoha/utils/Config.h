#pragma once

#include <stdbool.h>
#include <stddef.h>

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
#endif

typedef struct KotonohaConfig {
  char *assetsPath;
  char *assStyles;
  char *assStylesData;
  size_t assStylesSize;

  char *preferredRenderer;
  char *configFilePath;

  char **scriptsForced;
  int scriptCount;
  int scriptCapacity;

  int enableVsync;
  int windowSize[2];
  float scalerFactor;
  float videoCacheSeconds;

  bool fullScreen;
  bool showFps;
  bool showTimestamp;
  bool enableDebugUi;
  bool skipFiles;

  bool dirtyConfig;
  bool paused;
  bool back;
  bool next;
  bool cancelOpen;

  int scene;
} KotonohaConfig;

void KotonohaConfigFree(KotonohaConfig *config);
void KotonohaConfigSave(KotonohaConfig *config);

KotonohaConfig KotonohaConfigLoad(const char *path, int argc, char *argv[]);
#ifdef __cplusplus
}
#endif
