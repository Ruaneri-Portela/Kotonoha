#include <Kotonoha/utils/Log.h>
#include <Kotonoha/Init.hpp>
#include <algorithm>
#include <cctype>
#if defined(__APPLE__)
#include <TargetConditionals.h>
#endif

// Definição da estrutura global
struct Kotonoha_Game global = {};

namespace Kotonoha {

void CleanupGlobalResources() {
  KotonohaFPSRenderShutdown();
  KotonohaTimestampRenderShutdown();
  KotonohaLoadingRenderShutdown();

  if (global.cursor != nullptr) {
    SDL_DestroyCursor(global.cursor);
    global.cursor = nullptr;
  }

  delete static_cast<Sound *>(global.sound);
  global.sound = nullptr;

  if (global.ass_track != nullptr) {
    ass_free_track(global.ass_track);
    global.ass_track = nullptr;
  }

  if (global.ass_renderer != nullptr) {
    ass_renderer_done(global.ass_renderer);
    global.ass_renderer = nullptr;
  }

  if (global.ass_library != nullptr) {
    ass_library_done(global.ass_library);
    global.ass_library = nullptr;
  }

  if (global.window != nullptr) {
    SDL_DestroyWindow(global.window);
    global.window = nullptr;
  }
}

bool InitSDLSubsystems() {
  SDL_SetAppMetadata("A visual novel engine", "0.1", "me.hirameki.kotonoha");

  if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_EVENTS |
                SDL_INIT_GAMEPAD)) {
    KLOG_ERROR(CORE, "Couldn't initialize SDL: %s", SDL_GetError());
    return false;
  }

  if (!TTF_Init()) {
    KLOG_ERROR(CORE, "Couldn't initialize TTF: %s", SDL_GetError());
    SDL_Quit();
    return false;
  }

  return true;
}

bool InitWindowIcon(SDL_Window *window, const char *path) {
  if (window == nullptr || path == nullptr) {
    return false;
  }

  char *absolutePath = KotonohaPathGetAbsolute(path);
  if (absolutePath == nullptr) {
    return false;
  }

  SDL_Surface *iconSurface = KotonohaImageCreateSurface(absolutePath, 64, 64);
  SDL_free(absolutePath);

  if (iconSurface == nullptr) {
    return false;
  }

  SDL_SetWindowIcon(window, iconSurface);
  SDL_DestroySurface(iconSurface);
  return true;
}

bool InitCursor() {
  if (global.cursor != nullptr) {
    SDL_DestroyCursor(global.cursor);
    global.cursor = nullptr;
  }

  if (global.config.cursorPath != nullptr) {
    char *absolutePath = KotonohaPathGetAbsolute(global.config.cursorPath);
    if (absolutePath != nullptr) {
      const int size = global.config.cursorSize;
      SDL_Surface *surface = KotonohaImageCreateSurface(absolutePath, size, size);
      SDL_free(absolutePath);
      if (surface != nullptr) {
        global.cursor = SDL_CreateColorCursor(surface, 0, 0);
        SDL_DestroySurface(surface);
      }
    }
    if (global.cursor == nullptr) {
      KLOG_WARN(CORE, "Custom cursor '%s' unavailable: %s",
                global.config.cursorPath, SDL_GetError());
    }
  }
  if (global.cursor == nullptr) {
    global.cursor = SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_DEFAULT);
  }
  if (global.cursor != nullptr) {
    SDL_SetCursor(global.cursor);
  }
  return global.cursor != nullptr;
}

bool InitWindow() {
  SDL_WindowFlags windowFlags = global.flags | SDL_WINDOW_RESIZABLE |
                                SDL_WINDOW_HIDDEN |
                                SDL_WINDOW_HIGH_PIXEL_DENSITY;

#if (defined(__APPLE__) && TARGET_OS_IOS) || defined(__ANDROID__)
  windowFlags |= SDL_WINDOW_FULLSCREEN;
  global.config.fullScreen = true;
#endif

  global.window =
      SDL_CreateWindow("Kotonoha Engine", global.config.windowSize[0],
                       global.config.windowSize[1], windowFlags);

  if (global.window == nullptr) {
    KLOG_ERROR(CORE, "Couldn't create window: %s", SDL_GetError());
    return false;
  }
  global.flags = SDL_GetWindowFlags(global.window);

#ifndef KOTONOHA_MOBILE
  InitWindowIcon(global.window, global.config.iconPath != nullptr
                                       ? global.config.iconPath
                                       : "assets/icon.png");
#endif
  InitCursor();
  return true;
}

bool InitRenderer() {
  std::vector<const char *> rendererMethods;
  const bool hasExplicitRenderer =
      global.config.preferredRenderer != nullptr &&
      SDL_strcasecmp(global.config.preferredRenderer, "Auto") != 0;
  if (hasExplicitRenderer) {
    rendererMethods.push_back(global.config.preferredRenderer);
  }

#if defined(KOTONOHA_HAS_ANIME4K)
  if (!hasExplicitRenderer && global.config.scalerFactor > 1.0f) {
#if defined(__ANDROID__) || defined(ANDROID) || (defined(TARGET_OS_IPHONE) && TARGET_OS_IPHONE)
    rendererMethods.push_back("opengles2");
#else
    rendererMethods.push_back("opengl");
#endif
  }
#endif

  rendererMethods.push_back(nullptr);
  rendererMethods.push_back("software");

  for (const char *method : rendererMethods) {
    global.render = SDL_CreateRenderer(global.window, method);

    if (global.render != nullptr) {
      KLOG_INFO(CORE, "Renderer created with method '%s'",
              method ? method : "automatic");
      break;
    }

    KLOG_ERROR(CORE, "Failed to create renderer with method '%s': %s",
                 method ? method : "automatic", SDL_GetError());
  }

  if (global.render == nullptr) {
    KLOG_ERROR(CORE, "Couldn't create renderer with any method, giving up: %s",
                 SDL_GetError());
    return false;
  }

  if (global.config.enableVsync && !SDL_SetRenderVSync(global.render, 1)) {
    KLOG_ERROR(CORE, "Failed to enable VSync: %s", SDL_GetError());
    return false;
  }

  return true;
}

bool PreloadFontsInto(ASS_Library *library,
                      std::unordered_map<std::string, TTF_Font *> &fonts,
                      const char *fontsDir, const char *imguiFontName,
                      std::vector<Uint8> *imguiFontData) {

  fonts.clear();

  if (imguiFontData != nullptr) {
    imguiFontData->clear();
  }

  if (library == nullptr || fontsDir == nullptr || *fontsDir == '\0') {
    return false;
  }

  char *absPath = KotonohaPathGetAbsolute(fontsDir);

  if (absPath == nullptr) {
    return false;
  }

  SDL_PathInfo info;

  if (!SDL_GetPathInfo(absPath, &info) || info.type != SDL_PATHTYPE_DIRECTORY) {
    KLOG_WARN(CORE, "Fonts directory not found: %s",
                absPath);

    SDL_free(absPath);
    return false;
  }

  struct ScanContext {
    ASS_Library *lib;
    std::string dir;
    std::unordered_map<std::string, TTF_Font *> *fonts;
    const char *imguiFontName;
    std::vector<Uint8> *imguiFontData;
  };

  ScanContext ctx{library, absPath, &fonts, imguiFontName, imguiFontData};

  SDL_free(absPath);
  absPath = nullptr;

  return SDL_EnumerateDirectory(
      ctx.dir.c_str(),
      [](void *userdata, const char *dirname,
         const char *entry) -> SDL_EnumerationResult {
        auto *c = static_cast<ScanContext *>(userdata);

        if (c == nullptr || c->fonts == nullptr || entry == nullptr) {
          return SDL_ENUM_CONTINUE;
        }

        const std::string name(entry);

        const size_t dot = name.find_last_of('.');

        if (dot == std::string::npos) {
          return SDL_ENUM_CONTINUE;
        }

        std::string ext = name.substr(dot);

        std::transform(ext.begin(), ext.end(), ext.begin(),
                       [](unsigned char ch) {
                         return static_cast<char>(std::tolower(ch));
                       });

        if (ext != ".ttf" && ext != ".otf" && ext != ".ttc") {
          return SDL_ENUM_CONTINUE;
        }

        char *joinedPath = KotonohaPathJoin(c->dir.c_str(), name.c_str());

        if (joinedPath == nullptr) {
          return SDL_ENUM_CONTINUE;
        }

        const std::string fullPath(joinedPath);

        SDL_free(joinedPath);
        joinedPath = nullptr;

        size_t size = 0;
        void *data = SDL_LoadFile(fullPath.c_str(), &size);

        if (data == nullptr || size == 0) {
          KLOG_WARN(CORE, "Failed to load font: %s: %s", fullPath.c_str(),
                      SDL_GetError());

          if (data != nullptr) {
            SDL_free(data);
          }

          return SDL_ENUM_CONTINUE;
        }

        const std::string fontName = name.substr(0, dot);

        if (c->imguiFontData != nullptr && c->imguiFontName != nullptr &&
            c->imguiFontData->empty() && fontName == c->imguiFontName) {

          const Uint8 *bytes = static_cast<const Uint8 *>(data);

          c->imguiFontData->assign(bytes, bytes + size);

          KLOG_INFO(CORE, "Selected font for ImGui: %s (%zu bytes)", fontName.c_str(),
                  size);
        }

        ass_add_font(c->lib, const_cast<char *>(name.c_str()),
                     static_cast<char *>(data), static_cast<int>(size));

        KLOG_INFO(CORE, "Preloaded font into LibASS memory: %s (%zu bytes)",
                name.c_str(), size);

        SDL_IOStream *io = SDL_IOFromMem(data, size);

        if (io == nullptr) {
          KLOG_WARN(CORE, "Failed to create memory IOStream for font: %s: %s",
                      fullPath.c_str(), SDL_GetError());

          SDL_free(data);
          return SDL_ENUM_CONTINUE;
        }

        TTF_Font *font = TTF_OpenFontIO(io, true, 48);

        if (font == nullptr) {
          KLOG_WARN(CORE, "Failed to open font from memory: %s: %s",
                      fullPath.c_str(), SDL_GetError());

          SDL_free(data);
          return SDL_ENUM_CONTINUE;
        }

        (*c->fonts)[fontName] = font;

        return SDL_ENUM_CONTINUE;
      },
      &ctx);
}

bool InitLibAssAndTTFs(std::string defaultFont,
                       std::unordered_map<std::string, TTF_Font *> &fonts,
                       std::vector<Uint8> &imguiFontData) {

  global.ass_library = ass_library_init();

  if (global.ass_library == nullptr) {
    KLOG_ERROR(CORE, "Couldn't initialize libAss");
    return false;
  }

  global.ass_renderer = ass_renderer_init(global.ass_library);

  if (global.ass_renderer == nullptr) {
    KLOG_ERROR(CORE, "Couldn't initialize libAss Render");
    return false;
  }

  if (!PreloadFontsInto(global.ass_library, fonts,
                        global.config.fontsPath != nullptr
                            ? global.config.fontsPath
                            : "assets/fonts",
                        defaultFont.c_str(), &imguiFontData)) {
    return false;
  }

  ass_set_extract_fonts(global.ass_library, 1);

  ass_set_fonts(global.ass_renderer, defaultFont.c_str(), nullptr,
                ASS_FONTPROVIDER_NONE, nullptr, 1);

  ass_set_shaper(global.ass_renderer, ASS_SHAPING_COMPLEX);
  ass_set_hinting(global.ass_renderer, ASS_HINTING_NATIVE);

  return true;
}

bool InitSound() {
  global.sound = new (std::nothrow) Sound;
  if (global.sound == nullptr) {
    KLOG_ERROR(CORE, "Couldn't create sound engine");
    return false;
  }

  return true;
}

Kotonoha *GetApp(void *appstate) { return static_cast<Kotonoha *>(appstate); }

} // namespace Kotonoha
