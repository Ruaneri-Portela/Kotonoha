#include <Kotonoha/parsers/Config.h>
#include <Kotonoha/utils/Log.h>
#include <SDL3/SDL.h>
#include <errno.h>
#include <limits.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

const char *KotonohaAboutText(void) {
  return (
      "Kotonoha\n"
      "========\n"
      "\n"
      "Kotonoha is a Visual Novel engine built upon SDL (Simple DirectMedia "
      "Layer)\n"
      "and FFmpeg. It is a reimagining and reimplementation of the FORIS "
      "engine,\n"
      "originally used in the \"Days\" series games produced by 0verflow.\n"
      "\n"
      "Its goal is to provide a versatile and robust platform for creating "
      "and\n"
      "playing visual novels.\n"
      "\n"
      "Project:\n"
      "  https://github.com/Ruaneri-Portela/Kotonoha\n"
      "\n"
      "Contributors:\n"
      "  Ruaneri Ferreira Portela - https://github.com/Ruaneri-Portela\n"
      "  tsubaki-0xSUZU           - https://github.com/tsubaki-0x\n"
      "\n"
      "License:\n"
      "  MIT License\n"
      "\n"
      "Copyright (c) 2023 Ruaneri Ferreira Portela\n");
}

static void About(void) { fputs(KotonohaAboutText(), stdout); }

static void Help(void) {
  printf(
      "Usage: Kotonoha [options]\n"
      "\n"
      "Information:\n"
      "  -h                   Show this help message\n"
      "  -A                   Show project and license information\n"
      "  -G                   List available GPU renderers\n"
      "  -D                   List available audio drivers\n"
      "\n"
      "Loading and paths:\n"
      "  -l <file>            Load a gameplay script\n"
      "  -s <file>            Set the ASS subtitle styles file\n"
      "  -p <path>            Set the assets directory\n"
      "\n"
      "Video and audio:\n"
      "  -r <name>            Set the preferred renderer\n"
      "  -x <WxH>             Set the window size (example: -x 1280x720)\n"
      "  -q <factor>          Set the upscale factor; 1.0 disables upscaling\n"
      "  -f                   Toggle fullscreen mode\n"
      "  -v                   Enable VSync\n"
      "  -c <sec>             Media (audio/video) cache duration in seconds "
      "(default: "
      "2.0)\n"
      "\n"
      "Debugging:\n"
      "  -i                   Skip missing assets\n"
      "  -z                   Show FPS counter\n"
      "  -t                   Show timestamps\n"
      "  -u                   Show debug UI\n");
}

static char *Config_CopyString(const char *text) {
  if (text == NULL) {
    return NULL;
  }

  size_t length = SDL_strlen(text);
  char *copy = (char *)SDL_malloc(length + 1);

  if (copy != NULL) {
    SDL_memcpy(copy, text, length + 1);
  }

  return copy;
}

static bool Config_SetStr(char **target, const char *source) {
  if (target == NULL || source == NULL) {
    return false;
  }

  char *copy = SDL_strdup(source);

  if (copy == NULL) {
    return false;
  }

  if (*target != NULL) {
    SDL_free(*target);
  }

  *target = copy;
  return true;
}

static bool Config_AddScript(Kotonoha_Config *config, const char *scriptPath) {
  if (config->scriptCount == config->scriptCapacity) {
    int newCapacity =
        config->scriptCapacity == 0 ? 4 : config->scriptCapacity * 2;

    char **newScripts = (char **)SDL_realloc(
        config->scriptsForced, (size_t)newCapacity * sizeof(char *));

    if (newScripts == NULL) {
      fprintf(stderr, "Out of memory while adding script: %s\n", scriptPath);

      return false;
    }

    config->scriptsForced = newScripts;
    config->scriptCapacity = newCapacity;
  }

  char *copy = Config_CopyString(scriptPath);

  if (copy == NULL) {
    fprintf(stderr, "Out of memory while copying script path: %s\n",
            scriptPath);

    return false;
  }

  config->scriptsForced[config->scriptCount++] = copy;
  return true;
}

static bool Config_ParseInt(const char *text, int *output) {
  if (text == NULL || output == NULL) {
    return false;
  }

  char *end = NULL;
  errno = 0;

  long value = strtol(text, &end, 10);

  if (end == text || *end != '\0' || errno == ERANGE || value < INT_MIN ||
      value > INT_MAX) {
    return false;
  }

  *output = (int)value;
  return true;
}

static bool Config_SetWindowSize(Kotonoha_Config *config, const char *widthText,
                                 const char *heightText) {
  int width = 0;
  int height = 0;

  if (!Config_ParseInt(widthText, &width) ||
      !Config_ParseInt(heightText, &height) || width <= 0 || height <= 0) {
    fprintf(stderr, "Invalid window size: %s x %s\n", widthText, heightText);

    return false;
  }

  config->windowSize[0] = width;
  config->windowSize[1] = height;
  config->dirtyConfig = true;

  return true;
}

static bool Config_SetWindowSizeText(Kotonoha_Config *config,
                                     const char *sizeText) {
  if (sizeText == NULL) {
    return false;
  }

  char separator = '\0';
  const char *separatorPosition = sizeText;

  while (*separatorPosition != '\0') {
    if (*separatorPosition == 'x' || *separatorPosition == 'X') {
      separator = *separatorPosition;
      break;
    }

    ++separatorPosition;
  }

  if (separator == '\0') {
    fprintf(stderr, "Invalid window size format: %s (expected WxH)\n",
            sizeText);

    return false;
  }

  size_t widthLength = (size_t)(separatorPosition - sizeText);

  if (widthLength == 0 || widthLength >= 32) {
    fprintf(stderr, "Invalid window width: %s\n", sizeText);

    return false;
  }

  char widthText[32];
  SDL_memcpy(widthText, sizeText, widthLength);
  widthText[widthLength] = '\0';

  const char *heightText = separatorPosition + 1;

  if (*heightText == '\0') {
    fprintf(stderr, "Invalid window size format: %s (expected WxH)\n",
            sizeText);

    return false;
  }

  return Config_SetWindowSize(config, widthText, heightText);
}

static bool Config_SetUpscaleFactor(Kotonoha_Config *config,
                                    const char *factorText) {
  char *end = NULL;
  errno = 0;

  float factor = strtof(factorText, &end);

  if (end == factorText || *end != '\0' || errno == ERANGE ||
      !isfinite(factor) || factor <= 0.0f) {
    fprintf(stderr, "Invalid value for -q upscale factor: %s\n", factorText);

    return false;
  }

  config->scalerFactor = factor;
  config->dirtyConfig = true;

  printf("Upscaling factor set to %.3f%s\n", factor,
         factor > 1.0f ? " (upscale requested)" : " (native resolution)");

  return true;
}

static bool ParseArguments(Kotonoha_Config *config, int argc, char *argv[]) {
  bool cancelOpen = false;

  for (int i = 1; i < argc; ++i) {
    const char *arg = argv[i];

    if (arg == NULL || arg[0] != '-') {
      continue;
    }

    switch (arg[1]) {
    case 'A':
      About();
      cancelOpen = true;
      break;

    case 'G': {
      printf("Available GPU renderers:\n");

      for (int j = 0; j < SDL_GetNumRenderDrivers(); ++j) {
        const char *name = SDL_GetRenderDriver(j);
        printf("  %d. %s\n", j + 1, name ? name : "<null>");
      }

      cancelOpen = true;
      break;
    }

    case 'D': {
      printf("Available audio drivers:\n");

      for (int j = 0; j < SDL_GetNumAudioDrivers(); ++j) {
        const char *name = SDL_GetAudioDriver(j);
        printf("  %d. %s\n", j + 1, name ? name : "<null>");
      }

      cancelOpen = true;
      break;
    }

    case 'h':
      Help();
      cancelOpen = true;
      break;

    case 'l':
      if (i + 1 < argc && argv[i + 1] != NULL) {
        if (!Config_AddScript(config, argv[++i])) {
          cancelOpen = true;
        } else
          config->dirtyConfig = true;
      } else {
        fprintf(stderr, "Missing argument for -l option (gameplay script)\n");
        cancelOpen = true;
      }
      break;

    case 'p':
      if (i + 1 < argc && argv[i + 1] != NULL) {
        if (!Config_SetStr(&config->assetsPath, argv[++i])) {
          fprintf(stderr, "Failed to set assets directory\n");
          cancelOpen = true;
        } else
          config->dirtyConfig = true;
      } else {
        fprintf(stderr, "Missing argument for -p option (assets directory)\n");
        cancelOpen = true;
      }
      break;

    case 's':
      if (i + 1 < argc && argv[i + 1] != NULL) {
        if (!Config_SetStr(&config->assStyles, argv[++i])) {
          fprintf(stderr, "Failed to set ASS subtitle styles file\n");
          cancelOpen = true;
        } else
          config->dirtyConfig = true;
      } else {
        fprintf(stderr,
                "Missing argument for -s option (ASS subtitle styles file)\n");

        cancelOpen = true;
      }
      break;

    case 'r':
      if (i + 1 < argc && argv[i + 1] != NULL) {
        if (!Config_SetStr(&config->preferredRenderer, argv[++i])) {
          fprintf(stderr, "Failed to set preferred renderer\n");
          cancelOpen = true;
        } else
          config->dirtyConfig = true;
      } else {
        fprintf(stderr,
                "Missing argument for -r option (preferred renderer)\n");
        cancelOpen = true;
      }
      break;

    case 'x':
      if (i + 1 < argc && argv[i + 1] != NULL) {
        if (!Config_SetWindowSizeText(config, argv[++i])) {
          cancelOpen = true;
        } else
          config->dirtyConfig = true;
      } else {
        fprintf(stderr, "Missing argument for -x option (window size)\n");
        cancelOpen = true;
      }
      break;

    case 'q':
      if (i + 1 < argc && argv[i + 1] != NULL) {
        if (!Config_SetUpscaleFactor(config, argv[++i])) {
          cancelOpen = true;
        } else
          config->dirtyConfig = true;
      } else {
        fprintf(stderr, "Missing argument for -q option (upscale factor)\n");
        cancelOpen = true;
      }
      break;

    case 'c':
      if (i + 1 < argc && argv[i + 1] != NULL) {
        char *end = NULL;
        float cacheSec = strtof(argv[++i], &end);
        if (end != argv[i] && *end == '\0' && isfinite(cacheSec) &&
            cacheSec >= 0.1f && cacheSec <= 60.0f) {
          config->mediaCacheSeconds = cacheSec;
          config->dirtyConfig = true;
        } else {
          fprintf(stderr,
                  "Invalid cache duration for -C option: %s (must be 0.1 to "
                  "60.0)\n",
                  argv[i]);
          cancelOpen = true;
        }
      } else {
        fprintf(stderr,
                "Missing argument for -C option (cache duration in seconds)\n");
        cancelOpen = true;
      }
      break;

    case 'f':
      config->fullScreen = !config->fullScreen;
      config->dirtyConfig = true;
      break;

    case 'i':
      config->skipFiles = true;
      config->dirtyConfig = true;
      break;

    case 'v':
      config->enableVsync = true;
      config->dirtyConfig = true;
      break;

    case 'z':
      config->showFps = true;
      config->dirtyConfig = true;
      break;

    case 't':
      config->showTimestamp = true;
      config->dirtyConfig = true;
      break;

    case 'u':
      config->enableDebugUi = true;
      config->dirtyConfig = true;
      break;

    default:
      fprintf(stderr, "Unknown option: %s\n", arg);
      cancelOpen = true;
      break;
    }
    if (cancelOpen) {
      break;
    }
  }

  return cancelOpen;
}

static bool LoadConfigFile(Kotonoha_Config *config, const char *path) {
  if (!KotonohaConfigRegisterImGuiHandler(config)) {
    return false;
  }
  if (path != NULL) {
    // Se um caminho customizado foi passado, configura o arquivo no ImGui
    KotonohaConfigSetIniFilename(path);
    // Força o carregamento imediato do arquivo customizado
    KotonohaConfigLoadFromDisk(path);
  } else {
    // Carrega do arquivo padrão (ex: kotonoha.ini configurado no
    // io.IniFilename)
    KotonohaConfigLoadFromDisk(NULL);
  }

  return true;
}

static bool Config_LoadStylesFile(Kotonoha_Config *config) {
  if (config->assStyles == NULL) {
    return true;
  }

  size_t size = 0;

  char *data = (char *)SDL_LoadFile(config->assStyles, &size);

  if (data == NULL) {
    fprintf(stderr, "Failed to load ASS subtitle styles file '%s': %s\n",
            config->assStyles, SDL_GetError());

    return false;
  }

  if (config->assStylesData != NULL) {
    SDL_free(config->assStylesData);
  }

  config->assStylesData = data;
  config->assStylesSize = size;

  return true;
}

bool KotonohaConfigLoad(Kotonoha_Config *config, const char *path, int argc,
                        char *argv[]) {
#if defined(KOTONOHA_MOBILE)
  KotonohaMobileSetup();
#endif

  if (config == NULL) {
    return false;
  }

  // Define os valores padrão
  config->windowSize[0] = 1280;
  config->windowSize[1] = 720;
  config->scalerFactor = 1.0f;
  config->mediaCacheSeconds = 2.0f;
  config->debugUiScale = 1.0f;
  config->cursorSize = 32;
  config->splashSeconds = 3.0f;
  config->splashFadeSeconds = 0.8f;
  config->prefetchHorizonMs = 10000;
  config->seekBigMs = 5000;
  config->seekSmallMs = 50;
  config->loadingDebounceMs = 300;
  config->minLoadingPauseMs = 1000;
  config->upscalingEnabled = true;
  config->enableVsync = true;

  if (path != NULL) {
    config->configFilePath = Config_CopyString(path);

    if (config->configFilePath == NULL) {
      config->cancelOpen = true;
      return false;
    }
  }

  // Carrega as configurações do arquivo INI / ImGui
  if (!LoadConfigFile(config, path)) {
    config->cancelOpen = true;
    return false;
  }

  // Processa argumentos de linha de comando (argumentos sobrepõem o arquivo e
  // marcam dirty se necessário)
  config->cancelOpen = ParseArguments(config, argc, argv);

  if (!config->cancelOpen) {
    if (!Config_LoadStylesFile(config)) {
      config->cancelOpen = true;
      return false;
    }
  }

  KLOG_INFO(CONFIG, "Configuration loaded (cancelOpen=%d)",
            (int)config->cancelOpen);
  return !config->cancelOpen;
}

void KotonohaConfigFree(Kotonoha_Config *config) {
  if (config == NULL) {
    return;
  }

  if (config->assetsPath != NULL) {
    SDL_free(config->assetsPath);
    config->assetsPath = NULL;
  }

  if (config->assStyles != NULL) {
    SDL_free(config->assStyles);
    config->assStyles = NULL;
  }

  if (config->assStylesData != NULL) {
    SDL_free(config->assStylesData);
    config->assStylesData = NULL;
  }

  config->assStylesSize = 0;

  if (config->preferredRenderer != NULL) {
    SDL_free(config->preferredRenderer);
    config->preferredRenderer = NULL;
  }

  SDL_free(config->promptSelectSound);
  config->promptSelectSound = NULL;
  SDL_free(config->promptViewSound);
  config->promptViewSound = NULL;
  SDL_free(config->titleMusic);
  config->titleMusic = NULL;
  SDL_free(config->iconPath);
  config->iconPath = NULL;
  SDL_free(config->cursorPath);
  config->cursorPath = NULL;
  SDL_free(config->fontsPath);
  config->fontsPath = NULL;
  if (config->splashFiles != NULL) {
    SDL_free(config->splashFiles);
    config->splashFiles = NULL;
  }
  if (config->mpvUpscalerFiles != NULL) {
    SDL_free(config->mpvUpscalerFiles);
    config->mpvUpscalerFiles = NULL;
  }

  if (config->configFilePath != NULL) {
    SDL_free(config->configFilePath);
    config->configFilePath = NULL;
  }

  for (int i = 0; i < config->scriptCount; ++i) {
    if (config->scriptsForced[i] != NULL) {
      SDL_free(config->scriptsForced[i]);
    }
  }

  SDL_free(config->scriptsForced);

  config->scriptsForced = NULL;
  config->scriptCount = 0;
  config->scriptCapacity = 0;
}

void KotonohaConfigSave(Kotonoha_Config *config) {
  if (config == NULL) {
    return;
  }

  // Se a struct possui um caminho customizado salvo, atualiza o IniFilename do
  // ImGui
  if (config->configFilePath != NULL) {
    KotonohaConfigSetIniFilename(config->configFilePath);
  }

  // Passa as novas settings atualizadas e força a gravação imediata no arquivo
  // .ini
  KotonohaConfigApplyAndSave(config);
}