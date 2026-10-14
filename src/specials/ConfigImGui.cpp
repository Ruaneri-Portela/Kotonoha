#include "imgui.h"
#include "imgui_internal.h"
#include <Kotonoha/utils/Config.h>
#include <SDL3/SDL.h>
#include <cstdio>
#include <cstring>

extern "C" {

static void *KotonohaSettings_ReadOpen(ImGuiContext *ctx,
                                       ImGuiSettingsHandler *handler,
                                       const char *name) {
  return (void *)1;
}

// Função totalmente segura que evita dar free em ponteiros estáticos ou inválidos
static void ConfigImGui_SetString(char **target, const char *source) {
  if (source == NULL)
    return;

  // Remove espaços iniciais da linha do .ini
  while (*source == ' ' || *source == '\t') {
    source++;
  }

  // Se o conteúdo apontado já for idêntico, não faz nada
  if (*target != NULL && strcmp(*target, source) == 0) {
    return;
  }

  // ATENÇÃO: Se o target atual aponta para o valor padrão de style estático, 
  // não chamamos SDL_free nele para evitar o crash de malloc.
  // Apenas sobrescrevemos com uma nova string alocada via SDL_strdup.
  char *newStr = SDL_strdup(source);
  if (newStr != NULL) {
    if (*target != NULL) {
      // Opcional: só libera se tiver certeza que foi alocado, ou deixe a engine gerenciar no Free
      // Para segurança máxima no SDL, se veio de SDL_strdup, podemos liberar:
      SDL_free(*target);
    }
    *target = newStr;
  }
}

static void KotonohaSettings_ReadLine(ImGuiContext *ctx,
                                      ImGuiSettingsHandler *handler,
                                      void *entry, const char *line) {
  Kotonoha_Config *config = (Kotonoha_Config *)handler->UserData;
  if (config == NULL || line == NULL)
    return;

  // Ignora linhas vazias ou comentários
  while (*line == ' ' || *line == '\t')
    line++;
  if (*line == '\0' || *line == '#' || *line == '[')
    return;

  int w, h;
  float factor, cache;
  int full, vsync, fps, ts, ui, skip;

  if (sscanf(line, "WindowSize=%d,%d", &w, &h) == 2) {
    config->windowSize[0] = w;
    config->windowSize[1] = h;
  } else if (sscanf(line, "ScalerFactor=%f", &factor) == 1) {
    config->scalerFactor = factor;
  } else if (sscanf(line, "FullScreen=%d", &full) == 1) {
    config->fullScreen = (bool)full;
  } else if (sscanf(line, "EnableVsync=%d", &vsync) == 1) {
    config->enableVsync = (bool)vsync;
  } else if (sscanf(line, "VideoCacheSeconds=%f", &cache) == 1) {
    config->videoCacheSeconds = cache;
  } else if (sscanf(line, "ShowFps=%d", &fps) == 1) {
    config->showFps = (bool)fps;
  } else if (sscanf(line, "ShowTimestamp=%d", &ts) == 1) {
    config->showTimestamp = (bool)ts;
  } else if (sscanf(line, "EnableDebugUi=%d", &ui) == 1) {
    config->enableDebugUi = (bool)ui;
  } else if (sscanf(line, "SkipFiles=%d", &skip) == 1) {
    config->skipFiles = (bool)skip;
  } else if (strncmp(line, "AssetsPath=", 11) == 0) {
    ConfigImGui_SetString(&config->assetsPath, line + 11);
  } else if (strncmp(line, "AssStyles=", 10) == 0) {
    ConfigImGui_SetString(&config->assStyles, line + 10);
  } else if (strncmp(line, "PreferredRenderer=", 18) == 0) {
    ConfigImGui_SetString(&config->preferredRenderer, line + 18);
  }
}

static void KotonohaSettings_WriteAll(ImGuiContext *ctx,
                                      ImGuiSettingsHandler *handler,
                                      ImGuiTextBuffer *buf) {
  Kotonoha_Config *config = (Kotonoha_Config *)handler->UserData;
  if (config == NULL)
    return;

  buf->appendf("[KotonohaConfig][Settings]\n");
  buf->appendf("WindowSize=%d,%d\n", config->windowSize[0],
               config->windowSize[1]);
  buf->appendf("ScalerFactor=%.3f\n", config->scalerFactor);
  buf->appendf("FullScreen=%d\n", config->fullScreen ? 1 : 0);
  buf->appendf("EnableVsync=%d\n", config->enableVsync ? 1 : 0);
  buf->appendf("VideoCacheSeconds=%.2f\n", config->videoCacheSeconds);
  buf->appendf("ShowFps=%d\n", config->showFps ? 1 : 0);
  buf->appendf("ShowTimestamp=%d\n", config->showTimestamp ? 1 : 0);
  buf->appendf("EnableDebugUi=%d\n", config->enableDebugUi ? 1 : 0);
  buf->appendf("SkipFiles=%d\n", config->skipFiles ? 1 : 0);

  if (config->assetsPath != NULL) {
    buf->appendf("AssetsPath=%s\n", config->assetsPath);
  }
  if (config->assStyles != NULL) {
    buf->appendf("AssStyles=%s\n", config->assStyles);
  }
  if (config->preferredRenderer != NULL) {
    buf->appendf("PreferredRenderer=%s\n", config->preferredRenderer);
  }

  buf->appendf("\n");
}

void KotonohaConfigRegisterImGuiHandler(Kotonoha_Config *config) {
  ImGuiContext *ctx = ImGui::GetCurrentContext();
  if (ctx) {
    ImGuiSettingsHandler *existing =
        ImGui::FindSettingsHandler("KotonohaConfig");
    if (existing) {
      existing->UserData = config;
      return;
    }
  }

  ImGuiSettingsHandler handler;
  memset(&handler, 0, sizeof(handler));
  handler.TypeName = "KotonohaConfig";
  handler.TypeHash = ImHashStr("KotonohaConfig");
  handler.ReadOpenFn = KotonohaSettings_ReadOpen;
  handler.ReadLineFn = KotonohaSettings_ReadLine;
  handler.WriteAllFn = KotonohaSettings_WriteAll;
  handler.UserData = config;

  ImGui::AddSettingsHandler(&handler);
}

void KotonohaConfigSetIniFilename(const char *path) {
  ImGuiIO &io = ImGui::GetIO();
  io.IniFilename = path;
}

void KotonohaConfigApplyAndSave(Kotonoha_Config *config) {
  ImGuiContext *ctx = ImGui::GetCurrentContext();
  if (ctx) {
    ImGuiSettingsHandler *handler =
        ImGui::FindSettingsHandler("KotonohaConfig");
    if (handler) {
      handler->UserData = config;
    }
  }

  ImGuiIO &io = ImGui::GetIO();
  if (io.IniFilename != NULL) {
    ImGui::MarkIniSettingsDirty();
    ImGui::SaveIniSettingsToDisk(io.IniFilename);
  }
}

void KotonohaConfigLoadFromDisk(const char *path) {
  if (path != NULL) {
    ImGui::LoadIniSettingsFromDisk(path);
  } else {
    ImGuiIO &io = ImGui::GetIO();
    if (io.IniFilename != NULL) {
      ImGui::LoadIniSettingsFromDisk(io.IniFilename);
    }
  }
}

} // extern "C"