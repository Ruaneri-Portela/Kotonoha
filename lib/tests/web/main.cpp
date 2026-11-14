#define SDL_MAIN_USE_CALLBACKS 1
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

namespace {
SDL_Window *window = nullptr;
SDL_Renderer *renderer = nullptr;
}

SDL_Renderer *KotonohaWebSelfTestRenderer() { return renderer; }
void KotonohaWebSelfTestDrawMedia(SDL_Renderer *renderer);

SDL_AppResult SDL_AppInit(void **, int, char **) {
  if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO))
    return SDL_APP_FAILURE;
  window = SDL_CreateWindow("Kotonoha Web Selftest", 800, 600, 0);
  if (!window)
    return SDL_APP_FAILURE;
  renderer = SDL_CreateRenderer(window, nullptr);
  return renderer ? SDL_APP_CONTINUE : SDL_APP_FAILURE;
}

SDL_AppResult SDL_AppEvent(void *, SDL_Event *event) {
  return event->type == SDL_EVENT_QUIT ? SDL_APP_SUCCESS : SDL_APP_CONTINUE;
}

SDL_AppResult SDL_AppIterate(void *) {
  SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
  SDL_RenderClear(renderer);
  KotonohaWebSelfTestDrawMedia(renderer);
  SDL_SetRenderDrawColor(renderer, 255, 0, 0, 255);
  const SDL_FRect overlay{350, 250, 100, 100};
  SDL_RenderFillRect(renderer, &overlay);
  SDL_RenderPresent(renderer);
  return SDL_APP_CONTINUE;
}

void SDL_AppQuit(void *, SDL_AppResult) {
  SDL_DestroyRenderer(renderer);
  SDL_DestroyWindow(window);
  SDL_Quit();
}
