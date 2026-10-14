#pragma once

#include <Kotonoha/Kotonoha.hpp>
#include <SDL3/SDL.h>
#include <SDL3_ttf/SDL_ttf.h>
#include <string>
#include <unordered_map>
#include <vector>

extern struct Kotonoha_Game global;

namespace Kotonoha {

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

} // namespace Kotonoha

void KotonohaBasicGuiInit(Kotonoha_Game &gameContext, std::vector<Uint8> font);
bool KotonohaBasicGuiRun(Kotonoha::Kotonoha *game, Kotonoha::Gameplay *play,
                         Kotonoha_Game &context);
void KotonohaBasicGuiEvent(SDL_Event *event);