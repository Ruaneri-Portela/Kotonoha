#pragma once

#include <SDL3/SDL.h>
#include <string>
#include <vector>

namespace Kotonoha {

enum LogCategory {
  LogCore = SDL_LOG_CATEGORY_CUSTOM,
  LogConfig,
  LogMedia,
  LogUpscaler,
  LogUI
};

struct LogEntry {
  Uint64 milliseconds;
  int category;
  SDL_LogPriority priority;
  std::string message;
};

// Captures SDL logs while preserving its previous output (stderr for MCP).
class Log {
public:
  explicit Log(const std::string &file = {});
  ~Log();
  Log(const Log &) = delete;
  Log &operator=(const Log &) = delete;
  static std::vector<LogEntry> entries();
  static void clear();
};

} // namespace Kotonoha
