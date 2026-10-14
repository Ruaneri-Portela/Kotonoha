#include <Kotonoha/Kotonoha.hpp>
#include <cstring>
#include <string>
#include <vector>

extern "C" {
#include <Kotonoha/speciais/Upscaling.h>
}

extern bool KotonohaBasicGuiEditorShow;

namespace Kotonoha {
Kotonoha::Kotonoha(SDL_AppResult *status, struct Kotonoha_Game &context)
    : gameContext(context) {
  if (status == nullptr) {
    return;
  }

  *status = SDL_APP_CONTINUE;

  struct KotonohaConfig &config = context.config;

  // -i means the user explicitly chose to ignore missing assets.
  KotonohaIOMonitorSetRetryMissingAssets(!config.skipFiles);

  gameContext.taskLock = SDL_CreateMutex();

  if (gameContext.taskLock == nullptr) {
    SDL_LogError(0, "Failed to create mutex: %s", SDL_GetError());

    *status = SDL_APP_FAILURE;
    return;
  }

  processPoolRunning = true;

  processPool = SDL_CreateThreadRuntime(EventsThread, "Events Thread", this,
                                        nullptr, nullptr);

  if (processPool == nullptr) {
    SDL_LogError(0, "Failed to create worker thread: %s", SDL_GetError());

    *status = SDL_APP_FAILURE;
    return;
  }

  Sound *sound = static_cast<Sound *>(gameContext.sound);

  if (sound != nullptr) {
    BGM = sound->CreateChannel(SDL_AUDIO_F32, 2, 48000, false, "BGM", nullptr);

    Se = sound->CreateChannel(SDL_AUDIO_F32, 2, 48000, false, "Se", nullptr);

    Voice =
        sound->CreateChannel(SDL_AUDIO_F32, 2, 48000, false, "Voice", nullptr);
  }

  gameContext.processPoolTasks = &processPoolTasks;
}

Kotonoha::~Kotonoha() {
  for (auto *gameplay : gameplays) {
    if (gameplay != nullptr) {
      KotonohaIOMonitorCancel(&gameplay->ioMonitor);
    }
  }

  processPoolRunning = false;

  if (processPool != nullptr) {
    SDL_WaitThread(processPool, nullptr);
    processPool = nullptr;
  }

  ClearGameplays();

  for (auto &task : processPoolTasks) {
    SDL_free(std::get<1>(task));
  }
  processPoolTasks.clear();

  if (gameContext.taskLock != nullptr) {
    SDL_DestroyMutex(gameContext.taskLock);
    gameContext.taskLock = nullptr;
  }

  if (gameContext.render != nullptr) {
    SDL_DestroyRenderer(gameContext.render);
    gameContext.render = nullptr;
  }

  if (gameContext.window != nullptr) {
    SDL_DestroyWindow(gameContext.window);
    gameContext.window = nullptr;
  }

  for (std::unordered_map<std::string, TTF_Font *>::iterator it = fonts.begin();
       it != fonts.end(); ++it) {
    if (it->second != nullptr) {
      TTF_CloseFont(it->second);
    }
  }

  fonts.clear();
}

int Kotonoha::EventsThread(void *data) {
  Kotonoha *process = static_cast<Kotonoha *>(data);
  if (process == nullptr || process->gameContext.taskLock == nullptr) {
    return -1;
  }

  auto &tasks = process->processPoolTasks;
  Uint64 lastTime = SDL_GetTicks();

  while (process->processPoolRunning) {
    const Uint64 now = SDL_GetTicks();
    const Uint64 elapsed = now - lastTime;

    if (elapsed < 50) {
      SDL_Delay(static_cast<Uint32>(50 - elapsed));
    }
    if (!process->processPoolRunning) {
      break;
    }

    SDL_LockMutex(process->gameContext.taskLock);

    for (auto it = tasks.begin(); it != tasks.end();) {
      const int result = std::get<0>(*it)(std::get<1>(*it));
      if (result == -1) {
        SDL_free(std::get<1>(*it));
        it = tasks.erase(it);
      } else {
        ++it;
      }
    }

    SDL_UnlockMutex(process->gameContext.taskLock);
    lastTime = SDL_GetTicks();
  }

  return 0;
}

void Kotonoha::DeleteGameplay(Gameplay *gameplay) {
  if (gameplay == nullptr) {
    return;
  }

  if (gameContext.taskLock != nullptr) {
    SDL_LockMutex(gameContext.taskLock);

    for (auto it = processPoolTasks.begin(); it != processPoolTasks.end();) {
      void *taskData = std::get<1>(*it);
      bool removeTask = false;

      if (taskData != nullptr) {
        void **parms = static_cast<void **>(taskData);

        if (parms[0] == gameplay) {
          removeTask = true;
        }
      }

      if (removeTask) {
        SDL_free(taskData);
        it = processPoolTasks.erase(it);
      } else {
        ++it;
      }
    }

    SDL_UnlockMutex(gameContext.taskLock);
  }

  delete gameplay;
}

SDL_AppResult Kotonoha::Event(SDL_Event *event) {
  if (event == nullptr) {
    return SDL_APP_CONTINUE;
  }

  KotonohaConfig *config = &gameContext.config;

  switch (event->type) {
  case SDL_EVENT_KEY_DOWN:
    switch (event->key.key) {
    case SDLK_AC_BACK:
      return SDL_APP_SUCCESS;

    case SDLK_F11:

      SDL_SetWindowFullscreen(gameContext.window,
                              !(gameContext.flags & SDL_WINDOW_FULLSCREEN));
      gameContext.flags = SDL_GetWindowFlags(gameContext.window);
      break;

    case SDLK_F10:
      config->showFps = !config->showFps;
      break;

    case SDLK_F9:
      if (!SDL_GetRenderVSync(gameContext.render, &config->enableVsync)) {
        SDL_LogError(0, "Failed to get VSync state: %s", SDL_GetError());
        break;
      }

      if (!SDL_SetRenderVSync(gameContext.render,
                              gameContext.config.enableVsync ? 0 : 1)) {
        SDL_LogError(0, "Failed to toggle VSync: %s", SDL_GetError());
      }

      SDL_GetRenderVSync(gameContext.render, &config->enableVsync);
      break;

    case SDLK_F8:
      config->showTimestamp = !config->showTimestamp;
      break;

    case SDLK_F7:
      config->enableDebugUi = !config->enableDebugUi;
      break;

    default:
      break;
    }
    break;

  case SDL_EVENT_MOUSE_MOTION:
    lastMouseTime = SDL_GetTicks();
    break;

  default:
    break;
  }

  if (config->scene >= 0 &&
      static_cast<size_t>(config->scene) < gameplays.size()) {
    Gameplay *current = gameplays[static_cast<size_t>(config->scene)];
    if (current != nullptr) {
      current->Events(event, &gameContext);
    }
  }
  return SDL_APP_CONTINUE;
}

void Kotonoha::ClearGameplays() {
  KotonohaConfig *config = &gameContext.config;

  for (auto *gameplay : gameplays) {
    DeleteGameplay(gameplay);
  }

  gameplays.clear();
  config->scene = 0;
  config->next = false;
  config->back = false;

  lastScene = static_cast<size_t>(-1);
  sceneTransitionPrepared = false;
  sceneTransitionActive = false;
  lastSceneFrameCaptureTime = 0;
  if (sceneTransitionFrame != nullptr) {
    SDL_DestroyTexture(sceneTransitionFrame);
    sceneTransitionFrame = nullptr;
  }
}

bool Kotonoha::LoadScriptFile(const char *path) {
  if (path == nullptr || *path == '\0') {
    return false;
  }

  // Resolve o caminho absoluto do arquivo ou pasta
  char *absolutePath = KotonohaPathGetAbsolute(path);
  if (absolutePath == nullptr) {
    SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                 "Failed to resolve absolute path for: %s", path);
    return false;
  }

  const std::string scriptPath(absolutePath);
  SDL_free(absolutePath);

  SDL_PathInfo info;
  if (!SDL_GetPathInfo(scriptPath.c_str(), &info)) {
    SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "Script path not found: %s",
                scriptPath.c_str());
    return false;
  }

  // Obtém a fonte preferencial previamente carregada
  TTF_Font *preferredFont = nullptr;
  auto fontIt = fonts.find("ConcertOne-Regular");
  if (fontIt != fonts.end()) {
    preferredFont = fontIt->second;
  }

  bool loadedAny = false;

  // 1. Se for um DIRETÓRIO: varre todos os arquivos contidos nele
  if (info.type == SDL_PATHTYPE_DIRECTORY) {
    struct ScriptScanContext {
      const std::string *directory;
      std::vector<std::string> *files;
    };

    std::vector<std::string> files;
    ScriptScanContext context{&scriptPath, &files};

    if (!SDL_EnumerateDirectory(
            scriptPath.c_str(),
            [](void *userdata, const char *dirname,
               const char *fname) -> SDL_EnumerationResult {
              auto *ctx = static_cast<ScriptScanContext *>(userdata);
              if (ctx == nullptr || ctx->directory == nullptr ||
                  ctx->files == nullptr || fname == nullptr) {
                return SDL_ENUM_CONTINUE;
              }

              // Junta o diretório pai com o nome do arquivo individual
              char *entryPath =
                  KotonohaPathJoin(ctx->directory->c_str(), fname);
              if (entryPath == nullptr) {
                return SDL_ENUM_CONTINUE;
              }

              const std::string managedEntryPath(entryPath);
              SDL_free(entryPath);

              SDL_PathInfo entryInfo;
              if (SDL_GetPathInfo(managedEntryPath.c_str(), &entryInfo) &&
                  entryInfo.type == SDL_PATHTYPE_FILE) {
                ctx->files->emplace_back(managedEntryPath);
              }

              return SDL_ENUM_CONTINUE;
            },
            &context)) {
      SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                   "Failed to enumerate scripts directory: %s",
                   scriptPath.c_str());
      return false;
    }

    // Ordena os caminhos em ordem alfabética para garantir ordem consistente de
    // carregamento
    std::sort(files.begin(), files.end());

    for (const std::string &file : files) {
      auto *gameplay = new (std::nothrow)
          Gameplay(file.c_str(), &gameContext, preferredFont);
      if (gameplay == nullptr) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                     "Failed to allocate Gameplay for script: %s",
                     file.c_str());
        continue;
      }

      gameplays.push_back(gameplay);
      loadedAny = true;
    }

    return loadedAny;
  }

  // 2. Se for um ARQUIVO individual
  if (info.type == SDL_PATHTYPE_FILE) {
    auto *gameplay = new (std::nothrow)
        Gameplay(scriptPath.c_str(), &gameContext, preferredFont);
    if (gameplay == nullptr) {
      SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                   "Failed to allocate Gameplay for script: %s",
                   scriptPath.c_str());
      return false;
    }

    gameplays.push_back(gameplay);
    return true;
  }

  return false;
}

bool Kotonoha::CaptureSceneTransitionFrame(bool activateTransition) {
  if (gameContext.render == nullptr) {
    return false;
  }
  if (SDL_GetRenderTarget(gameContext.render) != nullptr) {
    SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                "Scene transition capture skipped: render target is not the "
                "backbuffer");
    return false;
  }

  SDL_Surface *surface = SDL_RenderReadPixels(gameContext.render, nullptr);
  if (surface == nullptr) {
    SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                 "Scene transition frame capture failed: %s", SDL_GetError());
    return false;
  }

  SDL_Texture *nextFrame =
      SDL_CreateTextureFromSurface(gameContext.render, surface);
  if (nextFrame == nullptr) {
    SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                 "Scene transition texture creation failed: %s",
                 SDL_GetError());
    SDL_DestroySurface(surface);
    return false;
  }
  SDL_DestroySurface(surface);

  if (!SDL_SetTextureBlendMode(nextFrame, SDL_BLENDMODE_NONE)) {
    SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                 "Scene transition texture blend setup failed: %s",
                 SDL_GetError());
    SDL_DestroyTexture(nextFrame);
    return false;
  }

  if (sceneTransitionFrame != nullptr) {
    SDL_DestroyTexture(sceneTransitionFrame);
  }
  sceneTransitionFrame = nextFrame;
  if (activateTransition) {
    sceneTransitionPrepared = true;
    sceneTransitionActive = true;
  }
  return true;
}

void Kotonoha::CacheSceneTransitionFrameIfNeeded(Gameplay *current) {
  if (sceneTransitionActive || sceneTransitionPrepared || current == nullptr ||
      current->eventManager == nullptr || current->drawCanvas == nullptr ||
      !current->drawCanvas->DrewAtOrBelow(1)) {
    return;
  }

  const float remaining = current->GetLastTime() - current->GetTime();
  if (remaining < -0.1f || remaining > 1.0f) {
    return;
  }

  const Uint64 now = SDL_GetTicks();
  if (now - lastSceneFrameCaptureTime < 100) {
    return;
  }
  if (CaptureSceneTransitionFrame(false)) {
    lastSceneFrameCaptureTime = now;
  }
}

void Kotonoha::PresentSceneTransitionFrame(Gameplay *incoming) {
  if (!sceneTransitionActive || sceneTransitionFrame == nullptr) {
    return;
  }

  if (incoming == nullptr &&
      (gameContext.config.scene < 0 ||
       static_cast<size_t>(gameContext.config.scene) >= gameplays.size())) {
    SDL_DestroyTexture(sceneTransitionFrame);
    sceneTransitionFrame = nullptr;
    sceneTransitionPrepared = false;
    sceneTransitionActive = false;
    lastSceneFrameCaptureTime = 0;
    return;
  }

  if (incoming != nullptr && incoming->drawCanvas != nullptr &&
      incoming->drawCanvas->DrewAtOrBelow(1)) {
    SDL_DestroyTexture(sceneTransitionFrame);
    sceneTransitionFrame = nullptr;
    sceneTransitionPrepared = false;
    sceneTransitionActive = false;
    lastSceneFrameCaptureTime = 0;
    return;
  }

  if (gameContext.render == nullptr) {
    SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                 "Cannot present retained scene frame without a renderer");
    return;
  }
  if (SDL_GetRenderTarget(gameContext.render) != nullptr &&
      !SDL_SetRenderTarget(gameContext.render, nullptr)) {
    SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                 "Cannot restore backbuffer for retained scene frame: %s",
                 SDL_GetError());
    return;
  }
  if (!SDL_RenderTexture(gameContext.render, sceneTransitionFrame, nullptr,
                         nullptr)) {
    SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                 "Retained scene frame presentation failed: %s",
                 SDL_GetError());
  }
}

SDL_AppResult Kotonoha::Main(Gameplay **out) {
  SDL_AppResult status = SDL_APP_CONTINUE;
  if (out != nullptr) {
    *out = nullptr;
  }

  KotonohaConfig *config = &gameContext.config;

  while (!gameplays.empty() &&
         static_cast<size_t>(config->scene) < gameplays.size()) {
    const size_t currentScene = static_cast<size_t>(config->scene);
    Gameplay *current = gameplays[currentScene];
    if (current == nullptr) {
      lastScene = config->scene++;
      continue;
    }

    // Parsed scenes remain available for immediate transition, but only the
    // visible scene may schedule media work or start decoders.
    for (size_t index = 0; index < gameplays.size(); ++index) {
      if (gameplays[index] != nullptr) {
        gameplays[index]->SetMediaActive(index == currentScene);
      }
    }

    if (lastScene != currentScene) {
      if (lastScene != static_cast<size_t>(-1) &&
          sceneTransitionFrame != nullptr) {
        sceneTransitionActive = true;
        sceneTransitionPrepared = false;
      }
      lastScene = currentScene;
      if (!current->firstFocus)
        current->Reset(true);
#ifdef KOTONOHA_HAS_METALFX
      KotonohaMetalFXClearScalerCache();
#endif
      continue;
    }

    if (config->back && config->scene > 0) {
      if (!current->firstFocus)
        current->Reset(true);
      lastScene = config->scene--;
      config->back = false;
      continue;
    }

    const SDL_AppResult result = current->Main(&gameContext);
    if (result == SDL_APP_FAILURE) {
      status = result;
      break;
    }
    if ((config->next || config->back) && result == SDL_APP_CONTINUE &&
        !sceneTransitionPrepared) {
      CaptureSceneTransitionFrame();
    }
    if (result != SDL_APP_CONTINUE || config->next) {
      if (!current->firstFocus)
        current->Reset(true);
      config->scene++;
      config->next = false;

      if (static_cast<size_t>(config->scene) < gameplays.size()) {
        continue;
      }

      lastScene = static_cast<size_t>(config->scene);
      break;
    }

    if (out != nullptr) {
      *out = current;
    }
    break;
  }

  const bool sceneChangePending =
      config->next || (config->back && config->scene > 0);
  if (sceneTransitionPrepared && !sceneTransitionActive &&
      !sceneChangePending) {
    if (sceneTransitionFrame != nullptr) {
      SDL_DestroyTexture(sceneTransitionFrame);
      sceneTransitionFrame = nullptr;
    }
    sceneTransitionPrepared = false;
  }

  if (gameContext.config.enableDebugUi ||
      SDL_GetTicks() - lastMouseTime <= 1000) {
    SDL_ShowCursor();
  } else if (!gameContext.config.enableDebugUi &&
             !SDL_GetWindowMouseGrab(gameContext.window)) {
    SDL_HideCursor();
  }

  return status;
}

} // namespace Kotonoha
