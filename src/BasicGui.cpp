#include <Kotonoha/Kotonoha.hpp>
#include <Kotonoha/speciais/Upscaling.h>
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

/* ---------------- Estado Global da Interface ---------------- */

bool KotonohaBasicGuiEditorShow = false;
bool KotonohaBasicGuiAnyFocus = false;

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
static bool textEditorDirty = false;

static std::string originalScriptPathForTemporary;
static std::string temporaryScriptPath;
static int temporaryGameplayIndex = -1;
static bool temporaryGameplayActive = false;
static ImGuiStyle debugUiBaseStyle;
static bool debugUiBaseStyleReady = false;
static float appliedDebugUiScale = 1.0f;

/* ---------------- Funções Utilitárias ---------------- */

static void Kotonoha_SetEditorStatus(const char *text) {
  SDL_snprintf(editorStatusBuf, sizeof(editorStatusBuf), "%s",
               text ? text : "");
}

static bool Kotonoha_IsValidGameplayIndex(Kotonoha::Kotonoha *game, int index) {
  return game != nullptr && index >= 0 &&
         index < static_cast<int>(game->gameplays.size());
}

static Kotonoha::Gameplay *Kotonoha_GetGameplayAt(Kotonoha::Kotonoha *game,
                                                  int index) {
  if (!Kotonoha_IsValidGameplayIndex(game, index)) {
    return nullptr;
  }
  return game->gameplays[index];
}

static std::string Kotonoha_JoinPath(const std::string &dir,
                                     const std::string &name) {
  if (dir.empty())
    return name;
  const char last = dir.back();
  if (last == '/' || last == '\\') {
    return dir + name;
  }
#ifdef _WIN32
  return dir + "\\" + name;
#else
  return dir + "/" + name;
#endif
}

static std::vector<std::string> Kotonoha_SplitTabs(const std::string &text) {
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

static std::string Kotonoha_JoinLines(const std::vector<std::string> &lines,
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

static std::string Kotonoha_FormatTime(Uint64 milliseconds) {
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

static std::string Kotonoha_FormatScriptTime(const std::string &timestamp) {
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

  return Kotonoha_FormatTime(milliseconds + parts[2] * 10);
}

static bool Kotonoha_FinishFrameAndReturn(Kotonoha_Game &context) {
  ImGui::Render();
  const ImVec2 fbScale = ImGui::GetIO().DisplayFramebufferScale;
  SDL_SetRenderScale(context.render, fbScale.x, fbScale.y);
  ImGui_ImplSDLRenderer3_RenderDrawData(ImGui::GetDrawData(), context.render);
  SDL_SetRenderScale(context.render, 1.0f, 1.0f);
  return true;
}

/* ---------------- Gerenciador do Editor de Texto e Sincronização
 * ---------------- */

static void Kotonoha_LoadTextEditor(const std::string &source) {
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
    const std::vector<std::string> fields = Kotonoha_SplitTabs(payload);

    if ((isPrintText && fields.size() != 4) ||
        (isSelection && fields.size() < 3)) {
      continue;
    }

    TextEditorEntry entry;
    entry.isSelection = isSelection;
    entry.lineIndex = i;
    entry.startTime = Kotonoha_FormatScriptTime(fields.front());
    entry.endTime = Kotonoha_FormatScriptTime(fields.back());

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

static void Kotonoha_SyncTextEditorToScript() {
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

  sceneEditorText = Kotonoha_JoinLines(textEditorLines, textEditorFinalNewline);
  textEditorSyncedScript = sceneEditorText;
  textEditorDirty = true;
}

/* ---------------- I/O e Operações de Cena ---------------- */

static std::string Kotonoha_GetTemporaryScenePath(const std::string &path) {
  if (path.empty())
    return "";

  char *prefPath = SDL_GetPrefPath("Kotonoha", "Engine");
  if (prefPath == nullptr) {
    Kotonoha_SetEditorStatus(SDL_GetError());
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
          const std::string fullPath = Kotonoha_JoinPath(dirname, currentName);
          SDL_RemovePath(fullPath.c_str());
        }

        return SDL_ENUM_CONTINUE;
      },
      &cleanupData);

  return Kotonoha_JoinPath(baseDir, keepFileName);
}

static bool Kotonoha_LoadTextFile(const std::string &path,
                                  std::string &outText) {
  if (path.empty()) {
    Kotonoha_SetEditorStatus("Empty path.");
    return false;
  }

  size_t dataSize = 0;
  void *data = SDL_LoadFile(path.c_str(), &dataSize);

  if (data == nullptr) {
    Kotonoha_SetEditorStatus(SDL_GetError());
    return false;
  }

  outText.assign(static_cast<const char *>(data), dataSize);
  SDL_free(data);
  return true;
}

static bool Kotonoha_SaveTextFile(const std::string &path,
                                  const std::string &text) {
  if (path.empty()) {
    Kotonoha_SetEditorStatus("Empty path.");
    return false;
  }

  SDL_IOStream *io = SDL_IOFromFile(path.c_str(), "wb");
  if (io == nullptr) {
    Kotonoha_SetEditorStatus(SDL_GetError());
    return false;
  }

  const size_t written = SDL_WriteIO(io, text.data(), text.size());
  const bool closeOk = SDL_CloseIO(io);

  if (written != text.size() || !closeOk) {
    Kotonoha_SetEditorStatus("Failed to write full file.");
    return false;
  }

  return true;
}

static bool Kotonoha_LoadSceneEditorFromDisk(Kotonoha::Gameplay *play) {
  if (play == nullptr || play->scriptPath.empty()) {
    Kotonoha_SetEditorStatus("Invalid gameplay script path.");
    return false;
  }

  std::string loadedText;
  if (!Kotonoha_LoadTextFile(play->scriptPath, loadedText)) {
    return false;
  }

  sceneEditorText = std::move(loadedText);
  Kotonoha_LoadTextEditor(sceneEditorText);
  editorDirty = false;
  textEditorDirty = false;
  Kotonoha_SetEditorStatus("Editor loaded from disk.");
  return true;
}

static bool Kotonoha_OpenPaths(Kotonoha::Kotonoha *game, const char *buffer,
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

static bool Kotonoha_SelectScene(Kotonoha::Kotonoha *game, int index) {
  Kotonoha::Gameplay *play = Kotonoha_GetGameplayAt(game, index);
  if (play == nullptr) {
    Kotonoha_SetEditorStatus("Invalid scene selection.");
    return false;
  }

  std::string loadedText;
  if (!Kotonoha_LoadTextFile(play->scriptPath, loadedText)) {
    return false;
  }

  selectedSceneIndex = index;
  editorSceneIndex = index;
  sceneEditorText = std::move(loadedText);
  Kotonoha_LoadTextEditor(sceneEditorText);
  editorDirty = false;
  textEditorDirty = false;
  Kotonoha_SetEditorStatus("Editor loaded from disk.");
  return true;
}

static void Kotonoha_TryAutoLoadCurrentScene(Kotonoha::Kotonoha *game,
                                             Kotonoha_Game &context) {
  if (game == nullptr || context.config.scene >= game->gameplays.size()) {
    return;
  }

  if (editorSceneIndex >= 0 &&
      editorSceneIndex < static_cast<int>(game->gameplays.size())) {
    return;
  }

  Kotonoha_SelectScene(game, static_cast<int>(context.config.scene));
}

static void Kotonoha_GoToScene(Kotonoha::Kotonoha *game, Kotonoha_Game &context,
                               int index) {
  if (!Kotonoha_IsValidGameplayIndex(game, index))
    return;

  Kotonoha::Gameplay *item = game->gameplays[index];
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

static bool Kotonoha_RecreateGameplayAtPath(Kotonoha::Kotonoha *game,
                                            Kotonoha_Game &context, int index,
                                            const std::string &newPath) {
  if (game == nullptr || !Kotonoha_IsValidGameplayIndex(game, index) ||
      newPath.empty()) {
    Kotonoha_SetEditorStatus("Invalid recreation parameters.");
    return false;
  }

  Kotonoha::Gameplay *oldPlay = game->gameplays[index];
  if (oldPlay == nullptr)
    return false;

  Kotonoha::Gameplay *newPlay = nullptr;

  try {
    TTF_Font *preferredFont = nullptr;
    auto it = game->fonts.find("ConcertOne-Regular");
    if (it != game->fonts.end()) {
      preferredFont = it->second;
    }
    newPlay = new Kotonoha::Gameplay(newPath.c_str(), &context, preferredFont);
  } catch (const std::exception &e) {
    Kotonoha_SetEditorStatus(e.what());
    return false;
  } catch (...) {
    Kotonoha_SetEditorStatus("Failed to recreate gameplay.");
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
    Kotonoha_LoadSceneEditorFromDisk(newPlay);
  }

  return true;
}

static bool Kotonoha_SaveAndReloadReplace(Kotonoha::Kotonoha *game,
                                          Kotonoha_Game &context, int index) {
  Kotonoha::Gameplay *selected = Kotonoha_GetGameplayAt(game, index);
  if (selected == nullptr)
    return false;

  const std::string targetPath =
      (temporaryGameplayActive && temporaryGameplayIndex == index)
          ? originalScriptPathForTemporary
          : selected->scriptPath;

  if (targetPath.empty() ||
      !Kotonoha_SaveTextFile(targetPath, sceneEditorText)) {
    return false;
  }

  editorDirty = false;
  temporaryGameplayActive = false;
  temporaryGameplayIndex = -1;
  temporaryScriptPath.clear();
  originalScriptPathForTemporary.clear();

  return Kotonoha_RecreateGameplayAtPath(game, context, index, targetPath);
}

static bool Kotonoha_LoadTemporaryGameplayAt(Kotonoha::Kotonoha *game,
                                             Kotonoha_Game &context,
                                             int index) {
  if (game == nullptr || !Kotonoha_IsValidGameplayIndex(game, index))
    return false;

  Kotonoha::Gameplay *selected = game->gameplays[index];
  if (selected == nullptr)
    return false;

  const std::string baseForTempPath =
      (temporaryGameplayActive && temporaryGameplayIndex == index)
          ? originalScriptPathForTemporary
          : selected->scriptPath;

  std::string tempPath = Kotonoha_GetTemporaryScenePath(baseForTempPath);
  if (tempPath.empty() || !Kotonoha_SaveTextFile(tempPath, sceneEditorText)) {
    return false;
  }

  if (!temporaryGameplayActive) {
    originalScriptPathForTemporary = selected->scriptPath;
  }

  temporaryScriptPath = tempPath;
  temporaryGameplayIndex = index;
  temporaryGameplayActive = true;
  editorDirty = false;

  return Kotonoha_RecreateGameplayAtPath(game, context, index, tempPath);
}

static bool Kotonoha_RestoreOriginalPath(Kotonoha::Kotonoha *game,
                                         Kotonoha_Game &context) {
  if (!temporaryGameplayActive ||
      !Kotonoha_IsValidGameplayIndex(game, temporaryGameplayIndex)) {
    return false;
  }

  const int index = temporaryGameplayIndex;
  const std::string originalPath = originalScriptPathForTemporary;

  temporaryGameplayActive = false;
  temporaryGameplayIndex = -1;
  temporaryScriptPath.clear();
  originalScriptPathForTemporary.clear();

  return Kotonoha_RecreateGameplayAtPath(game, context, index, originalPath);
}

/* ---------------- Renderização de Componentes ImGui ---------------- */

static void Kotonoha_DrawAboutFields() {
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

static void Kotonoha_DrawAboutWindow() {
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
    Kotonoha_DrawAboutFields();
  }
  ImGui::End();
}

static void Kotonoha_DrawSettingsFields(Kotonoha_Config *cfg,
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
        SDL_Rect bounds;
        if (SDL_GetDisplayBounds(displays[currentDisplayIdx], &bounds)) {
          // Checa o estado atual da janela
          const SDL_WindowFlags flags = SDL_GetWindowFlags(context.window);
          const bool isFullscreen = (flags & SDL_WINDOW_FULLSCREEN) != 0;
          const bool isBorderless =
              ((flags & SDL_WINDOW_BORDERLESS) != 0) && !isFullscreen;

          // 1. Sai de qualquer modo fullscreen/borderless para liberar a janela
          if (isFullscreen) {
            SDL_SetWindowFullscreen(context.window, false);
            SDL_SyncWindow(context.window);
          }
          if (isBorderless) {
            SDL_SetWindowBordered(context.window, false);
            SDL_SyncWindow(context.window);
          }

          // 2. Reposiciona a janela no novo monitor
          SDL_SetWindowPosition(context.window, bounds.x + 50, bounds.y + 50);
          SDL_SyncWindow(context.window);

          // 3. Reorganiza e reaplica o modo de exibição no novo monitor
          if (isFullscreen) {
            SDL_SetWindowFullscreen(context.window, true);
          } else if (isBorderless) {
            SDL_SetWindowBordered(context.window, false);
            SDL_SetWindowPosition(context.window, bounds.x, bounds.y);
            SDL_SetWindowSize(context.window, bounds.w, bounds.h);
          }

          cfg->dirtyConfig = true;
        }
      }
      SDL_free(displays);
    }

    /* ---------------- Modo de Tela (Windowed / Exclusive / Borderless)
     * ---------------- */
    const char *fullscreenModes[] = {"Windowed", "Fullscreen Exclusive",
                                     "Borderless Fullscreen"};

    int currentModeIdx = 0;
    const SDL_WindowFlags flags = SDL_GetWindowFlags(context.window);
    if ((flags & SDL_WINDOW_FULLSCREEN) != 0) {
      currentModeIdx = 1;
    } else if ((flags & SDL_WINDOW_BORDERLESS) != 0) {
      currentModeIdx = 2;
    }

    if (ImGui::Combo("Screen Mode", &currentModeIdx, fullscreenModes,
                     IM_ARRAYSIZE(fullscreenModes))) {
      cfg->dirtyConfig = true;
      switch (currentModeIdx) {
      case 0: // Windowed
        cfg->fullScreen = false;
        SDL_SetWindowFullscreen(context.window, false);
        SDL_SetWindowBordered(context.window, true);
        SDL_SetWindowSize(context.window, cfg->windowSize[0],
                          cfg->windowSize[1]);
        break;

      case 1: // Fullscreen Exclusive
        cfg->fullScreen = true;
        SDL_SetWindowBordered(context.window, true);
        SDL_SetWindowFullscreen(context.window, true);
        break;

      case 2: // Borderless Fullscreen (Sem bordas)
        cfg->fullScreen = false;
        SDL_SetWindowFullscreen(context.window, false);
        SDL_SetWindowBordered(context.window, false);

        SDL_DisplayID currentDisplay = SDL_GetDisplayForWindow(context.window);
        SDL_Rect bounds;
        if (SDL_GetDisplayBounds(currentDisplay, &bounds)) {
          SDL_SetWindowPosition(context.window, bounds.x, bounds.y);
          SDL_SetWindowSize(context.window, bounds.w, bounds.h);
        }
        break;
      }
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
    ImGui::SetNextItemWidth(100);
    if (ImGui::InputInt("W##res_w", &cfg->windowSize[0]))
      cfg->dirtyConfig = true;
    ImGui::SameLine();
    ImGui::SetNextItemWidth(100);
    if (ImGui::InputInt("H##res_h", &cfg->windowSize[1]))
      cfg->dirtyConfig = true;

    ImGui::SameLine();
    if (ImGui::Button("Apply")) {
      SDL_SetWindowSize(context.window, cfg->windowSize[0], cfg->windowSize[1]);
    }

    if (ImGui::SliderFloat("Scaler Factor", &cfg->scalerFactor, 0.5f, 4.0f,
                           "%.2fx")) {
      cfg->dirtyConfig = true;
    }
    if (ImGui::Checkbox("Enable Upscaling", &cfg->upscalingEnabled)) {
      KotonohaUpscalingSetEnabled(cfg->upscalingEnabled);
      cfg->dirtyConfig = true;
    }
    static std::string anime4kBuf = cfg->anime4kFiles ? cfg->anime4kFiles : "";
    if (ImGui::InputText("Anime4K Files (;)", &anime4kBuf)) {
      if (cfg->anime4kFiles)
        SDL_free(cfg->anime4kFiles);
      cfg->anime4kFiles = SDL_strdup(anime4kBuf.c_str());
      KotonohaAnime4KSetFiles(cfg->anime4kFiles);
      cfg->dirtyConfig = true;
    }
    ImGui::TreePop();
  }

  if (ImGui::TreeNodeEx("Performance & Overlay",
                        ImGuiTreeNodeFlags_DefaultOpen)) {
    if (ImGui::Checkbox("Show FPS Counter", &cfg->showFps))
      cfg->dirtyConfig = true;
    if (ImGui::Checkbox("Show Timestamps", &cfg->showTimestamp))
      cfg->dirtyConfig = true;
    if (ImGui::SliderFloat("Video Cache (s)", &cfg->videoCacheSeconds, 1.0f,
                           15.0f, "%.1f sec")) {
      cfg->dirtyConfig = true;
    }
    ImGui::TreePop();
  }

  if (ImGui::TreeNodeEx("Debug UI", ImGuiTreeNodeFlags_DefaultOpen)) {
    if (ImGui::SliderFloat("UI Scale", &cfg->debugUiScale, 0.5f, 3.0f,
                           "%.2fx")) {
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

static void Kotonoha_DrawSettingsWindow(Kotonoha_Game &context) {
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
    Kotonoha_DrawSettingsFields(&context.config, context);
  }
  ImGui::End();
}

static void Kotonoha_DrawPlayback(Kotonoha::Kotonoha *game,
                                  Kotonoha::Gameplay *play,
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
      Kotonoha_FormatTime(static_cast<Uint64>(newTime * 1000.0f)).c_str(),
      Kotonoha_FormatTime(static_cast<Uint64>(lastTime * 1000.0f)).c_str());

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
  ImGui::Checkbox("Pause", &context.config.paused);
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
    Kotonoha_GoToScene(game, context,
                       static_cast<int>(game->gameplays.size()) - 1);
    context.config.next = true;
  }
}

static void Kotonoha_DrawAudio(Kotonoha_Game &context) {
  if (!ImGui::CollapsingHeader("Audio Mixer", ImGuiTreeNodeFlags_DefaultOpen))
    return;

  auto *sound = static_cast<Kotonoha::Sound *>(context.sound);
  if (sound == nullptr) {
    ImGui::TextUnformatted("Sound system unavailable.");
    return;
  }

  float masterVolume = sound->volume.load(std::memory_order_relaxed);
  if (ImGui::SliderFloat("Master Volume", &masterVolume, 0.0f, 1.0f)) {
    sound->volume.store(masterVolume, std::memory_order_relaxed);
  }

  for (size_t i = 0;; ++i) {
    Kotonoha::Sound::Channel *channel = sound->GetChannelByIndex(i);
    if (channel == nullptr)
      break;

    float channelVolume = channel->volume.load(std::memory_order_relaxed);
    if (ImGui::SliderFloat(channel->name.c_str(), &channelVolume, 0.0f, 1.0f)) {
      channel->volume.store(channelVolume, std::memory_order_relaxed);
    }
  }
}

static void Kotonoha_DrawScriptToggles(Kotonoha::Kotonoha *game,
                                       Kotonoha_Game &context) {
  if (!ImGui::CollapsingHeader("Tools & Panels",
                               ImGuiTreeNodeFlags_DefaultOpen))
    return;

  ImGui::Checkbox("Playlist", &show_playlist);
  ImGui::SameLine();
  if (ImGui::Checkbox("Script Editor", &KotonohaBasicGuiEditorShow) &&
      KotonohaBasicGuiEditorShow) {
    Kotonoha_SelectScene(game, static_cast<int>(context.config.scene));
  }
  ImGui::SameLine();
  if (ImGui::Checkbox("Text Editor", &textEditorShow) && textEditorShow) {
    Kotonoha_SelectScene(game, static_cast<int>(context.config.scene));
  }
  ImGui::SameLine();
  ImGui::Checkbox("Settings Window", &settingsWindowShow);
  ImGui::SameLine();
  ImGui::Checkbox("About", &aboutWindowShow);
}

static void Kotonoha_DrawMainWindow(Kotonoha::Kotonoha *game,
                                    Kotonoha::Gameplay *play,
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

  if (ImGui::Begin("Kotonoha Control Panel", &context.config.enableDebugUi)) {
    ImGui::TextColored(ImVec4(0.80f, 0.60f, 0.95f, 1.0f),
                       "Current Scene: %d / %d",
                       static_cast<int>(context.config.scene) + 1,
                       static_cast<int>(game->gameplays.size()));

    ImGui::TextWrapped("File: %s", play->scriptPath.empty()
                                       ? "<no file path>"
                                       : play->scriptPath.c_str());
    ImGui::Separator();

    Kotonoha_DrawPlayback(game, play, context);
    Kotonoha_DrawAudio(context);

    if (ImGui::CollapsingHeader("Engine Settings")) {
      Kotonoha_DrawSettingsFields(&context.config, context);
    }

    if (ImGui::CollapsingHeader("About Kotonoha")) {
      Kotonoha_DrawAboutFields();
    }

    Kotonoha_DrawScriptToggles(game, context);
  }
  ImGui::End();
}

static void Kotonoha_DrawOpenWindow(Kotonoha::Kotonoha *game,
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
        showError = !Kotonoha_OpenPaths(game, openBuf, false);
        selectedSceneIndex = -1;
        editorSceneIndex = -1;
        sceneEditorText.clear();
        editorDirty = false;

        temporaryGameplayActive = false;
        temporaryGameplayIndex = -1;

        if (!game->gameplays.empty()) {
          context.config.scene = 0;
          Kotonoha_TryAutoLoadCurrentScene(game, context);
        }
      }

      if (ImGui::Button("Append Scenes", ImVec2(-1, 0))) {
        showError = !Kotonoha_OpenPaths(game, openBuf, true);
        if (selectedSceneIndex < 0 && !game->gameplays.empty()) {
          Kotonoha_TryAutoLoadCurrentScene(game, context);
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
          Kotonoha_TryAutoLoadCurrentScene(game, context);
        }

        ImGui::Checkbox("Playlist", &show_playlist);
        ImGui::SameLine();
        ImGui::Checkbox("Script Editor", &KotonohaBasicGuiEditorShow);
        ImGui::SameLine();
        ImGui::Checkbox("Text Editor", &textEditorShow);
        ImGui::SameLine();
        ImGui::Checkbox("Settings Window", &settingsWindowShow);
        ImGui::SameLine();
        ImGui::Checkbox("About", &aboutWindowShow);
      }

      if (ImGui::CollapsingHeader("Engine Settings")) {
        Kotonoha_DrawSettingsFields(&context.config, context);
      }

      if (ImGui::CollapsingHeader("About Kotonoha")) {
        Kotonoha_DrawAboutFields();
      }

      ImGui::Separator();
      if (ImGui::Button("Exit Application", ImVec2(-1, 0))) {
        shouldExit = true;
      }
    }
  }
  ImGui::End();
}

static void Kotonoha_DrawPlaylist(Kotonoha::Kotonoha *game,
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
        showError = !Kotonoha_OpenPaths(game, appendBuf, true);
      }
    }

    ImVec2 listSize = ImGui::GetContentRegionAvail();
    listSize.y = std::max(listSize.y - 10.0f, 120.0f);

    if (ImGui::CollapsingHeader("Scene Queue",
                                ImGuiTreeNodeFlags_DefaultOpen)) {
      if (ImGui::BeginListBox("##scene_playlist", listSize)) {
        for (int i = 0; i < static_cast<int>(game->gameplays.size()); ++i) {
          Kotonoha::Gameplay *item = game->gameplays[i];
          if (item == nullptr)
            continue;

          const std::string label =
              item->scriptPath.empty()
                  ? "Scene " + std::to_string(i)
                  : std::to_string(i) + " - " + item->scriptPath;

          const bool selected =
              (context.config.scene == static_cast<size_t>(i));
          if (ImGui::Selectable(label.c_str(), selected)) {
            Kotonoha_GoToScene(game, context, i);
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

static bool Kotonoha_DrawScriptEditor(Kotonoha::Kotonoha *game,
                                      Kotonoha_Game &context) {
  if (!KotonohaBasicGuiEditorShow)
    return true;

  if (context.config.scene != static_cast<size_t>(editorSceneIndex) ||
      editorSceneIndex < 0) {
    Kotonoha_SelectScene(game, static_cast<int>(context.config.scene));
  }

  bool shouldFinishFrame = false;

  const ImGuiViewport *viewport = ImGui::GetMainViewport();
  ImGui::SetNextWindowSizeConstraints(
      ImVec2(400, 300),
      ImVec2(viewport->WorkSize.x * 0.95f, viewport->WorkSize.y * 0.95f));

  if (ImGui::Begin("Script Editor", &KotonohaBasicGuiEditorShow)) {
    if (selectedSceneIndex >= 0 &&
        selectedSceneIndex < static_cast<int>(game->gameplays.size())) {
      Kotonoha::Gameplay *selected = game->gameplays[selectedSceneIndex];

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
            Kotonoha_LoadSceneEditorFromDisk(selected)) {
          Kotonoha_RecreateGameplayAtPath(game, context, selectedSceneIndex,
                                          selected->scriptPath);
        }

        ImVec2 avail = ImGui::GetContentRegionAvail();
        avail.y = std::max(avail.y - 70.0f, 120.0f);

        if (ImGui::InputTextMultiline("##scene_editor", &sceneEditorText,
                                      avail)) {
          editorDirty = true;
          Kotonoha_LoadTextEditor(sceneEditorText);
        }

        if (editorStatusBuf[0] != '\0') {
          ImGui::TextColored(ImVec4(0.80f, 0.60f, 0.95f, 1.0f), "%s",
                             editorStatusBuf);
        }

        ImGui::TextUnformatted(editorDirty ? "* modified" : "saved");
        ImGui::SameLine();

        if (ImGui::Button("Save and Reload") &&
            Kotonoha_SaveAndReloadReplace(game, context, selectedSceneIndex)) {
          shouldFinishFrame = true;
        }

        ImGui::SameLine();
        if (ImGui::Button("Save Temporary") &&
            Kotonoha_LoadTemporaryGameplayAt(game, context,
                                             selectedSceneIndex)) {
          shouldFinishFrame = true;
        }

        if (temporaryGameplayActive &&
            temporaryGameplayIndex == selectedSceneIndex) {
          ImGui::SameLine();
          if (ImGui::Button("Revert to Original") &&
              Kotonoha_RestoreOriginalPath(game, context)) {
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

static void Kotonoha_DrawTextEventList() {
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
      Kotonoha_SyncTextEditorToScript();
      editorDirty = true;
    }
  }
  ImGui::EndChild();
}

static bool Kotonoha_DrawTextEditor(Kotonoha::Kotonoha *game,
                                    Kotonoha_Game &context) {
  if (!textEditorShow)
    return true;

  if (context.config.scene != static_cast<size_t>(editorSceneIndex) ||
      editorSceneIndex < 0) {
    Kotonoha_SelectScene(game, static_cast<int>(context.config.scene));
  } else if (sceneEditorText != textEditorSyncedScript) {
    Kotonoha_LoadTextEditor(sceneEditorText);
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
      Kotonoha::Gameplay *selected = game->gameplays[selectedSceneIndex];

      ImGui::TextColored(ImVec4(0.80f, 0.60f, 0.95f, 1.0f), "Scene %d",
                         selectedSceneIndex + 1);
      ImGui::TextWrapped("File: %s", selected->scriptPath.empty()
                                         ? "<No path to file>"
                                         : selected->scriptPath.c_str());
      ImGui::Separator();

      Kotonoha_DrawTextEventList();

      if (ImGui::Button("Save and Reload")) {
        if (Kotonoha_SaveAndReloadReplace(game, context, selectedSceneIndex)) {
          shouldFinishFrame = true;
        }
      }

      ImGui::SameLine();
      if (ImGui::Button("Save Temporary")) {
        if (Kotonoha_LoadTemporaryGameplayAt(game, context,
                                             selectedSceneIndex)) {
          shouldFinishFrame = true;
        }
      }

      if (temporaryGameplayActive &&
          temporaryGameplayIndex == selectedSceneIndex) {
        ImGui::SameLine();
        if (ImGui::Button("Revert to Original")) {
          if (Kotonoha_RestoreOriginalPath(game, context)) {
            shouldFinishFrame = true;
          }
        }
      }
    }
  }
  ImGui::End();
  return !shouldFinishFrame;
}

static const char *GetFocusedWindowName() {
  ImGuiContext *ctx = ImGui::GetCurrentContext();
  return (ctx && ctx->NavWindow) ? ctx->NavWindow->Name : nullptr;
}

/* ---------------- Configuração do Tema Roxo e Ciclo de Vida ----------------
 */

static void Kotonoha_ApplyPurpleTheme() {
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

void KotonohaBasicGuiInit(Kotonoha_Game &gameContext, std::vector<Uint8> font) {
  ImGuiIO &io = ImGui::GetIO();
  io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
  io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;

  ImFont *mainFont = nullptr;

  if (!font.empty()) {
    const size_t fontDataSize = font.size();
    void *fontData = SDL_malloc(fontDataSize);
    if (fontData != nullptr) {
      SDL_memcpy(fontData, font.data(), fontDataSize);
      mainFont = io.Fonts->AddFontFromMemoryTTF(
          fontData, static_cast<int>(fontDataSize), 16.0f);
    }
  }

  if (mainFont == nullptr) {
    mainFont = io.Fonts->AddFontDefault();
  }

  io.FontDefault = mainFont;

  Kotonoha_ApplyPurpleTheme();
  debugUiBaseStyle = ImGui::GetStyle();
  debugUiBaseStyleReady = true;
  appliedDebugUiScale = 0.0f;

  ImGui_ImplSDL3_InitForSDLRenderer(gameContext.window, gameContext.render);
  ImGui_ImplSDLRenderer3_Init(gameContext.render);
}

bool KotonohaBasicGuiRun(Kotonoha::Kotonoha *game, Kotonoha::Gameplay *play,
                         Kotonoha_Game &context) {
  if (!context.config.enableDebugUi || game == nullptr) {
    return true;
  }

  const float debugUiScale =
      (context.config.debugUiScale >= 0.5f &&
       context.config.debugUiScale <= 3.0f)
          ? context.config.debugUiScale
          : 1.0f;
  if (debugUiBaseStyleReady && appliedDebugUiScale != debugUiScale) {
    ImGuiStyle &style = ImGui::GetStyle();
    style = debugUiBaseStyle;
    style.ScaleAllSizes(debugUiScale);
    style.FontScaleMain = debugUiScale;
    appliedDebugUiScale = debugUiScale;
  }

  Kotonoha_TryAutoLoadCurrentScene(game, context);

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
    Kotonoha_DrawMainWindow(game, play, context);
  } else {
    Kotonoha_DrawOpenWindow(game, context, shouldExit);
  }

  Kotonoha_DrawPlaylist(game, context);
  Kotonoha_DrawSettingsWindow(context);
  Kotonoha_DrawAboutWindow();

  if (!Kotonoha_DrawScriptEditor(game, context)) {
    return Kotonoha_FinishFrameAndReturn(context);
  }

  if (!Kotonoha_DrawTextEditor(game, context)) {
    return Kotonoha_FinishFrameAndReturn(context);
  }

  KotonohaBasicGuiAnyFocus = (GetFocusedWindowName() != nullptr);
  return !shouldExit && Kotonoha_FinishFrameAndReturn(context);
}

void KotonohaBasicGuiEvent(SDL_Event *event) {
  ImGui_ImplSDL3_ProcessEvent(event);
}