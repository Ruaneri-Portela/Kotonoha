#pragma once

#include <Kotonoha/Kotonoha.hpp>
#include <SDL3/SDL.h>
#include <vector>

// Public entry points of the Dear ImGui based debug UI.
namespace Kotonoha::Debug {

// Creates the fonts, theme and ImGui backends. Call once after the renderer
// exists.
void Init(Kotonoha_Game &gameContext, std::vector<Uint8> font);

// Builds and renders one debug UI frame. Returns false when the application
// should stop.
bool Run(::Kotonoha::Kotonoha *game, ::Kotonoha::Gameplay *play,
         Kotonoha_Game &context);

// Forwards an SDL event to ImGui.
void ProcessEvent(SDL_Event *event);

// True while a debug UI window owns the focus.
bool IsAnyWindowFocused();
bool IsMouseCaptured();

// True while the script editor window is open.
bool IsScriptEditorVisible();

} // namespace Kotonoha::Debug
