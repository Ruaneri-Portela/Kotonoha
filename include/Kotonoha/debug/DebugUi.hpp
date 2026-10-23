#pragma once

#include <Kotonoha/Kotonoha.hpp>
#include <SDL3/SDL.h>
#include <vector>

namespace Kotonoha {

enum class DisplayMode : Uint8 {
  Windowed,
  ExclusiveFullscreen,
  BorderlessFullscreen
};

bool GetDisplayMode(const Kotonoha_Game &context, DisplayMode *mode);
bool SetDisplay(Kotonoha_Game &context, DisplayMode mode,
                SDL_DisplayID targetDisplay = 0);

} // namespace Kotonoha

// Public entry points of the Dear ImGui based debug UI.
namespace Kotonoha::Debug {

using WindowMenuExtension = void (*)(void *userdata);

// Creates the fonts, theme and ImGui backends. Call once after the renderer
// exists.
bool Init(Kotonoha_Game &gameContext, std::vector<Uint8> font);
void Shutdown();

// Starts the debug workspace after the game frame has been rendered.
bool BeginWorkspaceFrame(Kotonoha_Game &context,
                         WindowMenuExtension extension = nullptr,
                         void *userdata = nullptr);
// Renders the workspace and restores SDL's normal render scale.
bool FinishWorkspaceFrame(Kotonoha_Game &context);

// Tracks enableDebugUi; on each activation (F7, KOTONOHA_DEBUG_UI, settings)
// every tool window is closed so only the Debug panel is shown. Returns true
// on the activation frame so games can close their own windows too.
bool SyncActivation(Kotonoha_Game &context);

// Builds and renders one debug UI frame. Returns false when the application
// should stop.
bool Run(::Kotonoha::Kotonoha *game, ::Kotonoha::Gameplay *play,
         Kotonoha_Game &context, bool forceOpenScenes = false);

// Forwards an SDL event to ImGui.
void ProcessEvent(SDL_Event *event);
// True when the debug UI owns this input event and the game must not see it.
bool ShouldCaptureEvent(const SDL_Event *event);
void UpdateFocusState();

// Draws the Debug menu and makes every debug window reachable.
void DrawWindowMenu(WindowMenuExtension extension = nullptr,
                    void *userdata = nullptr);
bool &PlaybackVisibility();

// Draws utility windows inside an already-started ImGui frame.
bool DrawAuxiliaryWindows(::Kotonoha::Kotonoha *game,
                          ::Kotonoha::Gameplay *play,
                          Kotonoha_Game &context, bool forceOpenScenes,
                          bool &shouldExit, bool drawControlPanel = false);

// True while a debug UI window owns the focus.
bool IsAnyWindowFocused();
bool IsMouseCaptured();

// True while the script editor window is open.
bool IsScriptEditorVisible();

} // namespace Kotonoha::Debug
