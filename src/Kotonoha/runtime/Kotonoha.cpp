#include <Kotonoha/Kotonoha.hpp>
#include <Kotonoha/debug/DebugUi.hpp>
#include <Kotonoha/utils/Log.h>
#include <cstring>
#include <exception>
#include <string>
#include <vector>

extern "C" {
#include <Kotonoha/specials/Upscaling.h>
}

namespace Kotonoha {
namespace {
constexpr const char *kDefaultSplashFiles = "assets/SPLASH.PNG";
constexpr const char *kDefaultTitleMusic = "assets/SDBGM28_LOOP.OGG";
} // namespace

Kotonoha::Kotonoha(SDL_AppResult *status, struct Kotonoha_Game &context)
    : gameContext(context) {
  if (status == nullptr) {
    return;
  }

  *status = SDL_APP_CONTINUE;

  Kotonoha_Config &config = context.config;
  Kotonoha_SetMenuVoiceEnabled(&gameContext, true);
  Kotonoha_SetMessageLayerEnabled(&gameContext, true);

  gameContext.taskLock = SDL_CreateMutex();

  if (gameContext.taskLock == nullptr) {
    KLOG_ERROR(CORE, "Failed to create mutex: %s", SDL_GetError());

    *status = SDL_APP_FAILURE;
    return;
  }

  processPoolRunning = true;

  processPool = SDL_CreateThread(EventsThread, "KotonohaEvents", this);

  if (processPool == nullptr) {
    KLOG_ERROR(CORE, "Failed to create worker thread: %s", SDL_GetError());

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

  std::vector<std::string> splashFiles;
  const std::string splashList =
      config.splashFiles != nullptr ? config.splashFiles : kDefaultSplashFiles;
  size_t pos = 0;
  while (pos <= splashList.size()) {
    size_t end = splashList.find(':', pos);
    if (end == std::string::npos) {
      end = splashList.size();
    }
    std::string file = splashList.substr(pos, end - pos);
    pos = end + 1;
    const size_t first = file.find_first_not_of(" \t");
    const size_t last = file.find_last_not_of(" \t");
    if (first == std::string::npos) {
      continue;
    }
    splashFiles.push_back(file.substr(first, last - first + 1));
  }
  LoadSplashImages(splashFiles);
}

bool Kotonoha::LoadSplashImages(const std::vector<std::string> &references,
                                bool append) {
  std::vector<SDL_Texture *> loaded;
  for (const std::string &file : references) {
    SDL_Texture *texture =
        KotonohaImageCreateTexture(gameContext.render, file.c_str(), -1, -1);
    if (texture == nullptr) {
      KLOG_WARN(CORE, "Splash image '%s' unavailable: %s", file.c_str(),
                SDL_GetError());
      continue;
    }

    SDL_SetTextureBlendMode(texture, SDL_BLENDMODE_BLEND);
    loaded.push_back(texture);
  }
  if (loaded.empty()) {
    return false;
  }

  if (append) {
    splashTextures.insert(splashTextures.end(), loaded.begin(), loaded.end());
  } else {
    for (SDL_Texture *texture : splashTextures) {
      SDL_DestroyTexture(texture);
    }
    splashTextures = std::move(loaded);
    splashIndex = 0;
    splashStartTicks = 0;
  }
  splashActive = true;
  KLOG_INFO(CORE, "Showing %zu splash image(s), %.1f s each",
            splashTextures.size(), gameContext.config.splashSeconds.load());
  return true;
}

void Kotonoha::UpdateTitleMusic(bool titleScreen) {
  Sound *sound = static_cast<Sound *>(gameContext.sound);
  if (!titleScreen) {
    if (titleMedia != nullptr && titleAudio != nullptr) {
      titleAudio->RemoveMedia(titleMedia);
      titleMedia = nullptr;
      KLOG_DEBUG(AUDIO, "Title music stopped");
    }
    return;
  }
  if (titleMedia != nullptr || sound == nullptr || BGM == nullptr) {
    return;
  }
  if (titleAudio == nullptr) {
    titleDemuxer = KotonohaDemuxerCreate(false, &gameContext.config);
    titleTime = KotonohaTimeNew(true);
    titleAudio = std::make_unique<Audio>(sound, titleTime, titleDemuxer);
    KotonohaDemuxerStart(titleDemuxer);
  }
  // One-shot (0/0) looping BGM: plays regardless of the timeline
  titleMedia = titleAudio->AddMedia(gameContext.config.titleMusic != nullptr
                                        ? gameContext.config.titleMusic
                                        : kDefaultTitleMusic,
                                    0, 0, true, "BGM");
  if (titleMedia == nullptr) {
    // Avoid retrying every frame
    KLOG_WARN(AUDIO, "Title music unavailable");
    titleMedia = reinterpret_cast<Kotonoha_AudioDecode *>(this);
  } else {
    KLOG_INFO(AUDIO, "Title music started");
  }
}

void Kotonoha::FinishSplash() {
  splashActive = false;
  for (SDL_Texture *texture : splashTextures) {
    SDL_DestroyTexture(texture);
  }
  splashTextures.clear();
  KLOG_INFO(CORE, "Splash finished");
}

void Kotonoha::RenderSplash() {
  if (!splashActive) {
    return;
  }
  if (splashStartTicks == 0) {
    splashStartTicks = SDL_GetTicks();
  }
  float elapsed =
      static_cast<float>(SDL_GetTicks() - splashStartTicks) / 1000.0f;
  const float splashSeconds = gameContext.config.splashSeconds;
  const float fadeSeconds = gameContext.config.splashFadeSeconds;
  while (elapsed >= splashSeconds) {
    ++splashIndex;
    splashStartTicks = SDL_GetTicks();
    elapsed = 0.0f;
    if (splashIndex >= splashTextures.size()) {
      FinishSplash();
      return;
    }
  }
  SDL_Texture *splashTexture = splashTextures[splashIndex];

  float alpha = 1.0f;
  if (fadeSeconds > 0.0f && elapsed < fadeSeconds) {
    alpha = elapsed / fadeSeconds;
  } else if (fadeSeconds > 0.0f && elapsed > splashSeconds - fadeSeconds) {
    alpha = (splashSeconds - elapsed) / fadeSeconds;
  }
  alpha = SDL_clamp(alpha, 0.0f, 1.0f);

  int outW = 0, outH = 0;
  if (!SDL_GetCurrentRenderOutputSize(gameContext.render, &outW, &outH) ||
      outW <= 0 || outH <= 0) {
    return;
  }
  float texW = 0.0f, texH = 0.0f;
  SDL_GetTextureSize(splashTexture, &texW, &texH);
  // Splash artwork is authored as a presentation frame, not a thumbnail.
  // Fit it against the full render output while preserving aspect ratio.
  // Matching-aspect images (for example 1280x720 in a 16:9 output) therefore
  // fill the entire viewport; other aspect ratios remain centered with the
  // renderer's black clear visible as letter/pillar-boxing.
  const float scale = SDL_min(static_cast<float>(outW) / texW,
                              static_cast<float>(outH) / texH);
  const SDL_FRect dst{(static_cast<float>(outW) - texW * scale) / 2.0f,
                      (static_cast<float>(outH) - texH * scale) / 2.0f,
                      texW * scale, texH * scale};
  SDL_SetTextureAlphaMod(splashTexture, static_cast<Uint8>(alpha * 255.0f));
  SDL_RenderTexture(gameContext.render, splashTexture, nullptr, &dst);
}

Kotonoha::~Kotonoha() {
  for (auto *gameplay : gameplays) {
    if (gameplay != nullptr) gameplay->RequestShutdown();
  }
  processPoolRunning = false;
  FinishSplash();

  if (processPool != nullptr) {
    SDL_WaitThread(processPool, nullptr);
    processPool = nullptr;
  }

  ClearGameplays();

  titleAudio.reset();
  if (titleDemuxer != nullptr) {
    KotonohaDemuxerDestroy(&titleDemuxer);
  }
  if (titleTime != nullptr) {
    KotonohaTimeDestroy(titleTime);
    titleTime = nullptr;
  }

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

int SDLCALL Kotonoha::EventsThread(void *data) {
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
      int result = -1;
      try {
        result = std::get<0>(*it)(std::get<1>(*it));
      } catch (const std::exception &error) {
        KLOG_ERROR(CORE, "Background gameplay task threw: %s", error.what());
      } catch (...) {
        KLOG_ERROR(CORE, "Background gameplay task threw an unknown exception");
      }
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

  gameplay->RequestShutdown();

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

  Kotonoha_Config *config = &gameContext.config;

  if (splashActive && (event->type == SDL_EVENT_KEY_DOWN ||
                       event->type == SDL_EVENT_MOUSE_BUTTON_DOWN ||
                       event->type == SDL_EVENT_FINGER_DOWN)) {
    FinishSplash();
    return SDL_APP_CONTINUE;
  }

  switch (event->type) {
  case SDL_EVENT_KEY_DOWN:
    switch (event->key.key) {
    case SDLK_AC_BACK:
      return SDL_APP_SUCCESS;

    case SDLK_F11:
      if (!event->key.repeat) {
        DisplayMode displayMode;
        if (GetDisplayMode(gameContext, &displayMode) &&
            !SetDisplay(gameContext,
                        displayMode == DisplayMode::Windowed
                            ? DisplayMode::BorderlessFullscreen
                            : DisplayMode::Windowed)) {
          KLOG_ERROR(CORE, "Failed to toggle fullscreen: %s", SDL_GetError());
        }
      }
      break;

    case SDLK_F10:
      config->showFps = !config->showFps;
      break;

    case SDLK_F9: {
      int vsyncState = 0;
      if (!SDL_GetRenderVSync(gameContext.render, &vsyncState)) {
        KLOG_ERROR(CORE, "Failed to get VSync state: %s", SDL_GetError());
        break;
      }
      config->enableVsync = vsyncState;

      if (!SDL_SetRenderVSync(gameContext.render,
                              gameContext.config.enableVsync ? 0 : 1)) {
        KLOG_ERROR(CORE, "Failed to toggle VSync: %s", SDL_GetError());
      }

      if (SDL_GetRenderVSync(gameContext.render, &vsyncState)) {
        config->enableVsync = vsyncState;
      }
      break;
    }

    case SDLK_F8:
      config->showTimestamp = !config->showTimestamp;
      break;

    case SDLK_F7:
      if (!event->key.repeat) {
        config->enableDebugUi = !config->enableDebugUi;
      }
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
  Kotonoha_Config *config = &gameContext.config;

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

Gameplay *Kotonoha::CreateGameplay(const char *path, TTF_Font *preferredFont) {
  if (path == nullptr || *path == '\0' ||
      gameplayEventRuntimeFactory.create == nullptr) {
    return nullptr;
  }
  try {
    auto *gameplay = new Gameplay(path, &gameContext, preferredFont,
                                  gameplayEventRuntimeFactory);
    if (!gameplay->HasEventRuntime()) {
      delete gameplay;
      return nullptr;
    }
    return gameplay;
  } catch (...) {
    return nullptr;
  }
}

bool Kotonoha::LoadScriptFile(const char *path) {
  if (path == nullptr || *path == '\0') {
    return false;
  }

  // Resolve o caminho absoluto do arquivo ou pasta
  char *absolutePath = KotonohaPathGetAbsolute(path);
  if (absolutePath == nullptr) {
    KLOG_ERROR(CORE, "Failed to resolve absolute path for: %s", path);
    return false;
  }

  const std::string scriptPath(absolutePath);
  SDL_free(absolutePath);

  SDL_PathInfo info;
  if (!SDL_GetPathInfo(scriptPath.c_str(), &info)) {
    KLOG_WARN(CORE, "Script path not found: %s", scriptPath.c_str());
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
      KLOG_ERROR(CORE, "Failed to enumerate scripts directory: %s",
                 scriptPath.c_str());
      return false;
    }

    // Ordena os caminhos em ordem alfabética para garantir ordem consistente de
    // carregamento
    std::sort(files.begin(), files.end());

    for (const std::string &file : files) {
      auto *gameplay = CreateGameplay(file.c_str(), preferredFont);
      if (gameplay == nullptr) {
        KLOG_ERROR(CORE, "Failed to allocate Gameplay for script: %s",
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
    auto *gameplay = CreateGameplay(scriptPath.c_str(), preferredFont);
    if (gameplay == nullptr) {
      KLOG_ERROR(CORE, "Failed to allocate Gameplay for script: %s",
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
    KLOG_WARN(CORE,
              "Scene transition capture skipped: render target is not the "
              "backbuffer");
    return false;
  }

  SDL_Surface *surface = SDL_RenderReadPixels(gameContext.render, nullptr);
  if (surface == nullptr) {
    KLOG_ERROR(CORE, "Scene transition frame capture failed: %s",
               SDL_GetError());
    return false;
  }

  SDL_Texture *nextFrame =
      SDL_CreateTextureFromSurface(gameContext.render, surface);
  if (nextFrame == nullptr) {
    KLOG_ERROR(CORE, "Scene transition texture creation failed: %s",
               SDL_GetError());
    SDL_DestroySurface(surface);
    return false;
  }
  SDL_DestroySurface(surface);

  if (!SDL_SetTextureBlendMode(nextFrame, SDL_BLENDMODE_NONE)) {
    KLOG_ERROR(CORE, "Scene transition texture blend setup failed: %s",
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
    KLOG_ERROR(CORE, "Cannot present retained scene frame without a renderer");
    return;
  }
  if (SDL_GetRenderTarget(gameContext.render) != nullptr &&
      !SDL_SetRenderTarget(gameContext.render, nullptr)) {
    KLOG_ERROR(CORE, "Cannot restore backbuffer for retained scene frame: %s",
               SDL_GetError());
    return;
  }
  if (!SDL_RenderTexture(gameContext.render, sceneTransitionFrame, nullptr,
                         nullptr)) {
    KLOG_ERROR(CORE, "Retained scene frame presentation failed: %s",
               SDL_GetError());
  }
}

bool Kotonoha::CaptureLastPresentedFrameForTransition() {
  return CaptureSceneTransitionFrame();
}

SDL_AppResult Kotonoha::Main(Gameplay **out) {
  SDL_AppResult status = SDL_APP_CONTINUE;
  if (out != nullptr) {
    *out = nullptr;
  }

  Kotonoha_Config *config = &gameContext.config;

  while (!gameplays.empty() &&
         static_cast<size_t>(config->scene) < gameplays.size()) {
    const size_t currentScene = static_cast<size_t>(config->scene);
    Gameplay *current = gameplays[currentScene];
    if (current == nullptr) {
      lastScene = config->scene++;
      continue;
    }

    const size_t nextScene = currentScene + 1;
    for (size_t index = 0; index < gameplays.size(); ++index) {
      if (gameplays[index] != nullptr) {
        const bool isCurrentScene = index == currentScene;
        const bool isNextScene = index == nextScene;
        gameplays[index]->SetMediaActive(isCurrentScene || isNextScene,
                                         isNextScene);
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

  UpdateTitleMusic(out == nullptr || *out == nullptr);

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
