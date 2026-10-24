#include <Kotonoha/Kotonoha.hpp>
#include <Kotonoha/debug/DebugUi.hpp>
#include <Kotonoha/specials/Upscaling.h>
#include <SDL3/SDL.h>
#include <SDL3/SDL_iostream.h>
#include <algorithm>
#include <cstddef>
#include <cstdio>
#include <exception>
#include <limits>
#include <sstream>
#include <string>
#include <vector>

#include <imgui.h>
#include <imgui_impl_sdl3.h>
#include <imgui_impl_sdlrenderer3.h>
#include <imgui_internal.h>
#include <imgui_stdlib.h>

namespace Kotonoha {

void BasicGuiShutdown();

/* ---------------- Estado Global da Interface ---------------- */

bool basicGuiEditorShow = false;

namespace {
bool imguiSdlBackendInitialized = false;
bool imguiRendererBackendInitialized = false;
}

static bool textEditorShow = false;
static bool settingsWindowShow = false;
static bool aboutWindowShow = false;
static bool showError = false;
static bool show_playlist = false;

static char openBuf[4096] = {0};
static char appendBuf[4096] = {0};

static int selectedSceneIndex = -1;
static int editorSceneIndex = -1;
static bool editorDirty = false;

static std::string sceneEditorText;
static char editorStatusBuf[512] = {0};

struct TextEditorEntry {
  bool isSelection = false;
  size_t lineIndex = 0;
  std::string startTime;
  std::string endTime;
  std::string prefix;
  std::string suffix;
  std::string character;
  std::string text;
  std::vector<std::string> options;
};

static std::vector<std::string> textEditorLines;
static std::vector<TextEditorEntry> textEditorEntries;
static std::string textEditorSyncedScript;
static bool textEditorFinalNewline = false;

static std::string originalScriptPathForTemporary;
static std::string temporaryScriptPath;
static int temporaryGameplayIndex = -1;
static bool temporaryGameplayActive = false;

/* ---------------- Funções Utilitárias ---------------- */

static void SetEditorStatus(const char *text) {
  SDL_snprintf(editorStatusBuf, sizeof(editorStatusBuf), "%s",
               text ? text : "");
}

static bool IsValidGameplayIndex(::Kotonoha::Kotonoha *game, int index) {
  return game != nullptr && index >= 0 &&
         index < static_cast<int>(game->gameplays.size());
}

static ::Kotonoha::Gameplay *GetGameplayAt(::Kotonoha::Kotonoha *game,
                                                  int index) {
  if (!IsValidGameplayIndex(game, index)) {
    return nullptr;
  }
  return game->gameplays[index];
}

static std::string JoinPath(const std::string &dir,
                                     const std::string &name) {
  if (dir.empty())
    return name;
  const char last = dir.back();
  if (last == '/' || last == '\\') {
    return dir + name;
  }
  return dir + "/" + name;
}

static std::vector<std::string> SplitTabs(const std::string &text) {
  std::vector<std::string> fields;
  size_t start = 0;

  while (true) {
    const size_t tab = text.find('\t', start);
    if (tab == std::string::npos) {
      fields.push_back(text.substr(start));
      break;
    }
    fields.push_back(text.substr(start, tab - start));
    start = tab + 1;
  }
  return fields;
}

static std::string JoinLines(const std::vector<std::string> &lines,
                                      bool finalNewline) {
  std::string result;
  for (size_t i = 0; i < lines.size(); ++i) {
    if (i != 0) {
      result.push_back('\n');
    }
    result += lines[i];
  }
  if (finalNewline) {
    result.push_back('\n');
  }
  return result;
}

static std::string FormatTime(Uint64 milliseconds) {
  const Uint64 totalCentiseconds = milliseconds / 10;
  const Uint64 minutes = totalCentiseconds / 6000;
  const Uint64 seconds = (totalCentiseconds / 100) % 60;
  const Uint64 centiseconds = totalCentiseconds % 100;

  char formatted[64];
  SDL_snprintf(formatted, sizeof(formatted), "%02llu:%02llu:%02llu",
               static_cast<unsigned long long>(minutes),
               static_cast<unsigned long long>(seconds),
               static_cast<unsigned long long>(centiseconds));

  return formatted;
}

static std::string FormatScriptTime(const std::string &timestamp) {
  Uint64 parts[3] = {0, 0, 0};
  size_t cursor = 0;

  for (size_t part = 0; part < 3; ++part) {
    const size_t start = cursor;

    while (cursor < timestamp.size() && timestamp[cursor] >= '0' &&
           timestamp[cursor] <= '9') {
      const Uint64 digit = static_cast<Uint64>(timestamp[cursor] - '0');

      if (parts[part] > (std::numeric_limits<Uint64>::max() - digit) / 10) {
        return timestamp;
      }
      parts[part] = parts[part] * 10 + digit;
      ++cursor;
    }

    if (cursor == start ||
        (part < 2 &&
         (cursor >= timestamp.size() || timestamp[cursor++] != ':')) ||
        (part == 2 && cursor != timestamp.size())) {
      return timestamp;
    }
  }

  if (parts[0] > std::numeric_limits<Uint64>::max() / 60000) {
    return timestamp;
  }

  Uint64 milliseconds = parts[0] * 60000;

  if (parts[1] > (std::numeric_limits<Uint64>::max() - milliseconds) / 1000) {
    return timestamp;
  }
  milliseconds += parts[1] * 1000;

  if (parts[2] > (std::numeric_limits<Uint64>::max() - milliseconds) / 10) {
    return timestamp;
  }

  return FormatTime(milliseconds + parts[2] * 10);
}

static bool FinishFrameAndReturn(Kotonoha_Game &context) {
  ImGui::Render();
  const ImVec2 fbScale = ImGui::GetIO().DisplayFramebufferScale;
  SDL_SetRenderScale(context.render, fbScale.x, fbScale.y);
  ImGui_ImplSDLRenderer3_RenderDrawData(ImGui::GetDrawData(), context.render);
  SDL_SetRenderScale(context.render, 1.0f, 1.0f);
  return true;
}

/* ---------------- Gerenciador do Editor de Texto e Sincronização
 * ---------------- */

static void LoadTextEditor(const std::string &source) {
  textEditorSyncedScript = source;
  textEditorLines.clear();
  textEditorEntries.clear();
  textEditorFinalNewline = !source.empty() && source.back() == '\n';

  std::stringstream stream(source);
  std::string line;

  while (std::getline(stream, line)) {
    textEditorLines.push_back(line);
  }

  for (size_t i = 0; i < textEditorLines.size(); ++i) {
    const std::string &rawLine = textEditorLines[i];
    const size_t contentEnd = rawLine.find('\r');
    std::string content = rawLine.substr(0, contentEnd);

    const bool hasTerminator = !content.empty() && content.back() == ';';
    if (hasTerminator) {
      content.pop_back();
    } else {
      continue;
    }

    const size_t equals = content.find('=');
    if (equals == std::string::npos) {
      continue;
    }

    const bool isPrintText = content.compare(0, equals, "[PrintText]") == 0;
    const bool isSelection = content.compare(0, equals, "[SetSELECT]") == 0;

    if (!isPrintText && !isSelection) {
      continue;
    }

    const std::string payload = content.substr(equals + 1);
    const std::vector<std::string> fields = SplitTabs(payload);

    if ((isPrintText && fields.size() != 4) ||
        (isSelection && fields.size() < 3)) {
      continue;
    }

    TextEditorEntry entry;
    entry.isSelection = isSelection;
    entry.lineIndex = i;
    entry.startTime = FormatScriptTime(fields.front());
    entry.endTime = FormatScriptTime(fields.back());

    const size_t firstTab = content.find('\t', equals + 1);
    const size_t lastTab = content.rfind('\t');

    if (firstTab == std::string::npos || lastTab == std::string::npos ||
        lastTab <= firstTab) {
      continue;
    }

    entry.prefix = content.substr(0, firstTab);
    entry.suffix = content.substr(lastTab) + ";";

    if (isPrintText) {
      entry.character = fields[1];
      entry.text = fields[2];
    } else {
      for (size_t field = 1; field + 1 < fields.size(); ++field) {
        entry.options.push_back(fields[field]);
      }
    }

    textEditorEntries.push_back(std::move(entry));
  }
}

static void SyncTextEditorToScript() {
  for (const TextEditorEntry &entry : textEditorEntries) {
    std::string replacement = entry.prefix;

    if (entry.isSelection) {
      for (const std::string &option : entry.options) {
        replacement.push_back('\t');
        replacement += option;
      }
    } else {
      replacement.push_back('\t');
      replacement += entry.character;
      replacement.push_back('\t');
      replacement += entry.text;
    }

    replacement += entry.suffix;

    const size_t carriageReturn = textEditorLines[entry.lineIndex].find('\r');
    if (carriageReturn != std::string::npos) {
      replacement += textEditorLines[entry.lineIndex].substr(carriageReturn);
    }

    textEditorLines[entry.lineIndex] = std::move(replacement);
  }

  sceneEditorText = JoinLines(textEditorLines, textEditorFinalNewline);
  textEditorSyncedScript = sceneEditorText;
}

/* ---------------- I/O e Operações de Cena ---------------- */

static std::string GetTemporaryScenePath(const std::string &path) {
  if (path.empty())
    return "";

  char *prefPath = SDL_GetPrefPath("Kotonoha", "Engine");
  if (prefPath == nullptr) {
    SetEditorStatus(SDL_GetError());
    return "";
  }

  std::string baseDir(prefPath);
  SDL_free(prefPath);

  std::string fileName = path;
  const size_t slashPos = fileName.find_last_of("/\\");
  if (slashPos != std::string::npos) {
    fileName = fileName.substr(slashPos + 1);
  }

  const std::string keepFileName = fileName + ".tmp.ks";

  struct TempCleanupData {
    std::string keepFileName;
  } cleanupData{keepFileName};

  SDL_EnumerateDirectory(
      baseDir.c_str(),
      [](void *userdata, const char *dirname,
         const char *fname) -> SDL_EnumerationResult {
        if (!userdata || !dirname || !fname)
          return SDL_ENUM_CONTINUE;

        const auto *data = static_cast<TempCleanupData *>(userdata);
        const std::string currentName = fname;

        if (currentName == "." || currentName == ".." ||
            currentName == data->keepFileName) {
          return SDL_ENUM_CONTINUE;
        }

        if (currentName.size() >= 7 &&
            currentName.substr(currentName.size() - 7) == ".tmp.ks") {
          const std::string fullPath = JoinPath(dirname, currentName);
          SDL_RemovePath(fullPath.c_str());
        }

        return SDL_ENUM_CONTINUE;
      },
      &cleanupData);

  return JoinPath(baseDir, keepFileName);
}

static bool LoadTextFile(const std::string &path,
                                  std::string &outText) {
  if (path.empty()) {
    SetEditorStatus("Empty path.");
    return false;
  }

  size_t dataSize = 0;
  void *data = SDL_LoadFile(path.c_str(), &dataSize);

  if (data == nullptr) {
    SetEditorStatus(SDL_GetError());
    return false;
  }

  outText.assign(static_cast<const char *>(data), dataSize);
  SDL_free(data);
  return true;
}

static bool SaveTextFile(const std::string &path,
                                  const std::string &text) {
  if (path.empty()) {
    SetEditorStatus("Empty path.");
    return false;
  }

  SDL_IOStream *io = SDL_IOFromFile(path.c_str(), "wb");
  if (io == nullptr) {
    SetEditorStatus(SDL_GetError());
    return false;
  }

  const size_t written = SDL_WriteIO(io, text.data(), text.size());
  const bool closeOk = SDL_CloseIO(io);

  if (written != text.size() || !closeOk) {
    SetEditorStatus("Failed to write full file.");
    return false;
  }

  return true;
}

static bool LoadSceneEditorFromDisk(::Kotonoha::Gameplay *play) {
  if (play == nullptr || play->scriptPath.empty()) {
    SetEditorStatus("Invalid gameplay script path.");
    return false;
  }

  std::string loadedText;
  if (!LoadTextFile(play->scriptPath, loadedText)) {
    return false;
  }

  sceneEditorText = std::move(loadedText);
  LoadTextEditor(sceneEditorText);
  editorDirty = false;
  SetEditorStatus("Editor loaded from disk.");
  return true;
}

static bool OpenPaths(::Kotonoha::Kotonoha *game, const char *buffer,
                               bool append) {
  if (game == nullptr || buffer == nullptr)
    return false;

  bool ok = true;
  bool loadedAny = false;
  std::stringstream ss(buffer);
  std::string part;

  if (!append) {
    game->ClearGameplays();
  }

  while (std::getline(ss, part, ';')) {
    if (part.empty())
      continue;

    if (!game->LoadScriptFile(part.c_str())) {
      ok = false;
    } else {
      loadedAny = true;
    }
  }

  if (!loadedAny && !append) {
    game->ClearGameplays();
  }

  return ok && loadedAny;
}

static bool SelectScene(::Kotonoha::Kotonoha *game, int index) {
  ::Kotonoha::Gameplay *play = GetGameplayAt(game, index);
  if (play == nullptr) {
    SetEditorStatus("Invalid scene selection.");
    return false;
  }

  std::string loadedText;
  if (!LoadTextFile(play->scriptPath, loadedText)) {
    return false;
  }

  selectedSceneIndex = index;
  editorSceneIndex = index;
  sceneEditorText = std::move(loadedText);
  LoadTextEditor(sceneEditorText);
  editorDirty = false;
  SetEditorStatus("Editor loaded from disk.");
  return true;
}

static void TryAutoLoadCurrentScene(::Kotonoha::Kotonoha *game,
                                             Kotonoha_Game &context) {
  if (game == nullptr || context.config.scene >= game->gameplays.size()) {
    return;
  }

  if (editorSceneIndex >= 0 &&
      editorSceneIndex < static_cast<int>(game->gameplays.size())) {
    return;
  }

  SelectScene(game, static_cast<int>(context.config.scene));
}

static void GoToScene(::Kotonoha::Kotonoha *game, Kotonoha_Game &context,
                               int index) {
  if (!IsValidGameplayIndex(game, index))
    return;

  ::Kotonoha::Gameplay *item = game->gameplays[index];
  if (item == nullptr)
    return;

  if (context.config.scene == static_cast<size_t>(index)) {
    item->Reset(true);
    return;
  }

  const size_t older = context.config.scene;
  context.config.scene = index;
  game->gameplays[context.config.scene]->Reset(true);

  if (older < game->gameplays.size() && game->gameplays[older] != nullptr) {
    game->gameplays[older]->Pause();
  }
}

static bool RecreateGameplayAtPath(::Kotonoha::Kotonoha *game,
                                            Kotonoha_Game &context, int index,
                                            const std::string &newPath) {
  if (game == nullptr || !IsValidGameplayIndex(game, index) ||
      newPath.empty()) {
    SetEditorStatus("Invalid recreation parameters.");
    return false;
  }

  ::Kotonoha::Gameplay *oldPlay = game->gameplays[index];
  if (oldPlay == nullptr)
    return false;

  ::Kotonoha::Gameplay *newPlay = nullptr;

  try {
    TTF_Font *preferredFont = nullptr;
    auto it = game->fonts.find("ConcertOne-Regular");
    if (it != game->fonts.end()) {
      preferredFont = it->second;
    }
    newPlay = game->CreateGameplay(newPath.c_str(), preferredFont);
    if (newPlay == nullptr) {
      SetEditorStatus("Gameplay factory rejected scene recreation.");
      return false;
    }
  } catch (const std::exception &e) {
    SetEditorStatus(e.what());
    return false;
  } catch (...) {
    SetEditorStatus("Failed to recreate gameplay.");
    return false;
  }

  newPlay->SetTime(oldPlay->GetTime());
  game->gameplays[index] = newPlay;
  game->DeleteGameplay(oldPlay);

  if (selectedSceneIndex == index || editorSceneIndex == index) {
    selectedSceneIndex = index;
    editorSceneIndex = index;
    sceneEditorText.clear();
    editorDirty = false;
    LoadSceneEditorFromDisk(newPlay);
  }

  return true;
}

static bool SaveAndReloadReplace(::Kotonoha::Kotonoha *game,
                                          Kotonoha_Game &context, int index) {
  ::Kotonoha::Gameplay *selected = GetGameplayAt(game, index);
  if (selected == nullptr)
    return false;

  const std::string targetPath =
      (temporaryGameplayActive && temporaryGameplayIndex == index)
          ? originalScriptPathForTemporary
          : selected->scriptPath;

  if (targetPath.empty() ||
      !SaveTextFile(targetPath, sceneEditorText)) {
    return false;
  }

  editorDirty = false;
  temporaryGameplayActive = false;
  temporaryGameplayIndex = -1;
  temporaryScriptPath.clear();
  originalScriptPathForTemporary.clear();

  return RecreateGameplayAtPath(game, context, index, targetPath);
}

static bool LoadTemporaryGameplayAt(::Kotonoha::Kotonoha *game,
                                             Kotonoha_Game &context,
                                             int index) {
  if (game == nullptr || !IsValidGameplayIndex(game, index))
    return false;

  ::Kotonoha::Gameplay *selected = game->gameplays[index];
  if (selected == nullptr)
    return false;

  const std::string baseForTempPath =
      (temporaryGameplayActive && temporaryGameplayIndex == index)
          ? originalScriptPathForTemporary
          : selected->scriptPath;

  std::string tempPath = GetTemporaryScenePath(baseForTempPath);
  if (tempPath.empty() || !SaveTextFile(tempPath, sceneEditorText)) {
    return false;
  }

  if (!temporaryGameplayActive) {
    originalScriptPathForTemporary = selected->scriptPath;
  }

  temporaryScriptPath = tempPath;
  temporaryGameplayIndex = index;
  temporaryGameplayActive = true;
  editorDirty = false;

  return RecreateGameplayAtPath(game, context, index, tempPath);
}

static bool RestoreOriginalPath(::Kotonoha::Kotonoha *game,
                                         Kotonoha_Game &context) {
  if (!temporaryGameplayActive ||
      !IsValidGameplayIndex(game, temporaryGameplayIndex)) {
    return false;
  }

  const int index = temporaryGameplayIndex;
  const std::string originalPath = originalScriptPathForTemporary;

  temporaryGameplayActive = false;
  temporaryGameplayIndex = -1;
  temporaryScriptPath.clear();
  originalScriptPathForTemporary.clear();

  return RecreateGameplayAtPath(game, context, index, originalPath);
}

/* ---------------- Renderização de Componentes ImGui ---------------- */

static void DrawAboutFields() {
  ImGui::TextColored(ImVec4(0.85f, 0.65f, 0.98f, 1.0f),
                     "Kotonoha Visual Novel Engine");
  ImGui::Separator();
  ImGui::TextWrapped(
      "Kotonoha is a Visual Novel engine built upon SDL (Simple DirectMedia "
      "Layer) "
      "and FFmpeg. It is a reimagining and reimplementation of the FORIS "
      "engine, "
      "originally used in the \"Days\" series games produced by 0verflow.\n\n"
      "Its goal is to provide a versatile and robust platform for creating and "
      "playing visual novels.");

  ImGui::Spacing();
  if (ImGui::TreeNodeEx("Project Links", ImGuiTreeNodeFlags_DefaultOpen)) {
    ImGui::BulletText("GitHub: https://github.com/Ruaneri-Portela/Kotonoha");
    ImGui::TreePop();
  }

  if (ImGui::TreeNodeEx("Contributors", ImGuiTreeNodeFlags_DefaultOpen)) {
    ImGui::BulletText("Ruaneri Ferreira Portela");
    ImGui::BulletText("tsubaki-0xSUZU");
    ImGui::TreePop();
  }

  if (ImGui::TreeNodeEx("License", ImGuiTreeNodeFlags_DefaultOpen)) {
    ImGui::Text("MIT License");
    ImGui::TextDisabled("Copyright (c) 2023 Ruaneri Ferreira Portela");
    ImGui::TreePop();
  }
}

static void DrawAboutWindow() {
  if (!aboutWindowShow)
    return;

  const ImGuiViewport *viewport = ImGui::GetMainViewport();
  ImGui::SetNextWindowPos(
      ImVec2(viewport->WorkPos.x + 40, viewport->WorkPos.y + 40),
      ImGuiCond_FirstUseEver);
  ImGui::SetNextWindowSizeConstraints(
      ImVec2(380, 320),
      ImVec2(viewport->WorkSize.x * 0.8f, viewport->WorkSize.y * 0.8f));

  if (ImGui::Begin("About Kotonoha", &aboutWindowShow,
                   ImGuiWindowFlags_AlwaysAutoResize)) {
    DrawAboutFields();
  }
  ImGui::End();
}

static void DrawSettingsFields(Kotonoha_Config *cfg,
                                        Kotonoha_Game &context) {
  if (cfg == nullptr)
    return;

  if (ImGui::TreeNodeEx("Display & Graphics", ImGuiTreeNodeFlags_DefaultOpen)) {

    /* ---------------- Seleção de Display/Monitor ---------------- */
    int displayCount = 0;
    SDL_DisplayID *displays = SDL_GetDisplays(&displayCount);

    if (displays != nullptr && displayCount > 0) {
      SDL_DisplayID currentDisplay = SDL_GetDisplayForWindow(context.window);
      int currentDisplayIdx = 0;

      // Descobre o índice do display atual
      for (int i = 0; i < displayCount; ++i) {
        if (displays[i] == currentDisplay) {
          currentDisplayIdx = i;
          break;
        }
      }

      // Constrói a lista de nomes dos monitores
      std::vector<std::string> displayNames;
      std::vector<const char *> displayLabels;
      for (int i = 0; i < displayCount; ++i) {
        const char *name = SDL_GetDisplayName(displays[i]);
        std::string label = "Display " + std::to_string(i + 1) +
                            (name ? std::string(" (") + name + ")" : "");
        displayNames.push_back(label);
      }
      for (const auto &name : displayNames) {
        displayLabels.push_back(name.c_str());
      }

      if (ImGui::Combo("Target Display", &currentDisplayIdx,
                       displayLabels.data(),
                       static_cast<int>(displayLabels.size()))) {
        DisplayMode mode;
        if (GetDisplayMode(context, &mode) &&
            SetDisplay(context, mode, displays[currentDisplayIdx])) {
          cfg->dirtyConfig = true;
        } else if (SDL_GetError()[0] != '\0') {
          SDL_LogError(SDL_LOG_CATEGORY_VIDEO,
                       "Could not move Kotonoha to the selected display: %s",
                       SDL_GetError());
        }
      }
      SDL_free(displays);
    }

    /* ---------------- Modo de Tela (Windowed / Exclusive / Borderless)
     * ---------------- */
    const char *fullscreenModes[] = {"Windowed", "Fullscreen Exclusive",
                                     "Borderless Fullscreen"};

    DisplayMode displayMode = DisplayMode::Windowed;
    if (GetDisplayMode(context, &displayMode)) {
      int currentModeIdx = static_cast<int>(displayMode);
      if (ImGui::Combo("Screen Mode", &currentModeIdx, fullscreenModes,
                       IM_ARRAYSIZE(fullscreenModes)) &&
          currentModeIdx >= static_cast<int>(DisplayMode::Windowed) &&
          currentModeIdx <=
              static_cast<int>(DisplayMode::BorderlessFullscreen)) {
        if (SetDisplay(context, static_cast<DisplayMode>(currentModeIdx))) {
          cfg->dirtyConfig = true;
        }
      }
    } else {
      SDL_LogError(SDL_LOG_CATEGORY_VIDEO,
                   "Could not query Kotonoha display mode: %s", SDL_GetError());
    }

    /* ---------------- VSync ---------------- */
    bool vsyncBool = (cfg->enableVsync != 0);
    if (ImGui::Checkbox("Enable VSync", &vsyncBool)) {
      cfg->enableVsync = vsyncBool ? 1 : 0;
      cfg->dirtyConfig = true;
      SDL_SetRenderVSync(context.render, cfg->enableVsync);
    }

    /* ---------------- Resolução ---------------- */
    ImGui::Text("Window Resolution:");
    int windowWidth = cfg->windowSize[0].load();
    int windowHeight = cfg->windowSize[1].load();
    ImGui::SetNextItemWidth(100);
    if (ImGui::InputInt("W##res_w", &windowWidth)) {
      cfg->windowSize[0].store(windowWidth);
      cfg->dirtyConfig = true;
    }
    ImGui::SameLine();
    ImGui::SetNextItemWidth(100);
    if (ImGui::InputInt("H##res_h", &windowHeight)) {
      cfg->windowSize[1].store(windowHeight);
      cfg->dirtyConfig = true;
    }

    ImGui::SameLine();
    if (ImGui::Button("Apply")) {
      SDL_SetWindowSize(context.window, cfg->windowSize[0].load(),
                        cfg->windowSize[1].load());
    }

    float scalerFactor = cfg->scalerFactor.load();
    if (ImGui::SliderFloat("Scaler Factor", &scalerFactor, 0.5f, 4.0f,
                           "%.2fx")) {
      cfg->scalerFactor.store(scalerFactor);
      KotonohaUpscalingSetFactor(scalerFactor);
      cfg->dirtyConfig = true;
    }
    ImGui::TreePop();
  }

  if (ImGui::TreeNodeEx("Performance & Overlay",
                        ImGuiTreeNodeFlags_DefaultOpen)) {
    bool showFps = cfg->showFps.load();
    if (ImGui::Checkbox("Show FPS Counter", &showFps)) {
      cfg->showFps.store(showFps);
      cfg->dirtyConfig = true;
    }
    bool showTimestamp = cfg->showTimestamp.load();
    if (ImGui::Checkbox("Show Timestamps", &showTimestamp)) {
      cfg->showTimestamp.store(showTimestamp);
      cfg->dirtyConfig = true;
    }
    float mediaCacheSeconds = cfg->mediaCacheSeconds.load();
    if (ImGui::SliderFloat("Media Cache (s)", &mediaCacheSeconds, 1.0f,
                           15.0f, "%.1f sec")) {
      cfg->mediaCacheSeconds.store(mediaCacheSeconds);
      cfg->dirtyConfig = true;
    }
    ImGui::TreePop();
  }

  if (ImGui::TreeNodeEx("Paths & Assets", ImGuiTreeNodeFlags_DefaultOpen)) {
    static std::string assetsBuf = cfg->assetsPath ? cfg->assetsPath : "";
    if (ImGui::InputText("Assets Path", &assetsBuf)) {
      if (cfg->assetsPath)
        SDL_free(cfg->assetsPath);
      cfg->assetsPath = SDL_strdup(assetsBuf.c_str());
      cfg->dirtyConfig = true;
    }
    ImGui::TreePop();
  }

  ImGui::Separator();
  if (ImGui::Button("Save Configuration", ImVec2(-1, 28))) {
    KotonohaConfigSave(cfg);
    cfg->dirtyConfig = false;
  }

  if (cfg->dirtyConfig) {
    ImGui::TextColored(ImVec4(0.95f, 0.50f, 0.95f, 1.0f), "* Unsaved Settings");
  }
}

static void DrawSettingsWindow(Kotonoha_Game &context) {
  if (!settingsWindowShow)
    return;

  const ImGuiViewport *viewport = ImGui::GetMainViewport();
  ImGui::SetNextWindowPos(
      ImVec2(viewport->WorkPos.x + 20, viewport->WorkPos.y + 20),
      ImGuiCond_FirstUseEver);
  ImGui::SetNextWindowSizeConstraints(
      ImVec2(340, 320),
      ImVec2(viewport->WorkSize.x * 0.9f, viewport->WorkSize.y * 0.9f));

  if (ImGui::Begin("Engine Settings", &settingsWindowShow,
                   ImGuiWindowFlags_AlwaysAutoResize)) {
    DrawSettingsFields(&context.config, context);
  }
  ImGui::End();
}

static void DrawPlayback(::Kotonoha::Kotonoha *game,
                                  ::Kotonoha::Gameplay *play,
                                  Kotonoha_Game &context) {
  if (play == nullptr ||
      !ImGui::CollapsingHeader("Playback Controls",
                               ImGuiTreeNodeFlags_DefaultOpen)) {
    return;
  }

  float newTime = play->GetTime();
  const float lastTime = play->GetLastTime();

  ImGui::TextColored(
      ImVec4(0.85f, 0.75f, 0.95f, 1.0f), "Time: %s / %s",
      FormatTime(static_cast<Uint64>(newTime * 1000.0f)).c_str(),
      FormatTime(static_cast<Uint64>(lastTime * 1000.0f)).c_str());

  if (ImGui::SliderFloat("Timeline", &newTime, 0.0f, lastTime)) {
    play->SetTime(newTime);
  }

  const float buttonWidth =
      (ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x * 3) /
      4.0f;

  if (ImGui::Button("<< 5s", ImVec2(buttonWidth, 0)))
    play->SeekBackward(5000);
  ImGui::SameLine();
  if (ImGui::Button(">> 5s", ImVec2(buttonWidth, 0)))
    play->SeekForward(5000);
  ImGui::SameLine();
  bool paused = context.config.paused.load();
  if (ImGui::Checkbox("Pause", &paused)) {
    context.config.paused.store(paused);
  }
  ImGui::SameLine();
  ImGui::Checkbox("Loop", &play->loop);

  if (ImGui::Button("Reset Scene", ImVec2(buttonWidth, 0)))
    play->Reset(true);
  ImGui::SameLine();
  if (ImGui::Button("Next", ImVec2(buttonWidth, 0)))
    context.config.next = true;
  ImGui::SameLine();
  if (ImGui::Button("Previous", ImVec2(buttonWidth, 0)))
    context.config.back = true;
  ImGui::SameLine();
  if (ImGui::Button("Exit", ImVec2(buttonWidth, 0))) {
    GoToScene(game, context,
                       static_cast<int>(game->gameplays.size()) - 1);
    context.config.next = true;
  }
}

static void DrawAudio(Kotonoha_Game &context) {
  if (!ImGui::CollapsingHeader("Audio Mixer", ImGuiTreeNodeFlags_DefaultOpen))
    return;

  auto *sound = static_cast<Sound *>(context.sound);
  if (sound == nullptr) {
    ImGui::TextUnformatted("Sound system unavailable.");
    return;
  }

  float masterVolume = sound->volume.load(std::memory_order_relaxed);
  if (ImGui::SliderFloat("Master Volume", &masterVolume, 0.0f, 1.0f)) {
    sound->volume.store(masterVolume, std::memory_order_relaxed);
  }

  for (size_t i = 0;; ++i) {
    Sound::Channel *channel = sound->GetChannelByIndex(i);
    if (channel == nullptr)
      break;

    float channelVolume = channel->volume.load(std::memory_order_relaxed);
    if (ImGui::SliderFloat(channel->name.c_str(), &channelVolume, 0.0f, 1.0f)) {
      channel->volume.store(channelVolume, std::memory_order_relaxed);
    }
  }
}

static void DrawScriptToggles(::Kotonoha::Kotonoha *game,
                                       Kotonoha_Game &context) {
  if (!ImGui::CollapsingHeader("Tools & Panels",
                               ImGuiTreeNodeFlags_DefaultOpen))
    return;

  ImGui::Checkbox("Playlist", &show_playlist);
  ImGui::SameLine();
  if (ImGui::Checkbox("Script Editor", &basicGuiEditorShow) &&
      basicGuiEditorShow) {
    SelectScene(game, static_cast<int>(context.config.scene));
  }
  ImGui::SameLine();
  if (ImGui::Checkbox("Text Editor", &textEditorShow) && textEditorShow) {
    SelectScene(game, static_cast<int>(context.config.scene));
  }
  ImGui::SameLine();
  ImGui::Checkbox("Settings Window", &settingsWindowShow);
  ImGui::SameLine();
  ImGui::Checkbox("About", &aboutWindowShow);
}

static void DrawMainWindow(::Kotonoha::Kotonoha *game,
                                    ::Kotonoha::Gameplay *play,
                                    Kotonoha_Game &context) {
  if (play == nullptr)
    return;

  const ImGuiViewport *viewport = ImGui::GetMainViewport();
  ImGui::SetNextWindowPos(
      ImVec2(viewport->WorkPos.x + 20, viewport->WorkPos.y + 20),
      ImGuiCond_FirstUseEver);
  ImGui::SetNextWindowSizeConstraints(
      ImVec2(380, 440),
      ImVec2(viewport->WorkSize.x * 0.9f, viewport->WorkSize.y * 0.9f));

  bool debugUiEnabled = context.config.enableDebugUi.load();
  const bool panelOpen =
      ImGui::Begin("Kotonoha Control Panel", &debugUiEnabled);
  context.config.enableDebugUi.store(debugUiEnabled);
  if (panelOpen) {
    ImGui::TextColored(ImVec4(0.80f, 0.60f, 0.95f, 1.0f),
                       "Current Scene: %d / %d",
                       static_cast<int>(context.config.scene) + 1,
                       static_cast<int>(game->gameplays.size()));

    ImGui::TextWrapped("File: %s", play->scriptPath.empty()
                                       ? "<no file path>"
                                       : play->scriptPath.c_str());
    ImGui::Separator();

    DrawPlayback(game, play, context);
    DrawAudio(context);

    if (ImGui::CollapsingHeader("Engine Settings")) {
      DrawSettingsFields(&context.config, context);
    }

    if (ImGui::CollapsingHeader("About Kotonoha")) {
      DrawAboutFields();
    }

    DrawScriptToggles(game, context);
  }
  ImGui::End();
}

static void DrawOpenWindow(::Kotonoha::Kotonoha *game,
                                    Kotonoha_Game &context, bool &shouldExit) {
  const ImGuiViewport *viewport = ImGui::GetMainViewport();
  ImGui::SetNextWindowPos(
      ImVec2(viewport->WorkPos.x + 20, viewport->WorkPos.y + 20),
      ImGuiCond_FirstUseEver);
  ImGui::SetNextWindowSizeConstraints(
      ImVec2(360, 280),
      ImVec2(viewport->WorkSize.x * 0.9f, viewport->WorkSize.y * 0.9f));

  if (ImGui::Begin("Open Scenes")) {
    if (ImGui::CollapsingHeader("Load Scenes",
                                ImGuiTreeNodeFlags_DefaultOpen)) {

      ImGui::SetNextItemWidth(-80);
      const bool enterPressed = ImGui::InputTextWithHint(
          "##open_script_path", "file1.ks;file2.ks;folder", openBuf,
          IM_ARRAYSIZE(openBuf), ImGuiInputTextFlags_EnterReturnsTrue);

      ImGui::SameLine();
      if (enterPressed || ImGui::Button("Open", ImVec2(70, 0))) {
        showError = !OpenPaths(game, openBuf, false);
        selectedSceneIndex = -1;
        editorSceneIndex = -1;
        sceneEditorText.clear();
        editorDirty = false;

        temporaryGameplayActive = false;
        temporaryGameplayIndex = -1;

        if (!game->gameplays.empty()) {
          context.config.scene = 0;
          TryAutoLoadCurrentScene(game, context);
        }
      }

      if (ImGui::Button("Append Scenes", ImVec2(-1, 0))) {
        showError = !OpenPaths(game, openBuf, true);
        if (selectedSceneIndex < 0 && !game->gameplays.empty()) {
          TryAutoLoadCurrentScene(game, context);
        }
      }

      if (showError) {
        ImGui::TextColored(ImVec4(1.0f, 0.35f, 0.45f, 1.0f),
                           "Failed to open one or more paths.");
      }

      if (!game->gameplays.empty()) {
        ImGui::Separator();
        ImGui::Text("Loaded scenes: %d",
                    static_cast<int>(game->gameplays.size()));

        if (ImGui::Button("Start from First", ImVec2(-1, 0))) {
          context.config.scene = 0;
          TryAutoLoadCurrentScene(game, context);
        }

        ImGui::Checkbox("Playlist", &show_playlist);
        ImGui::SameLine();
        ImGui::Checkbox("Script Editor", &basicGuiEditorShow);
        ImGui::SameLine();
        ImGui::Checkbox("Text Editor", &textEditorShow);
        ImGui::SameLine();
        ImGui::Checkbox("Settings Window", &settingsWindowShow);
        ImGui::SameLine();
        ImGui::Checkbox("About", &aboutWindowShow);
      }

      if (ImGui::CollapsingHeader("Engine Settings")) {
        DrawSettingsFields(&context.config, context);
      }

      if (ImGui::CollapsingHeader("About Kotonoha")) {
        DrawAboutFields();
      }

      ImGui::Separator();
      if (ImGui::Button("Exit Application", ImVec2(-1, 0))) {
        shouldExit = true;
      }
    }
  }
  ImGui::End();
}

static void DrawPlaylist(::Kotonoha::Kotonoha *game,
                                  Kotonoha_Game &context) {
  if (!show_playlist)
    return;

  const ImGuiViewport *viewport = ImGui::GetMainViewport();
  ImGui::SetNextWindowSizeConstraints(
      ImVec2(320, 280),
      ImVec2(viewport->WorkSize.x * 0.9f, viewport->WorkSize.y * 0.9f));

  if (ImGui::Begin("Playlist", &show_playlist)) {
    if (context.config.scene < game->gameplays.size() &&
        game->gameplays[context.config.scene] != nullptr) {
      ImGui::TextWrapped(
          "Current File: %s",
          game->gameplays[context.config.scene]->scriptPath.empty()
              ? "<no file path>"
              : game->gameplays[context.config.scene]->scriptPath.c_str());
    } else {
      ImGui::TextUnformatted("Current File: <none>");
    }

    ImGui::TextColored(ImVec4(0.80f, 0.60f, 0.95f, 1.0f), "Total Scenes: %d",
                       static_cast<int>(game->gameplays.size()));

    if (ImGui::CollapsingHeader("Append Scenes",
                                ImGuiTreeNodeFlags_DefaultOpen)) {
      ImGui::SetNextItemWidth(-80);
      ImGui::InputTextWithHint("##append_scene_path", "append file or folder",
                               appendBuf, IM_ARRAYSIZE(appendBuf));
      ImGui::SameLine();
      if (ImGui::Button("Append##playlist", ImVec2(70, 0))) {
        showError = !OpenPaths(game, appendBuf, true);
      }
    }

    ImVec2 listSize = ImGui::GetContentRegionAvail();
    listSize.y = std::max(listSize.y - 10.0f, 120.0f);

    if (ImGui::CollapsingHeader("Scene Queue",
                                ImGuiTreeNodeFlags_DefaultOpen)) {
      if (ImGui::BeginListBox("##scene_playlist", listSize)) {
        for (int i = 0; i < static_cast<int>(game->gameplays.size()); ++i) {
          ::Kotonoha::Gameplay *item = game->gameplays[i];
          if (item == nullptr)
            continue;

          const std::string label =
              item->scriptPath.empty()
                  ? "Scene " + std::to_string(i)
                  : std::to_string(i) + " - " + item->scriptPath;

          const bool selected =
              (context.config.scene == static_cast<size_t>(i));
          if (ImGui::Selectable(label.c_str(), selected)) {
            GoToScene(game, context, i);
          }
          if (selected) {
            ImGui::SetItemDefaultFocus();
          }
        }
        ImGui::EndListBox();
      }
    }
  }
  ImGui::End();
}

static bool DrawScriptEditor(::Kotonoha::Kotonoha *game,
                                      Kotonoha_Game &context) {
  if (!basicGuiEditorShow)
    return true;

  if (context.config.scene != static_cast<size_t>(editorSceneIndex) ||
      editorSceneIndex < 0) {
    SelectScene(game, static_cast<int>(context.config.scene));
  }

  bool shouldFinishFrame = false;

  const ImGuiViewport *viewport = ImGui::GetMainViewport();
  ImGui::SetNextWindowSizeConstraints(
      ImVec2(400, 300),
      ImVec2(viewport->WorkSize.x * 0.95f, viewport->WorkSize.y * 0.95f));

  if (ImGui::Begin("Script Editor", &basicGuiEditorShow)) {
    if (selectedSceneIndex >= 0 &&
        selectedSceneIndex < static_cast<int>(game->gameplays.size())) {
      ::Kotonoha::Gameplay *selected = game->gameplays[selectedSceneIndex];

      if (selected != nullptr) {
        ImGui::TextColored(ImVec4(0.80f, 0.60f, 0.95f, 1.0f),
                           "Selected Scene: %d", selectedSceneIndex);
        ImGui::TextWrapped("File: %s", selected->scriptPath.empty()
                                           ? "<no file path>"
                                           : selected->scriptPath.c_str());

        if (temporaryGameplayActive &&
            temporaryGameplayIndex == selectedSceneIndex) {
          ImGui::TextColored(ImVec4(0.95f, 0.50f, 0.95f, 1.0f),
                             "Temporary gameplay active.");
        }

        if (ImGui::Button("Load From Disk") &&
            LoadSceneEditorFromDisk(selected)) {
          RecreateGameplayAtPath(game, context, selectedSceneIndex,
                                          selected->scriptPath);
        }

        ImVec2 avail = ImGui::GetContentRegionAvail();
        avail.y = std::max(avail.y - 70.0f, 120.0f);

        if (ImGui::InputTextMultiline("##scene_editor", &sceneEditorText,
                                      avail)) {
          editorDirty = true;
          LoadTextEditor(sceneEditorText);
        }

        if (editorStatusBuf[0] != '\0') {
          ImGui::TextColored(ImVec4(0.80f, 0.60f, 0.95f, 1.0f), "%s",
                             editorStatusBuf);
        }

        ImGui::TextUnformatted(editorDirty ? "* modified" : "saved");
        ImGui::SameLine();

        if (ImGui::Button("Save and Reload") &&
            SaveAndReloadReplace(game, context, selectedSceneIndex)) {
          shouldFinishFrame = true;
        }

        ImGui::SameLine();
        if (ImGui::Button("Save Temporary") &&
            LoadTemporaryGameplayAt(game, context,
                                             selectedSceneIndex)) {
          shouldFinishFrame = true;
        }

        if (temporaryGameplayActive &&
            temporaryGameplayIndex == selectedSceneIndex) {
          ImGui::SameLine();
          if (ImGui::Button("Revert to Original") &&
              RestoreOriginalPath(game, context)) {
            shouldFinishFrame = true;
          }
        }
      }
    } else {
      ImGui::TextUnformatted("No scene selected.");
    }
  }
  ImGui::End();
  return !shouldFinishFrame;
}

static void DrawTextEventList() {
  ImVec2 listSize = ImGui::GetContentRegionAvail();
  listSize.y -= 50.0f;

  if (ImGui::BeginChild("##text_event_list", listSize,
                        ImGuiChildFlags_Borders)) {
    if (textEditorEntries.empty()) {
      ImGui::TextDisabled("No PRINT_TEXT or SetSELECT in this scene.");
    }

    bool changed = false;

    for (size_t i = 0; i < textEditorEntries.size(); ++i) {
      TextEditorEntry &entry = textEditorEntries[i];
      ImGui::PushID(static_cast<int>(i));

      ImGui::TextColored(ImVec4(0.80f, 0.60f, 0.95f, 1.0f), "%s  %s - %s",
                         entry.isSelection ? "Choice" : "Speak",
                         entry.startTime.c_str(), entry.endTime.c_str());

      if (entry.isSelection) {
        for (size_t option = 0; option < entry.options.size(); ++option) {
          ImGui::SetNextItemWidth(-10);
          const std::string label = "Option " + std::to_string(option + 1);
          if (ImGui::InputText(label.c_str(), &entry.options[option])) {
            changed = true;
          }
        }
      } else {
        ImGui::SetNextItemWidth(-10);
        if (ImGui::InputText("Character", &entry.character))
          changed = true;

        ImGui::SetNextItemWidth(-10);
        if (ImGui::InputText("Dialogue", &entry.text))
          changed = true;
      }

      ImGui::Separator();
      ImGui::PopID();
    }

    if (changed) {
      SyncTextEditorToScript();
      editorDirty = true;
    }
  }
  ImGui::EndChild();
}

static bool DrawTextEditor(::Kotonoha::Kotonoha *game,
                                    Kotonoha_Game &context) {
  if (!textEditorShow)
    return true;

  if (context.config.scene != static_cast<size_t>(editorSceneIndex) ||
      editorSceneIndex < 0) {
    SelectScene(game, static_cast<int>(context.config.scene));
  } else if (sceneEditorText != textEditorSyncedScript) {
    LoadTextEditor(sceneEditorText);
  }

  bool shouldFinishFrame = false;

  const ImGuiViewport *viewport = ImGui::GetMainViewport();
  ImGui::SetNextWindowSizeConstraints(
      ImVec2(380, 300),
      ImVec2(viewport->WorkSize.x * 0.95f, viewport->WorkSize.y * 0.95f));

  if (ImGui::Begin("Text Editor", &textEditorShow)) {
    if (selectedSceneIndex >= 0 &&
        selectedSceneIndex < static_cast<int>(game->gameplays.size()) &&
        game->gameplays[selectedSceneIndex] != nullptr) {
      ::Kotonoha::Gameplay *selected = game->gameplays[selectedSceneIndex];

      ImGui::TextColored(ImVec4(0.80f, 0.60f, 0.95f, 1.0f), "Scene %d",
                         selectedSceneIndex + 1);
      ImGui::TextWrapped("File: %s", selected->scriptPath.empty()
                                         ? "<No path to file>"
                                         : selected->scriptPath.c_str());
      ImGui::Separator();

      DrawTextEventList();

      if (ImGui::Button("Save and Reload")) {
        if (SaveAndReloadReplace(game, context, selectedSceneIndex)) {
          shouldFinishFrame = true;
        }
      }

      ImGui::SameLine();
      if (ImGui::Button("Save Temporary")) {
        if (LoadTemporaryGameplayAt(game, context,
                                             selectedSceneIndex)) {
          shouldFinishFrame = true;
        }
      }

      if (temporaryGameplayActive &&
          temporaryGameplayIndex == selectedSceneIndex) {
        ImGui::SameLine();
        if (ImGui::Button("Revert to Original")) {
          if (RestoreOriginalPath(game, context)) {
            shouldFinishFrame = true;
          }
        }
      }
    }
  }
  ImGui::End();
  return !shouldFinishFrame;
}

/* ---------------- Configuração do Tema Roxo e Ciclo de Vida ----------------
 */

static void ApplyPurpleTheme() {
  ImGuiStyle &style = ImGui::GetStyle();

  style.WindowRounding = 8.0f;
  style.ChildRounding = 6.0f;
  style.FrameRounding = 5.0f;
  style.PopupRounding = 6.0f;
  style.ScrollbarRounding = 8.0f;
  style.GrabRounding = 5.0f;
  style.TabRounding = 5.0f;

  style.WindowPadding = ImVec2(10.0f, 10.0f);
  style.FramePadding = ImVec2(6.0f, 4.0f);
  style.ItemSpacing = ImVec2(8.0f, 6.0f);
  style.ItemInnerSpacing = ImVec2(6.0f, 4.0f);

  ImVec4 *colors = style.Colors;

  // Fundo e Estrutura Principal
  colors[ImGuiCol_WindowBg] = ImVec4(0.09f, 0.07f, 0.13f, 0.96f);
  colors[ImGuiCol_ChildBg] = ImVec4(0.07f, 0.05f, 0.10f, 0.80f);
  colors[ImGuiCol_PopupBg] = ImVec4(0.11f, 0.08f, 0.16f, 0.98f);
  colors[ImGuiCol_Border] = ImVec4(0.28f, 0.18f, 0.42f, 0.60f);
  colors[ImGuiCol_BorderShadow] = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);

  // Cabeçalhos e Títulos
  colors[ImGuiCol_TitleBg] = ImVec4(0.12f, 0.08f, 0.18f, 1.00f);
  colors[ImGuiCol_TitleBgActive] = ImVec4(0.24f, 0.12f, 0.36f, 1.00f);
  colors[ImGuiCol_TitleBgCollapsed] = ImVec4(0.08f, 0.05f, 0.12f, 0.75f);
  colors[ImGuiCol_MenuBarBg] = ImVec4(0.12f, 0.08f, 0.18f, 1.00f);

  // Campos de Entrada (InputText, Checkbox, Slider)
  colors[ImGuiCol_FrameBg] = ImVec4(0.16f, 0.11f, 0.24f, 1.00f);
  colors[ImGuiCol_FrameBgHovered] = ImVec4(0.24f, 0.15f, 0.36f, 1.00f);
  colors[ImGuiCol_FrameBgActive] = ImVec4(0.30f, 0.18f, 0.45f, 1.00f);

  // Botões
  colors[ImGuiCol_Button] = ImVec4(0.32f, 0.16f, 0.48f, 0.85f);
  colors[ImGuiCol_ButtonHovered] = ImVec4(0.42f, 0.22f, 0.62f, 1.00f);
  colors[ImGuiCol_ButtonActive] = ImVec4(0.50f, 0.26f, 0.72f, 1.00f);

  // Collapsing Headers & Selectables
  colors[ImGuiCol_Header] = ImVec4(0.28f, 0.15f, 0.42f, 0.75f);
  colors[ImGuiCol_HeaderHovered] = ImVec4(0.38f, 0.20f, 0.56f, 0.85f);
  colors[ImGuiCol_HeaderActive] = ImVec4(0.46f, 0.24f, 0.68f, 1.00f);

  // Elementos de Interação (Sliders, Checkmarks)
  colors[ImGuiCol_CheckMark] = ImVec4(0.80f, 0.45f, 0.95f, 1.00f);
  colors[ImGuiCol_SliderGrab] = ImVec4(0.56f, 0.28f, 0.82f, 1.00f);
  colors[ImGuiCol_SliderGrabActive] = ImVec4(0.68f, 0.34f, 0.98f, 1.00f);

  // Separadores e Scrollbars
  colors[ImGuiCol_Separator] = ImVec4(0.28f, 0.18f, 0.42f, 0.60f);
  colors[ImGuiCol_SeparatorHovered] = ImVec4(0.45f, 0.24f, 0.65f, 0.80f);
  colors[ImGuiCol_SeparatorActive] = ImVec4(0.60f, 0.30f, 0.85f, 1.00f);

  colors[ImGuiCol_ScrollbarBg] = ImVec4(0.08f, 0.05f, 0.12f, 0.60f);
  colors[ImGuiCol_ScrollbarGrab] = ImVec4(0.24f, 0.14f, 0.36f, 1.00f);
  colors[ImGuiCol_ScrollbarGrabHovered] = ImVec4(0.36f, 0.20f, 0.52f, 1.00f);
  colors[ImGuiCol_ScrollbarGrabActive] = ImVec4(0.48f, 0.25f, 0.68f, 1.00f);

  // Texto
  colors[ImGuiCol_Text] = ImVec4(0.94f, 0.92f, 0.98f, 1.00f);
  colors[ImGuiCol_TextDisabled] = ImVec4(0.50f, 0.44f, 0.58f, 1.00f);
  colors[ImGuiCol_TextSelectedBg] = ImVec4(0.48f, 0.22f, 0.68f, 0.45f);
}

bool BasicGuiInit(Kotonoha_Game &gameContext, std::vector<Uint8> font) {
  if (ImGui::GetCurrentContext() == nullptr || gameContext.window == nullptr ||
      gameContext.render == nullptr) {
    SDL_SetError("Debug UI requires an ImGui context, window, and renderer");
    return false;
  }

  ImGuiIO &io = ImGui::GetIO();
  io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
  io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;

  ImFont *mainFont = nullptr;

  if (!font.empty()) {
    const size_t fontDataSize = font.size();
    void *fontData = SDL_malloc(fontDataSize);
    if (fontData != nullptr) {
      SDL_memcpy(fontData, font.data(), fontDataSize);
      ImFontConfig fontConfig;
      fontConfig.RasterizerDensity =
          std::max(1.0f, SDL_GetWindowPixelDensity(gameContext.window));
      mainFont = io.Fonts->AddFontFromMemoryTTF(
          fontData, static_cast<int>(fontDataSize), 16.0f, &fontConfig);
    }
  }

  if (mainFont == nullptr) {
    mainFont = io.Fonts->AddFontDefault();
  }

  io.FontDefault = mainFont;

  ApplyPurpleTheme();

  imguiSdlBackendInitialized =
      ImGui_ImplSDL3_InitForSDLRenderer(gameContext.window, gameContext.render);
  if (!imguiSdlBackendInitialized) {
    SDL_SetError("Failed to initialize ImGui SDL3 backend");
    return false;
  }
  imguiRendererBackendInitialized =
      ImGui_ImplSDLRenderer3_Init(gameContext.render);
  if (!imguiRendererBackendInitialized) {
    SDL_SetError("Failed to initialize ImGui SDL renderer backend");
    BasicGuiShutdown();
    return false;
  }
  return true;
}

void BasicGuiShutdown() {
  if (imguiRendererBackendInitialized) {
    ImGui_ImplSDLRenderer3_Shutdown();
    imguiRendererBackendInitialized = false;
  }
  if (imguiSdlBackendInitialized) {
    ImGui_ImplSDL3_Shutdown();
    imguiSdlBackendInitialized = false;
  }
  if (ImGui::GetCurrentContext() != nullptr) {
    ImGui::DestroyContext();
  }
}

bool BasicGuiRun(::Kotonoha::Kotonoha *game, ::Kotonoha::Gameplay *play,
                 Kotonoha_Game &context) {
  if (!context.config.enableDebugUi || game == nullptr) {
    return true;
  }

  TryAutoLoadCurrentScene(game, context);

  if (context.config.scene < game->gameplays.size()) {
    play = game->gameplays[context.config.scene];
  } else {
    play = nullptr;
  }

  ImGui_ImplSDLRenderer3_NewFrame();
  ImGui_ImplSDL3_NewFrame();
  ImGui::NewFrame();
  bool shouldExit = false;

  if (play != nullptr) {
    DrawMainWindow(game, play, context);
  } else {
    DrawOpenWindow(game, context, shouldExit);
  }

  DrawPlaylist(game, context);
  DrawSettingsWindow(context);
  DrawAboutWindow();

  if (!DrawScriptEditor(game, context)) {
    return FinishFrameAndReturn(context);
  }

  if (!DrawTextEditor(game, context)) {
    return FinishFrameAndReturn(context);
  }

  return !shouldExit && FinishFrameAndReturn(context);
}

void BasicGuiEvent(SDL_Event *event) {
  if (imguiSdlBackendInitialized && event != nullptr) {
    ImGui_ImplSDL3_ProcessEvent(event);
  }
}

} // namespace Kotonoha
