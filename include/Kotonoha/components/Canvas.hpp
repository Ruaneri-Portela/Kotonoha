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
  bool swapTexture;
  bool keepLastFrame;
  bool hasDirtyFrame;

  void *userData;
  const char *name;
  Kotonoha_Scene_Status lastStatus;
};

class Canvas {
private:
  std::vector<CanvasItem> drawingList{};
  Sint16 minDrawnZIndex = 32767;

public:
  void RegisterCanva(Kotonoha_Scene_Status (*drawingPoint)(KOTONOHA_SCENE_CALL),
                     Sint16 zIndex, SDL_FRect place, void *userData,
                     const char *name = "");

  void UnregisterCanva(
      Kotonoha_Scene_Status (*drawingPoint)(KOTONOHA_SCENE_CALL));

  // Remove layers owned by one concrete object.  This is safer than comparing
  // callback function addresses when the caller and the runtime live in
  // different DLL/EXE modules and is the correct lifetime boundary before the
  // pointed-to userdata is destroyed.
  void UnregisterCanvaUserData(void *userData);

  void UpdateCanva(Kotonoha_Scene_Status (*drawingPoint)(KOTONOHA_SCENE_CALL),
                   Sint16 zIndex, SDL_FRect place);

  SDL_AppResult RenderCanvas(SDL_Window *window, SDL_Renderer *render);

  int CanvasCount();
  std::vector<CanvasLayerInfo> GetLayerInfo() const;
  void BeginFrame();
  bool DrewAtOrBelow(Sint16 zIndex) const;
  void Reset();

  ~Canvas();
};
} // namespace Kotonoha
