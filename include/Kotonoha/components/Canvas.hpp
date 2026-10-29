#pragma once
#include <vector>

#include <Kotonoha/Kotonoha.h>
#include <SDL3/SDL.h>

namespace Kotonoha {

// Snapshot of one canvas layer for the debug UI.
struct CanvasLayerInfo {
  const char *name = "";
  Sint16 zIndex = 0;
  SDL_FRect place{};
  Kotonoha_Scene_Status status = KOTONOHA_SCENE_NULL;
  SDL_Texture *texture = nullptr; // what the layer last drew (may be null)
  bool retained = false;          // the last frame is being redrawn
};

struct CanvasItem {
  Kotonoha_Scene_Status (*drawingPoint)(KOTONOHA_SCENE_CALL);
  SDL_Texture *target;
  SDL_Texture *dirtyFrame;

  Sint16 zIndex;
  SDL_FRect place;
  int targetWidth;
  int targetHeight;
  bool swapTexture;
  bool keepLastFrame;
  bool hasDirtyFrame;

  void *userData;
  const char *name;
  Kotonoha_Scene_Status lastStatus;
};

// Standard canvas layer z-indices across engine components.
enum class CanvasLayer : Sint16 {
  Image = 0,
  Video = 1,
  Prompt = 2,
  Subtitles = 3,
  Fade = 4,
  Overlay = 5
};

// Unified render dispatcher for canvas components. Connects component
// callbacks with both 4-argument and legacy 5-argument dispatch signatures.
template <typename Component, Kotonoha_Scene_Status (*CallbackFunc)(KOTONOHA_SCENE_CALL)>
struct ComponentRenderDispatcher {
  using Callback = Kotonoha_Scene_Status (*)(SDL_Window *, SDL_Renderer *,
                                             void *, SDL_Texture *);

  operator Callback() const { return CallbackFunc; }

  Kotonoha_Scene_Status operator()(SDL_Window *window, SDL_Renderer *render,
                                   void *userData, SDL_Texture *target) const {
    auto *comp = static_cast<Component *>(userData);
    return CallbackFunc(window, render, comp, target);
  }

  Kotonoha_Scene_Status operator()(SDL_Window *window, SDL_Renderer *render,
                                   void *legacyEventQueue, void *userData,
                                   SDL_Texture *target) const {
    (void)legacyEventQueue;
    auto *comp = static_cast<Component *>(userData);
    return CallbackFunc(window, render, comp, target);
  }
};

class Canvas {
private:
  std::vector<CanvasItem> drawingList{};
  Sint16 minDrawnZIndex = 32767;

public:
  void RegisterCanva(Kotonoha_Scene_Status (*drawingPoint)(KOTONOHA_SCENE_CALL),
                     Sint16 zIndex, SDL_FRect place, void *userData,
                     const char *name = "");

  void RegisterCanva(Kotonoha_Scene_Status (*drawingPoint)(KOTONOHA_SCENE_CALL),
                     CanvasLayer layer, SDL_FRect place, void *userData,
                     const char *name = "") {
    RegisterCanva(drawingPoint, static_cast<Sint16>(layer), place, userData, name);
  }

  void UnregisterCanva(
      Kotonoha_Scene_Status (*drawingPoint)(KOTONOHA_SCENE_CALL));

  // Remove layers owned by one concrete object.  This is safer than comparing
  // callback function addresses when the caller and the runtime live in
  // different DLL/EXE modules and is the correct lifetime boundary before the
  // pointed-to userdata is destroyed.
  void UnregisterCanvaUserData(void *userData);

  void UpdateCanva(Kotonoha_Scene_Status (*drawingPoint)(KOTONOHA_SCENE_CALL),
                   Sint16 zIndex, SDL_FRect place);
  // Changes only the backing render-target resolution. The item is still
  // composited into its physical `place`, allowing mobile builds to render at
  // a lower internal resolution and let SDL scale the finished layer.
  void UpdateTargetSize(int width, int height);

  SDL_AppResult RenderCanvas(SDL_Window *window, SDL_Renderer *render);

  int CanvasCount();
  std::vector<CanvasLayerInfo> GetLayerInfo() const;
  void BeginFrame();
  bool DrewAtOrBelow(Sint16 zIndex) const;
  void Reset();

  ~Canvas();
};
} // namespace Kotonoha
