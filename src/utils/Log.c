#include <Kotonoha/utils/Log.h>

void KotonohaLogInit(void) {
  if (SDL_GetHint(SDL_HINT_LOGGING) != NULL) {
    return;
  }
#if defined(NDEBUG)
  SDL_SetLogPriorities(SDL_LOG_PRIORITY_INFO);
#else
  SDL_SetLogPriorities(SDL_LOG_PRIORITY_DEBUG);
#endif
}
