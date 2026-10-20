#pragma once
#include <SDL3/SDL.h>

#ifdef __cplusplus
extern "C" {
#endif

// One SDL log category per engine module. Every message is prefixed with the
// module tag so logs read the same everywhere:  "[Video] Failed to ...".
enum {
  KOTONOHA_LOG_CORE = SDL_LOG_CATEGORY_CUSTOM,
  KOTONOHA_LOG_CONFIG,
  KOTONOHA_LOG_PARSER,
  KOTONOHA_LOG_SCRIPT,
  KOTONOHA_LOG_DEMUXER,
  KOTONOHA_LOG_FFMPEG,
  KOTONOHA_LOG_VIDEO,
  KOTONOHA_LOG_AUDIO,
  KOTONOHA_LOG_IMAGE,
  KOTONOHA_LOG_TEXT,
  KOTONOHA_LOG_RENDER,
  KOTONOHA_LOG_UPSCALER,
  KOTONOHA_LOG_UI
};

#define KOTONOHA_LOGTAG_CORE "[Core] "
#define KOTONOHA_LOGTAG_CONFIG "[Config] "
#define KOTONOHA_LOGTAG_PARSER "[Parser] "
#define KOTONOHA_LOGTAG_SCRIPT "[Script] "
#define KOTONOHA_LOGTAG_DEMUXER "[Demuxer] "
#define KOTONOHA_LOGTAG_FFMPEG "[FFmpeg] "
#define KOTONOHA_LOGTAG_VIDEO "[Video] "
#define KOTONOHA_LOGTAG_AUDIO "[Audio] "
#define KOTONOHA_LOGTAG_IMAGE "[Image] "
#define KOTONOHA_LOGTAG_TEXT "[Text] "
#define KOTONOHA_LOGTAG_RENDER "[Render] "
#define KOTONOHA_LOGTAG_UPSCALER "[Upscaler] "
#define KOTONOHA_LOGTAG_UI "[UI] "

// Usage: KLOG_ERROR(VIDEO, "Failed to open '%s': %s", path, SDL_GetError());
// The format string must be a literal and must not end with a newline.
#define KLOG_ERROR(mod, ...) KOTONOHA_LOG_IMPL(Error, mod, __VA_ARGS__)
#define KLOG_WARN(mod, ...) KOTONOHA_LOG_IMPL(Warn, mod, __VA_ARGS__)
#define KLOG_INFO(mod, ...) KOTONOHA_LOG_IMPL(Info, mod, __VA_ARGS__)
#define KLOG_DEBUG(mod, ...) KOTONOHA_LOG_IMPL(Debug, mod, __VA_ARGS__)

#define KOTONOHA_LOG_IMPL(level, mod, ...)                                     \
  KOTONOHA_LOG_CALL_##level(KOTONOHA_LOG_##mod, KOTONOHA_LOGTAG_##mod,         \
                            __VA_ARGS__)
#define KOTONOHA_LOG_CALL_Error(cat, tag, fmt, ...)                            \
  SDL_LogError(cat, tag fmt, ##__VA_ARGS__)
#define KOTONOHA_LOG_CALL_Warn(cat, tag, fmt, ...)                             \
  SDL_LogWarn(cat, tag fmt, ##__VA_ARGS__)
#define KOTONOHA_LOG_CALL_Info(cat, tag, fmt, ...)                             \
  SDL_LogInfo(cat, tag fmt, ##__VA_ARGS__)
#define KOTONOHA_LOG_CALL_Debug(cat, tag, fmt, ...)                            \
  SDL_LogDebug(cat, tag fmt, ##__VA_ARGS__)

// Sets the default priority of every category (INFO, DEBUG in debug builds)
// unless the user already chose one through the SDL_LOGGING hint.
void KotonohaLogInit(void);

#ifdef __cplusplus
}
#endif
