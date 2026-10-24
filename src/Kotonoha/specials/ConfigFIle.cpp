#include "imgui.h"
#include "imgui_internal.h"
#include <Kotonoha/parsers/Config.h>
#include <SDL3/SDL.h>
#include <cstdio>
#include <cstring>

namespace Kotonoha::ConfigDetail {

static void *SettingsReadOpen(ImGuiContext *ctx,
                              ImGuiSettingsHandler *handler,
                              const char *name) {
  return (void *)1;
}

// Função totalmente segura que evita dar free em ponteiros estáticos ou
// inválidos
static void SetString(char **target, const char *source) {
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
      // Opcional: só libera se tiver certeza que foi alocado, ou deixe a engine
      // gerenciar no Free Para segurança máxima no SDL, se veio de SDL_strdup,
      // podemos liberar:
      SDL_free(*target);
    }
    *target = newStr;
  }
}

static void SettingsReadLine(ImGuiContext *ctx,
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
  float factor, cache, debugUiScale;
  int full, vsync, fps, ts, ui, skip, upscaling, ival;
  float fval;

  if (SDL_sscanf(line, "WindowSize=%d,%d", &w, &h) == 2) {
    config->windowSize[0] = w;
    config->windowSize[1] = h;
  } else if (SDL_sscanf(line, "ScalerFactor=%f", &factor) == 1) {
    config->scalerFactor = factor;
  } else if (SDL_sscanf(line, "FullScreen=%d", &full) == 1) {
    config->fullScreen = (bool)full;
  } else if (SDL_sscanf(line, "EnableVsync=%d", &vsync) == 1) {
    config->enableVsync = (bool)vsync;
  } else if (SDL_sscanf(line, "MediaCacheSeconds=%f", &cache) == 1 ||
             SDL_sscanf(line, "VideoCacheSeconds=%f", &cache) == 1) {
    config->mediaCacheSeconds = cache;
  } else if (SDL_sscanf(line, "DebugUiScale=%f", &debugUiScale) == 1) {
    if (debugUiScale >= 0.5f && debugUiScale <= 3.0f) {
      config->debugUiScale = debugUiScale;
    }
  } else if (SDL_sscanf(line, "ShowFps=%d", &fps) == 1) {
    config->showFps = (bool)fps;
  } else if (SDL_sscanf(line, "ShowTimestamp=%d", &ts) == 1) {
    config->showTimestamp = (bool)ts;
  } else if (SDL_sscanf(line, "EnableDebugUi=%d", &ui) == 1) {
    config->enableDebugUi = (bool)ui;
  } else if (SDL_sscanf(line, "SkipFiles=%d", &skip) == 1) {
    config->skipFiles = (bool)skip;
  } else if (SDL_sscanf(line, "UpscalingEnabled=%d", &upscaling) == 1) {
    config->upscalingEnabled = (bool)upscaling;
  } else if (SDL_sscanf(line, "SplashSeconds=%f", &fval) == 1) {
    if (fval > 0.0f)
      config->splashSeconds = fval;
  } else if (SDL_sscanf(line, "SplashFadeSeconds=%f", &fval) == 1) {
    if (fval >= 0.0f)
      config->splashFadeSeconds = fval;
  } else if (SDL_sscanf(line, "PrefetchHorizonMs=%d", &ival) == 1) {
    if (ival >= 0)
      config->prefetchHorizonMs = ival;
  } else if (SDL_sscanf(line, "SeekBigMs=%d", &ival) == 1) {
    if (ival >= 0)
      config->seekBigMs = ival;
  } else if (SDL_sscanf(line, "SeekSmallMs=%d", &ival) == 1) {
    if (ival >= 0)
      config->seekSmallMs = ival;
  } else if (SDL_sscanf(line, "LoadingDebounceMs=%d", &ival) == 1) {
    if (ival >= 0)
      config->loadingDebounceMs = ival;
  } else if (SDL_sscanf(line, "MinLoadingPauseMs=%d", &ival) == 1) {
    if (ival >= 0)
      config->minLoadingPauseMs = ival;
  } else if (strncmp(line, "TitleMusic=", 11) == 0) {
    SetString(&config->titleMusic, line + 11);
  } else if (strncmp(line, "CursorPath=", 11) == 0) {
    SetString(&config->cursorPath, line + 11);
  } else if (SDL_sscanf(line, "CursorSize=%d", &ival) == 1) {
    if (ival > 0)
      config->cursorSize = ival;
  } else if (strncmp(line, "IconPath=", 9) == 0) {
    SetString(&config->iconPath, line + 9);
  } else if (strncmp(line, "FontsPath=", 10) == 0) {
    SetString(&config->fontsPath, line + 10);
  } else if (strncmp(line, "PromptSelectSound=", 18) == 0) {
    SetString(&config->promptSelectSound, line + 18);
  } else if (strncmp(line, "PromptViewSound=", 16) == 0) {
    SetString(&config->promptViewSound, line + 16);
  } else if (strncmp(line, "Splash=", 7) == 0) {
    SetString(&config->splashFiles, line + 7);
  } else if (strncmp(line, "MpvUpscalerFiles=", 13) == 0) {
    SetString(&config->mpvUpscalerFiles, line + 13);
  } else if (strncmp(line, "AssetsPath=", 11) == 0) {
    SetString(&config->assetsPath, line + 11);
  } else if (strncmp(line, "AssStyles=", 10) == 0) {
    SetString(&config->assStyles, line + 10);
  } else if (strncmp(line, "PreferredRenderer=", 18) == 0) {
    SetString(&config->preferredRenderer, line + 18);
  }
}

static void SettingsWriteAll(ImGuiContext *ctx,
                             ImGuiSettingsHandler *handler,
                             ImGuiTextBuffer *buf) {
  Kotonoha_Config *config = (Kotonoha_Config *)handler->UserData;
  if (config == NULL)
    return;

  buf->appendf("[KotonohaConfig][Settings]\n");
  buf->appendf("WindowSize=%d,%d\n", config->windowSize[0].load(),
               config->windowSize[1].load());
  buf->appendf("ScalerFactor=%.3f\n", config->scalerFactor.load());
  buf->appendf("FullScreen=%d\n", config->fullScreen ? 1 : 0);
  buf->appendf("EnableVsync=%d\n", config->enableVsync ? 1 : 0);
  buf->appendf("MediaCacheSeconds=%.2f\n", config->mediaCacheSeconds.load());
  buf->appendf("DebugUiScale=%.2f\n", config->debugUiScale.load());
  buf->appendf("ShowFps=%d\n", config->showFps ? 1 : 0);
  buf->appendf("ShowTimestamp=%d\n", config->showTimestamp ? 1 : 0);
  buf->appendf("EnableDebugUi=%d\n", config->enableDebugUi ? 1 : 0);
  buf->appendf("SkipFiles=%d\n", config->skipFiles ? 1 : 0);
  buf->appendf("UpscalingEnabled=%d\n", config->upscalingEnabled ? 1 : 0);
  buf->appendf("SplashSeconds=%.2f\n", config->splashSeconds.load());
  buf->appendf("SplashFadeSeconds=%.2f\n", config->splashFadeSeconds.load());
  buf->appendf("PrefetchHorizonMs=%d\n", config->prefetchHorizonMs.load());
  buf->appendf("SeekBigMs=%d\n", config->seekBigMs.load());
  buf->appendf("SeekSmallMs=%d\n", config->seekSmallMs.load());
  buf->appendf("LoadingDebounceMs=%d\n", config->loadingDebounceMs.load());
  buf->appendf("MinLoadingPauseMs=%d\n", config->minLoadingPauseMs.load());
  if (config->titleMusic != NULL) {
    buf->appendf("TitleMusic=%s\n", config->titleMusic);
  }
  if (config->cursorPath != NULL) {
    buf->appendf("CursorPath=%s\n", config->cursorPath);
  }
  buf->appendf("CursorSize=%d\n", config->cursorSize.load());
  if (config->iconPath != NULL) {
    buf->appendf("IconPath=%s\n", config->iconPath);
  }
  if (config->fontsPath != NULL) {
    buf->appendf("FontsPath=%s\n", config->fontsPath);
  }
  if (config->promptSelectSound != NULL) {
    buf->appendf("PromptSelectSound=%s\n", config->promptSelectSound);
  }
  if (config->promptViewSound != NULL) {
    buf->appendf("PromptViewSound=%s\n", config->promptViewSound);
  }
  if (config->splashFiles != NULL) {
    buf->appendf("Splash=%s\n", config->splashFiles);
  }
  if (config->mpvUpscalerFiles != NULL) {
    buf->appendf("MpvUpscalerFiles=%s\n", config->mpvUpscalerFiles);
  }

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

} // namespace Kotonoha::ConfigDetail

extern "C" {

bool KotonohaConfigRegisterImGuiHandler(Kotonoha_Config *config) {
  if (config == nullptr) {
    SDL_SetError("Kotonoha config handler requires a config object");
    return false;
  }
  ImGuiContext *ctx = ImGui::GetCurrentContext();
  if (ctx == nullptr) {
    SDL_SetError("Create an ImGui context before loading Kotonoha settings");
    return false;
  }
  ImGuiSettingsHandler *existing =
      ImGui::FindSettingsHandler("KotonohaConfig");
  if (existing) {
    existing->UserData = config;
    return true;
  }

  ImGuiSettingsHandler handler;
  memset(&handler, 0, sizeof(handler));
  handler.TypeName = "KotonohaConfig";
  handler.TypeHash = ImHashStr("KotonohaConfig");
  handler.ReadOpenFn = Kotonoha::ConfigDetail::SettingsReadOpen;
  handler.ReadLineFn = Kotonoha::ConfigDetail::SettingsReadLine;
  handler.WriteAllFn = Kotonoha::ConfigDetail::SettingsWriteAll;
  handler.UserData = config;

  ImGui::AddSettingsHandler(&handler);
  return true;
}

void KotonohaConfigSetIniFilename(const char *path) {
  if (ImGui::GetCurrentContext() == nullptr) {
    SDL_SetError("Create an ImGui context before setting the Kotonoha INI path");
    return;
  }
  ImGuiIO &io = ImGui::GetIO();
  io.IniFilename = path;
}

void KotonohaConfigApplyAndSave(Kotonoha_Config *config) {
  ImGuiContext *ctx = ImGui::GetCurrentContext();
  if (ctx == nullptr) {
    SDL_SetError("Create an ImGui context before saving Kotonoha settings");
    return;
  }
  ImGuiSettingsHandler *handler =
      ImGui::FindSettingsHandler("KotonohaConfig");
  if (handler) {
    handler->UserData = config;
  }

  ImGuiIO &io = ImGui::GetIO();
  if (io.IniFilename != NULL) {
    ImGui::MarkIniSettingsDirty();
    ImGui::SaveIniSettingsToDisk(io.IniFilename);
  }
}

void KotonohaConfigLoadFromDisk(const char *path) {
  if (ImGui::GetCurrentContext() == nullptr) {
    SDL_SetError("Create an ImGui context before loading Kotonoha settings");
    return;
  }
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