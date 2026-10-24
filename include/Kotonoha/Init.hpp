#pragma once

#include <Kotonoha/Kotonoha.hpp>
#include <SDL3/SDL.h>
#include <SDL3_ttf/SDL_ttf.h>
#include <string>
#include <unordered_map>
#include <vector>

namespace Kotonoha {

#if defined(_WIN32) && !defined(KOTONOHA_RUNTIME_STATIC)
#if defined(KOTONOHA_RUNTIME_EXPORTS)
#define KOTONOHA_RUNTIME_DATA __declspec(dllexport)
#else
#define KOTONOHA_RUNTIME_DATA __declspec(dllimport)
#endif
#else
#define KOTONOHA_RUNTIME_DATA
#endif

extern KOTONOHA_RUNTIME_DATA Kotonoha_Game global;

void CleanupGlobalResources();
bool InitSDLSubsystems();
bool InitWindowIcon(SDL_Window *window, const char *path);
bool InitCursor();
bool InitWindow();
bool InitRenderer();

bool PreloadFontsInto(ASS_Library *library,
                      std::unordered_map<std::string, TTF_Font *> &fonts,
                      const char *fontsDir, const char *imguiFontName,
                      std::vector<Uint8> *imguiFontData);

bool InitLibAssAndTTFs(std::string defaultFont,
                       std::unordered_map<std::string, TTF_Font *> &fonts,
                       std::vector<Uint8> &imguiFontData);

bool InitSound();
Kotonoha *GetApp(void *appstate);

inline bool &FrameHandledFlag() {
  static bool handled = false;
  return handled;
}

inline void MarkFrameHandled() { FrameHandledFlag() = true; }

SDL_AppResult EngineMasterInit(Kotonoha **app, int argc, char *argv[]);
void EngineMasterShutdown(Kotonoha **app);
SDL_AppResult EngineMasterEvent(Kotonoha *app, SDL_Event *event);
// True when the debug UI owns the event; game-side handlers must skip it.
bool EngineMasterCapturedByDebugUi(const SDL_Event *event);
SDL_AppResult EngineMasterMain(Kotonoha *app, Gameplay **running);

bool BasicGuiInit(Kotonoha_Game &gameContext, std::vector<Uint8> font);
void BasicGuiShutdown();
bool BasicGuiRun(::Kotonoha::Kotonoha *game, ::Kotonoha::Gameplay *play,
                 Kotonoha_Game &context);
void BasicGuiEvent(SDL_Event *event);

} // namespace Kotonoha
