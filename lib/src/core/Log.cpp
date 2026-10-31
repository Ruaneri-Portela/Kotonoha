#include <Kotonoha/core/Log.hpp>

#include <deque>
#include <fstream>
#include <mutex>
#include <stdexcept>

namespace Kotonoha {
namespace {
std::mutex mutex;
std::deque<LogEntry> history;
std::ofstream output;
SDL_LogOutputFunction previous = nullptr;
void *previousData = nullptr;
bool active = false;

void SDLCALL capture(void *, int category, SDL_LogPriority priority,
                     const char *message) {
  {
    std::lock_guard<std::mutex> lock(mutex);
    if (history.size() == 1000)
      history.pop_front();
    history.push_back({SDL_GetTicks(), category, priority, message});
    if (output.is_open()) {
      output << history.back().milliseconds << " [" << category << ":"
             << static_cast<int>(priority) << "] " << message << '\n';
      output.flush();
      if (!output) {
        output.close();
        if (previous)
          previous(previousData, LogCore, SDL_LOG_PRIORITY_ERROR,
                   "[Log] Cannot write log file; file logging stopped");
      }
    }
  }
  if (previous)
    previous(previousData, category, priority, message);
}
} // namespace

Log::Log(const std::string &file) {
  std::lock_guard<std::mutex> lock(mutex);
  if (active)
    throw std::logic_error("Only one engine log collector may be active");
  if (!file.empty()) {
    output.clear();
    output.open(file, std::ios::app);
    if (!output)
      throw std::runtime_error("Cannot open log file: " + file);
  }
  history.clear();
  SDL_GetLogOutputFunction(&previous, &previousData);
  if (!SDL_GetHint(SDL_HINT_LOGGING))
    for (int category : {LogCore, LogConfig, LogMedia, LogUpscaler, LogUI})
      SDL_SetLogPriority(category, SDL_LOG_PRIORITY_INFO);
  active = true;
  SDL_SetLogOutputFunction(capture, nullptr);
}

Log::~Log() {
  SDL_SetLogOutputFunction(previous, previousData);
  std::lock_guard<std::mutex> lock(mutex);
  output.close();
  active = false;
}

std::vector<LogEntry> Log::entries() {
  std::lock_guard<std::mutex> lock(mutex);
  return {history.begin(), history.end()};
}

void Log::clear() {
  std::lock_guard<std::mutex> lock(mutex);
  history.clear();
}
} // namespace Kotonoha
