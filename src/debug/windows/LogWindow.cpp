#include <Kotonoha/debug/LogPanel.hpp>

#include <imgui.h>

#include <deque>
#include <mutex>
#include <string>

namespace Kotonoha::Debug {
namespace {

constexpr size_t kMaxEntries = 2000;

struct LogEntry {
  SDL_LogPriority priority;
  std::string text;
};

std::mutex g_mutex;
std::deque<LogEntry> g_entries;
unsigned long long g_revision = 0;

SDL_LogOutputFunction g_previousOutput = nullptr;
void *g_previousUserData = nullptr;
bool g_installed = false;

void SDLCALL CaptureLog(void *, int category, SDL_LogPriority priority,
                        const char *message) {
  if (g_previousOutput != nullptr) {
    g_previousOutput(g_previousUserData, category, priority, message);
  }

  std::lock_guard<std::mutex> guard(g_mutex);
  if (g_entries.size() >= kMaxEntries) {
    g_entries.pop_front();
  }
  g_entries.push_back({priority, message != nullptr ? message : ""});
  g_revision++;
}

const char *PriorityLabel(SDL_LogPriority priority) {
  switch (priority) {
  case SDL_LOG_PRIORITY_TRACE:
    return "TRACE";
  case SDL_LOG_PRIORITY_VERBOSE:
    return "VERBOSE";
  case SDL_LOG_PRIORITY_DEBUG:
    return "DEBUG";
  case SDL_LOG_PRIORITY_INFO:
    return "INFO";
  case SDL_LOG_PRIORITY_WARN:
    return "WARN";
  case SDL_LOG_PRIORITY_ERROR:
    return "ERROR";
  case SDL_LOG_PRIORITY_CRITICAL:
    return "CRITICAL";
  default:
    return "LOG";
  }
}

ImVec4 PriorityColor(SDL_LogPriority priority) {
  switch (priority) {
  case SDL_LOG_PRIORITY_WARN:
    return ImVec4(1.00f, 0.80f, 0.35f, 1.0f);
  case SDL_LOG_PRIORITY_ERROR:
  case SDL_LOG_PRIORITY_CRITICAL:
    return ImVec4(1.00f, 0.42f, 0.42f, 1.0f);
  case SDL_LOG_PRIORITY_DEBUG:
  case SDL_LOG_PRIORITY_VERBOSE:
  case SDL_LOG_PRIORITY_TRACE:
    return ImVec4(0.60f, 0.60f, 0.68f, 1.0f);
  default:
    return ImVec4(0.90f, 0.90f, 0.95f, 1.0f);
  }
}

bool MatchesFilter(const std::string &text, const char *filter) {
  return filter[0] == '\0' || SDL_strcasestr(text.c_str(), filter) != nullptr;
}

} // namespace

void InstallLogCapture() {
  if (g_installed) {
    return;
  }
  SDL_GetLogOutputFunction(&g_previousOutput, &g_previousUserData);
  SDL_SetLogOutputFunction(CaptureLog, nullptr);
  g_installed = true;
}

void UninstallLogCapture() {
  if (!g_installed) {
    return;
  }
  SDL_SetLogOutputFunction(g_previousOutput, g_previousUserData);
  g_previousOutput = nullptr;
  g_previousUserData = nullptr;
  g_installed = false;
}

void DrawLogPanel(bool *open) {
  if (open == nullptr || !*open) {
    return;
  }

  static const char *levelNames[] = {"Verbose", "Debug", "Info", "Warn",
                                     "Error"};
  static const SDL_LogPriority levelValues[] = {
      SDL_LOG_PRIORITY_VERBOSE, SDL_LOG_PRIORITY_DEBUG, SDL_LOG_PRIORITY_INFO,
      SDL_LOG_PRIORITY_WARN, SDL_LOG_PRIORITY_ERROR};

  static int levelIndex = 2;
  static bool autoScroll = true;
  static char filter[128] = {0};
  static unsigned long long lastRevision = 0;

  const ImGuiViewport *viewport = ImGui::GetMainViewport();
  ImGui::SetNextWindowPos(
      ImVec2(viewport->WorkPos.x + 40,
             viewport->WorkPos.y + viewport->WorkSize.y * 0.55f),
      ImGuiCond_FirstUseEver);
  ImGui::SetNextWindowSize(ImVec2(720, 260), ImGuiCond_FirstUseEver);

  if (!ImGui::Begin("Log", open)) {
    ImGui::End();
    return;
  }

  const float scale = ImGui::GetStyle().FontScaleMain;
  ImGui::SetNextItemWidth(110.0f * scale);
  ImGui::Combo("Level", &levelIndex, levelNames, 5);
  ImGui::SameLine();
  ImGui::SetNextItemWidth(180.0f * scale);
  ImGui::InputTextWithHint("##logFilter", "Filter...", filter, sizeof(filter));
  ImGui::SameLine();
  ImGui::Checkbox("Auto-scroll", &autoScroll);
  ImGui::SameLine();
  const bool clear = ImGui::Button("Clear");
  ImGui::SameLine();
  const bool copy = ImGui::Button("Copy");
  ImGui::Separator();

  const SDL_LogPriority minPriority = levelValues[levelIndex];
  std::string copyBuffer;

  {
    std::lock_guard<std::mutex> guard(g_mutex);
    if (clear) {
      g_entries.clear();
      g_revision++;
    }

    ImGui::BeginChild("##logScroll", ImVec2(0, 0), ImGuiChildFlags_None,
                      ImGuiWindowFlags_HorizontalScrollbar);
    for (const LogEntry &entry : g_entries) {
      if (entry.priority < minPriority || !MatchesFilter(entry.text, filter)) {
        continue;
      }
      const std::string line =
          std::string("[") + PriorityLabel(entry.priority) + "] " + entry.text;
      if (copy) {
        copyBuffer += line;
        copyBuffer += '\n';
      }
      ImGui::PushStyleColor(ImGuiCol_Text, PriorityColor(entry.priority));
      ImGui::TextUnformatted(line.c_str());
      ImGui::PopStyleColor();
    }

    if (autoScroll && lastRevision != g_revision) {
      ImGui::SetScrollHereY(1.0f);
    }
    lastRevision = g_revision;
    ImGui::EndChild();
  }

  if (copy) {
    ImGui::SetClipboardText(copyBuffer.c_str());
  }

  ImGui::End();
}

} // namespace Kotonoha::Debug
