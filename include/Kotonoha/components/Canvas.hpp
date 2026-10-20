#pragma once
#include <vector>

#include <Kotonoha/Kotonoha.h>
#include <SDL3/SDL.h>

namespace Kotonoha {

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
};

class Canvas {
private:
  std::vector<CanvasItem> drawingList{};
  Sint16 minDrawnZIndex = 32767;

public:
  void RegisterCanva(Kotonoha_Scene_Status (*drawingPoint)(KOTONOHA_SCENE_CALL),
                     Sint16 zIndex, SDL_FRect place, void *userData);

  void UnregisterCanva(
      Kotonoha_Scene_Status (*drawingPoint)(KOTONOHA_SCENE_CALL));

  void UpdateCanva(Kotonoha_Scene_Status (*drawingPoint)(KOTONOHA_SCENE_CALL),
                   Sint16 zIndex, SDL_FRect place);

  SDL_AppResult RenderCanvas(SDL_Window *window, SDL_Renderer *render);

  int CanvasCount();
  void BeginFrame();
  bool DrewAtOrBelow(Sint16 zIndex) const;
  void Reset();

  ~Canvas();
};
} // namespace Kotonoha
