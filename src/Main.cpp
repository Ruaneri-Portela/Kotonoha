#include "Kotonoha/Gameplay.hpp"
#include "Kotonoha/components/Sound.hpp"
#include <Kotonoha/Kotonoha.hpp>
#include <string.h>

void Kotonoha_BasicGuiInit(Kotonoha_Game &gameContext);
bool Kotonoha_BasicGuiRun(Kotonoha::Kotonoha *game, Kotonoha::Gameplay *play,
                          Kotonoha_Game &context);
void Kotonoha_BasicGuiEvent(SDL_Event *event);

static struct Kotonoha_Game global = {};

extern "C" {
#define SDL_MAIN_USE_CALLBACKS 1
#include <SDL3/SDL_main.h>
/* Função chamada uma vez na inicialização */
SDL_AppResult SDL_AppInit(void **appstate, int argc, char *argv[]) {
#if defined(KOTONOHA_MOBILE)
  // Configurações específicas do Mobile
  Kotonoha_MobileSetup();
#endif
  // Inicializa SDL
  if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_EVENTS)) {
    SDL_LogError(0, "Couldn't initialize SDL: %s", SDL_GetError());
    return SDL_APP_FAILURE;
  }

  // Inicializa TTF
  if (!TTF_Init()) {
    SDL_LogError(0, "Couldn't initialize TTF: %s", SDL_GetError());
    return SDL_APP_FAILURE;
  }

  // Inicializa Window
  global.window =
      SDL_CreateWindow("Kotonoha Engine", 1280, 720,
                       global.flags | SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIDDEN);
  if (!global.window) {
    SDL_LogError(0, "Couldn't create windowr: %s", SDL_GetError());
    return SDL_APP_FAILURE;
  }

  // Inicializar Apha Substation
  global.ass_library = ass_library_init();
  if (!global.ass_library) {
    SDL_LogError(0, "Couldn't initialize libAss");
    return SDL_APP_FAILURE;
  }

  global.ass_renderer = ass_renderer_init(global.ass_library);
  if (!global.ass_renderer) {
    ass_library_done(global.ass_library);
    SDL_LogError(0, "Couldn't initialize libAss Render");
    return SDL_APP_FAILURE;
  }

  ass_set_extract_fonts(global.ass_library, 1);
  ass_set_fonts_dir(global.ass_library, "assets/fonts");
  ass_set_fonts(global.ass_renderer, "assets/fonts/NotoSans-Regular.ttf",
                nullptr, ASS_FONTPROVIDER_AUTODETECT, nullptr, 1);
  ass_set_shaper(global.ass_renderer, ASS_SHAPING_COMPLEX);
  ass_set_hinting(global.ass_renderer, ASS_HINTING_NATIVE);

  // Incializar Engine de audio
  global.sound = new Kotonoha::Sound;

  // Inicializa o Kotonoha com os argumentos da linha de comando
  SDL_AppResult status;
  *appstate = new Kotonoha::Kotonoha(argc, argv, &status, global);
  if (appstate != nullptr) {
    Kotonoha_BasicGuiInit(global);
  }
  return status;
}

/* Função chamada para tratar eventos (input do mouse, teclas, etc) */
SDL_AppResult SDL_AppEvent(void *appstate, SDL_Event *event) {
  Kotonoha_BasicGuiEvent(event);
  return static_cast<Kotonoha::Kotonoha *>(appstate)->Event(event);
}

/* Função chamada uma vez por frame, coração do programa */
SDL_AppResult SDL_AppIterate(void *appstate) {
  Kotonoha::Gameplay *inRunning = nullptr;
  Kotonoha::Kotonoha *app = static_cast<Kotonoha::Kotonoha *>(appstate);

  SDL_SetRenderTarget(global.render, nullptr);
  SDL_SetRenderDrawColor(global.render, 0, 0, 0, 0);
  SDL_RenderClear(global.render);

  SDL_AppResult status = app->Main(&inRunning);

  if (global.showFps) {
    Kotonoha_FPSRender(global.window, global.render, NULL, nullptr, nullptr);
  }

  if (global.showTimestamp && inRunning != nullptr) {
    Kotonoha_TimestampRender(global.window, global.render, nullptr,
                             inRunning->tm, nullptr);
  }

  SDL_SetRenderTarget(global.render, nullptr);
  if (!Kotonoha_BasicGuiRun(app, inRunning, global))
      return SDL_APP_SUCCESS;

  SDL_RenderPresent(global.render);
  return status;
}

/* Função chamada na finalização do programa */
void SDL_AppQuit(void *appstate, SDL_AppResult result) {
  (void)result;
  delete static_cast<Kotonoha::Kotonoha *>(appstate);
  delete static_cast<Kotonoha::Sound *>(global.sound);
  ass_renderer_done(global.ass_renderer);
  ass_library_done(global.ass_library);
  TTF_Quit();
  SDL_Quit();
}
}
