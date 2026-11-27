#pragma once
#include <Kotonoha/core/Log.hpp>
#include <SDL3/SDL.h>
extern "C" {
#include <libavutil/log.h>
}
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <mutex>
namespace Kotonoha::detail {
inline void mediaLog(void *context, int level, const char *format,
                     va_list args) {
  if (level == AV_LOG_INFO) {
    char line[128];
    va_list copy;
    va_copy(copy, args);
    std::vsnprintf(line, sizeof(line), format, copy);
    va_end(copy);
    // WMV3 accepts this zero padding. Keep it available in debug logs;
    // warnings, errors and nonzero trailing data retain FFmpeg's reporting.
    if (std::strcmp(line, "Extra data: 8 bits left, value: 0\n") == 0) {
      SDL_LogDebug(LogMedia, "FFmpeg: WMV3 zero header padding (8 bits)");
      return;
    }
  }
  av_log_default_callback(context, level, format, args);
}
inline void initializeMediaLog() {
  static std::once_flag once;
  std::call_once(once, [] { av_log_set_callback(mediaLog); });
}
} // namespace Kotonoha::detail
