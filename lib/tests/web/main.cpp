#define SDL_MAIN_USE_CALLBACKS 1
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#include <emscripten/emscripten.h>

namespace {
SDL_Window *window = nullptr;
SDL_Renderer *renderer = nullptr;
int touchDown = 0;
int touchUp = 0;
int keyDown = 0;
int padDown = 0;
int padUp = 0;
}

extern "C" {
EMSCRIPTEN_KEEPALIVE int KotonohaWebSelfTestTouchDownCount() {
  return touchDown;
}
EMSCRIPTEN_KEEPALIVE int KotonohaWebSelfTestTouchUpCount() {
  return touchUp;
}
EMSCRIPTEN_KEEPALIVE int KotonohaWebSelfTestKeyDownCount() {
  return keyDown;
}
EMSCRIPTEN_KEEPALIVE int KotonohaWebSelfTestGamepadDownCount() {
  return padDown;
}
EMSCRIPTEN_KEEPALIVE int KotonohaWebSelfTestGamepadUpCount() {
  return padUp;
}
EMSCRIPTEN_KEEPALIVE int KotonohaWebSelfTestConnectedGamepads() {
  int count = 0;
  SDL_JoystickID *ids = SDL_GetGamepads(&count);
  SDL_free(ids);
  return count;
}
}

SDL_Renderer *KotonohaWebSelfTestRenderer() { return renderer; }
void KotonohaWebSelfTestDrawMedia(SDL_Renderer *renderer);

SDL_AppResult SDL_AppInit(void **, int, char **) {
  if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_GAMEPAD))
    return SDL_APP_FAILURE;
  window = SDL_CreateWindow("Kotonoha Web Selftest", 800, 600, 0);
  if (!window)
    return SDL_APP_FAILURE;
  renderer = SDL_CreateRenderer(window, nullptr);
  return renderer ? SDL_APP_CONTINUE : SDL_APP_FAILURE;
}

SDL_AppResult SDL_AppEvent(void *, SDL_Event *event) {
  if (event->type == SDL_EVENT_FINGER_DOWN)
    ++touchDown;
  else if (event->type == SDL_EVENT_FINGER_UP)
    ++touchUp;
  else if (event->type == SDL_EVENT_KEY_DOWN)
    ++keyDown;
  else if (event->type == SDL_EVENT_GAMEPAD_BUTTON_DOWN)
    ++padDown;
  else if (event->type == SDL_EVENT_GAMEPAD_BUTTON_UP)
    ++padUp;
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
