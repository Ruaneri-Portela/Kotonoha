#include "Kotonoha/components/Sound.hpp"
#include <Kotonoha/Kotonoha.hpp>
#include <SDL3/SDL_video.h>
#include <cstddef>
#include <fstream>
#include <iostream>
#include <string>

extern bool Kotonoha_BasicGuiShow;

namespace Kotonoha {
// Construtor da classe Kotonoha
Kotonoha::Kotonoha(int argc, char *argv[], SDL_AppResult *status,
                   struct Kotonoha_Game &context)
    : gameContext(context) {
  if (!status)
    return;

  if (!parserArguments(argc, argv, false)) {
    *status = SDL_APP_SUCCESS;
    return;
  }

  *status = SDL_APP_CONTINUE;

  loadWindowIcon("assets/icon.png");

  const char *renderMethods[] = {preferedGPU, "gpu", "software"};
  const size_t numMethods = sizeof(renderMethods) / sizeof(renderMethods[0]);

  // Tenta criar o renderizador com as opções de fallback
  for (size_t i = 0; i < numMethods; i++) {
    if (renderMethods[i] == nullptr)
      continue;
    gameContext.render =
        SDL_CreateRenderer(gameContext.window, renderMethods[i]);
    if (gameContext.render != nullptr) {
      // Caso o renderizador seja criado com sucesso, saímos do loop
      SDL_Log("Renderer created with method '%s'", renderMethods[i]);
      break;
    }

    // Loga a tentativa de criação do renderizador
    SDL_LogError(0, "Failed to create renderer with method '%s': %s",
                 renderMethods[i], SDL_GetError());
  }

  // Verifica se o renderizador foi criado
  if (gameContext.render == nullptr) {
    // Se todos os métodos falharem, registra o erro e retorna a falha
    SDL_LogError(0, "Couldn't create renderer with any method, giving up: %s",
                 SDL_GetError());
    *status = SDL_APP_FAILURE;
    return;
  }

  gameContext.flags |= SDL_GetWindowFlags(gameContext.window);
  SDL_GetRenderVSync(gameContext.render, &gameContext.vsync);
  SDL_SetAppMetadata("A visual novel engine", "0.1", "me.hirameki.kotonoha");

  gameContext.taskLock = SDL_CreateMutex();
  // Cria thread do pool de processos
  processPool = SDL_CreateThreadRuntime(EventsThread, "Events Thread", this,
                                        nullptr, nullptr);
  gameContext.processPoolTasks = &processPoolTasks;

  // Configura o áudio
  Sound *sound = static_cast<Sound *>(gameContext.sound);
  BGM = sound->CreateChannel(SDL_AUDIO_F32, 2, 48000, false, "BGM", nullptr);
  Se = sound->CreateChannel(SDL_AUDIO_F32, 2, 48000, false, "Se", nullptr);
  Voice =
      sound->CreateChannel(SDL_AUDIO_F32, 2, 48000, false, "Voice", nullptr);

  // Set cursor
  SDL_Surface *cursorImg =
      Kotonoha_imageCreateSurface("assets/test_cursor.png", 45, 32);
  cursor = SDL_CreateColorCursor(cursorImg, 0, 0);
  SDL_SetCursor(cursor);
  SDL_DestroySurface(cursorImg);

  // Processa argumentos de linha de comando
  parserArguments(argc, argv, true);
  SDL_ShowWindow(gameContext.window);
}

// Destrutor da classe Kotonoha
Kotonoha::~Kotonoha() {
  for (auto *gameplay : gameplays)
    delete gameplay;

  processPoolRunning = false;
  SDL_WaitThread(processPool, nullptr);
  SDL_DestroyCursor(cursor);

  SDL_DestroyRenderer(gameContext.render);
  SDL_DestroyWindow(gameContext.window);
}

// Função para o processamento em threads do pool
int Kotonoha::EventsThread(void *data) {
  Kotonoha *process = static_cast<Kotonoha *>(data);
  if (!process || !process->gameContext.taskLock)
    return -1;

  auto &tasks = process->processPoolTasks;
  Uint64 lastTime = 0;
  while (process->processPoolRunning) {
    Uint64 actualTime = SDL_GetTicks();
    // 20 Tps
    Uint32 waitTime = static_cast<Uint32>(actualTime - lastTime);
    if (waitTime < 50) {
      SDL_Delay(50 - waitTime);
    }
    SDL_LockMutex(process->gameContext.taskLock);
    if (!tasks.empty()) {
      for (auto it = tasks.begin(); it != tasks.end();) {
        int result = std::get<0>(*it)(std::get<1>(*it));
        if (result == -1) {
          SDL_free(std::get<1>(*it));
          it = tasks.erase(it);
          continue;
        }
        ++it;
      }
    }
    SDL_UnlockMutex(process->gameContext.taskLock);
    lastTime = actualTime;
  }
  return 0;
}

// Função para parsing de argumentos
bool Kotonoha::parserArguments(int argc, char *argv[], bool initDependent) {
  // Função de exibição de ajuda
  auto showHelp = []() {
    SDL_Log("Usage: Kotonoha [options]");
    SDL_Log("Options:");
    SDL_Log("  -l <file>            Load a gameplay file");
    SDL_Log("  -p <path>            Set the path for assets");
    SDL_Log("  -s <file>            Load subtitle styles file");
    SDL_Log("  -r <gpu>             Set preferred GPU");
    SDL_Log("  -g                   Show GPU render drivers list");
    SDL_Log("  -f                   Set fullscreen window mode");
    SDL_Log("  -o                   Enable OpenGL rendering context");
    SDL_Log("  -x <width> <height>  Set window size");
    SDL_Log("  -v                   Enable VSync");
    SDL_Log("  -z                   Show FPS");
    SDL_Log("  -t                   Show Timestamp");
    SDL_Log("  -u                   Show debug UI");
    SDL_Log("  -h                   Show this help message");
  };

  for (int i = 1; i < argc; ++i) {
    char *arg = argv[i];

    if (*arg == '-') {
      switch (*(arg + 1)) {
      case '!':
        break;

      case 'h': // Exibe a ajuda
        showHelp();
        return false; // Sai da função após mostrar a ajuda

      case 'l': // Carrega o gameplay
        if (!initDependent)
          break;
        if (i + 1 < argc) {
          *arg = '!';
          loadScriptFile(argv[++i]);
        } else {
          SDL_LogError(0, "Missing argument for -l option (gameplay file)");
        }
        break;

      case 'g': // Exibe a lista de drivers de renderização
        for (int j = 0; j < SDL_GetNumRenderDrivers(); ++j) {
          const char *name = SDL_GetRenderDriver(j);
          SDL_Log("Driver %d: %s", j, name);
        }
        return false; // Sai após exibir a lista de drivers

      case 'p': // Define o caminho dos assets
        if (i + 1 < argc) {
          *arg = '!';
          gameContext.assetsPath = argv[++i];
        } else {
          SDL_LogError(0, "Missing argument for -p option (assets path)");
        }
        break;

      case 's': // Carrega o arquivo de estilos de legenda
        if (!initDependent)
          break;
        if (i + 1 < argc) {
          *arg = '!';
          loadSubtitleStylesFile(argv[++i]);
        } else {
          SDL_LogError(0,
                       "Missing argument for -s option (subtitle styles file)");
        }
        break;

      case 'r': // Define o GPU preferido
        if (i + 1 < argc) {
          *arg = '!';
          preferedGPU = argv[++i];
        } else {
          SDL_LogError(0, "Missing argument for -r option (preferred GPU)");
        }
        break;

      case 'f': // Define o modo de tela cheia
        *arg = '!';
        gameContext.flags |= SDL_WINDOW_FULLSCREEN;
        break;

      case 'o': // Habilita contexto OpenGL
        *arg = '!';
        gameContext.flags |= SDL_WINDOW_OPENGL;
        break;

      case 'x': // Define altura e largura da janela
        *arg = '!';
        if (i + 2 < argc) {
          windowsWidth = SDL_atoi(argv[++i]);
          windowsHeight = SDL_atoi(argv[++i]);
          SDL_SetWindowSize(gameContext.window, windowsWidth, windowsHeight);
        } else {
          SDL_LogError(0, "Missing arguments for -x option (width and height)");
        }
        break;

      case 'v': // Ativa o V-Sync
        if (!initDependent)
          break;
        *arg = '!';
        SDL_SetRenderVSync(gameContext.render, 1);
        break;

      case 'z': // Ativa a exibição de FPS
        *arg = '!';
        gameContext.showFps = true;
        break;

      case 't': // Ativa a exibição de timestamp
        *arg = '!';
        gameContext.showTimestamp = true;
        break;

      case 'u':
        *arg = '!';
        Kotonoha_BasicGuiShow = true;
        break;

      default:
        SDL_LogError(0, "Unknown option: %s", arg);
        break;
        return false;
      }
    }
  }
  return true;
}

// Função para carregar estilos do arquivo
void Kotonoha::loadSubtitleStylesFile(char *path) {
  std::ifstream file(path, std::ios::binary | std::ios::ate);
  if (!file.is_open()) {
    std::cerr << "Erro ao abrir o arquivo: " << path << std::endl;
    return;
  }

  std::streamsize size = file.tellg();
  file.seekg(0, std::ios::beg);

  std::string content(size, '\0');
  if (!file.read(&content[0], size)) {
    std::cerr << "Erro ao ler o arquivo: " << path << std::endl;
    return;
  }
  file.close();

  // Libera o estilo atual antes de carregar um novo
  delete[] gameContext.styleStr;
  gameContext.styleStr = new char[content.size() + 1];
  std::copy(content.begin(), content.end(), gameContext.styleStr);
  gameContext.styleStr[content.size()] = '\0';
}

void Kotonoha::loadWindowIcon(const char *path) {
  if (path == nullptr)
    return;

  SDL_Surface *iconSurface = Kotonoha_imageCreateSurface(path, 64, 64);
  if (iconSurface == nullptr) {
    SDL_LogError(0, "Couldn't load window icon: %s", SDL_GetError());
    return;
  }
  SDL_SetWindowIcon(gameContext.window, iconSurface);
  SDL_DestroySurface(iconSurface);
  return;
}

bool Kotonoha::loadScriptFile(const char *path) {
  try {
    Gameplay *newObject = new Gameplay(path, &gameContext);
    gameplays.push_back(newObject);
    return true;
  } catch (...) {
    return false;
  }
}

// Função de eventos
SDL_AppResult Kotonoha::Event(SDL_Event *event) {
  switch (event->type) {
  case SDL_EVENT_QUIT:
    return SDL_APP_SUCCESS;

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
      gameContext.showFps = !gameContext.showFps;
      break;

    case SDLK_F9:
      SDL_GetRenderVSync(gameContext.render, &gameContext.vsync);
      gameContext.vsync ? SDL_SetRenderVSync(gameContext.render, 0)
                        : SDL_SetRenderVSync(gameContext.render, 1);
      break;
    case SDLK_F8:
      gameContext.showTimestamp = !gameContext.showTimestamp;
      break;
    case SDLK_F7:
      Kotonoha_BasicGuiShow = !Kotonoha_BasicGuiShow;
      break;
    }
    break;
  case SDL_EVENT_MOUSE_MOTION:
    lastMouseTime = SDL_GetTicks();
    break;
  default:
    break;
  }

  Kotonoha_eventPush(&gameContext.eventQueu, *event);
  return SDL_APP_CONTINUE;
}

void Kotonoha::ClearGameplays() {
  for (auto *gameplay : gameplays) {
    delete gameplay;
  }

  gameplays.clear();

  gameContext.scene = 0;
  gameContext.next = false;
  gameContext.back = false;
}

// Função principal do loop do jogo
SDL_AppResult Kotonoha::Main(Gameplay **out) {
  static size_t lastScene = static_cast<size_t>(-1);

  while (true) {
    if (!gameplays.empty() && gameContext.scene >= 0 &&
        static_cast<size_t>(gameContext.scene) < gameplays.size()) {

      size_t currentScene = static_cast<size_t>(gameContext.scene);

      if (lastScene != currentScene) {
        gameplays[currentScene]->Reset();
        lastScene = currentScene;
      }

      if (gameContext.back && gameContext.scene > 0) {
        gameplays[currentScene]->Pause();
        gameContext.scene--;
        gameContext.back = false;
        continue;
      }

      SDL_AppResult result = gameplays[currentScene]->Main(&gameContext);

      if (result != SDL_APP_CONTINUE || gameContext.next) {
        gameContext.next = false;
        gameplays[currentScene]->Pause();
        gameContext.scene++;

        if (gameContext.scene >= 0 &&
            static_cast<size_t>(gameContext.scene) < gameplays.size()) {
          continue;
        } else {
          lastScene = gameContext.scene;
          break;
        }
      } else {
        *out = gameplays[currentScene];
        break;
      }
    }
    break;
  }

  SDL_SetRenderTarget(gameContext.render, nullptr);
  Kotonoha_eventFree(&gameContext.eventQueu);

  if (SDL_GetTicks() - lastMouseTime <= 1000) {
    SDL_ShowCursor();
  } else {
    SDL_HideCursor();
  }

  return SDL_APP_CONTINUE;
}
} // namespace Kotonoha
