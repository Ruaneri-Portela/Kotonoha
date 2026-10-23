// Generic Kotonoha application: the engine's default behaviour (see
// Kotonoha/Main.hpp) with no extra game logic, so every hook just continues.
#include <Kotonoha/Main.hpp>

SDL_AppResult LocalArgs(int, char *[]) { return SDL_APP_CONTINUE; }

SDL_AppResult LocalInit(void **, int, char *[]) { return SDL_APP_CONTINUE; }

SDL_AppResult LocalEvent(void *, SDL_Event *) { return SDL_APP_CONTINUE; }

SDL_AppResult LocalIterate(void *) { return SDL_APP_CONTINUE; }

void LocalQuit(void *, SDL_AppResult) {}
