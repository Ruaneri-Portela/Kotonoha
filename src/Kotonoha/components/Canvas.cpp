#include <Kotonoha/components/Canvas.hpp>
#include <Kotonoha/utils/Log.h>
#include <algorithm>

namespace Kotonoha {

namespace {
static bool CanvasItemLess(const CanvasItem &a, const CanvasItem &b) {
  return a.zIndex < b.zIndex;
}

static bool IsValidSize(const SDL_FRect &place) {
  return place.w > 0.0f && place.h > 0.0f;
}

static void DestroyCanvasItemTextures(CanvasItem &item) {
  if (item.target != nullptr) {
    SDL_DestroyTexture(item.target);
    item.target = nullptr;
  }
  if (item.dirtyFrame != nullptr) {
    SDL_DestroyTexture(item.dirtyFrame);
    item.dirtyFrame = nullptr;
  }
  item.hasDirtyFrame = false;
  item.keepLastFrame = false;
}

static void DestroyCanvasItemTarget(CanvasItem &item) {
  if (item.target != nullptr) {
    SDL_DestroyTexture(item.target);
    item.target = nullptr;
  }
}

static bool CaptureDirtyFrame(SDL_Renderer *render, CanvasItem &item) {
  float width = 0.0f;
  float height = 0.0f;
  if (!SDL_GetTextureSize(item.target, &width, &height)) {
    KLOG_ERROR(RENDER, "Failed to get canvas texture dimensions: %s",
               SDL_GetError());
    return false;
  }

  if (item.dirtyFrame != nullptr) {
    float dirtyWidth = 0.0f;
    float dirtyHeight = 0.0f;
    if (!SDL_GetTextureSize(item.dirtyFrame, &dirtyWidth, &dirtyHeight) ||
        dirtyWidth != width || dirtyHeight != height) {
      SDL_DestroyTexture(item.dirtyFrame);
      item.dirtyFrame = nullptr;
      item.hasDirtyFrame = false;
    }
  }

  if (item.dirtyFrame == nullptr) {
    item.dirtyFrame = SDL_CreateTexture(
        render, SDL_PIXELFORMAT_RGBA8888, SDL_TEXTUREACCESS_TARGET,
        static_cast<int>(width), static_cast<int>(height));
    if (item.dirtyFrame == nullptr) {
      KLOG_ERROR(RENDER, "Failed to create retained canvas texture: %s",
                 SDL_GetError());
      return false;
    }
  }

  if (!SDL_SetRenderTarget(render, item.dirtyFrame)) {
    KLOG_ERROR(RENDER, "Failed to set retained canvas target: %s",
               SDL_GetError());
    return false;
  }

  Uint8 drawR = 0;
  Uint8 drawG = 0;
  Uint8 drawB = 0;
  Uint8 drawA = 0;
  SDL_BlendMode sourceBlendMode = SDL_BLENDMODE_BLEND;
  const bool drawColorSaved =
      SDL_GetRenderDrawColor(render, &drawR, &drawG, &drawB, &drawA);
  const bool sourceBlendSaved =
      SDL_GetTextureBlendMode(item.target, &sourceBlendMode);
  const bool snapshotPrepared =
      drawColorSaved && sourceBlendSaved &&
      SDL_SetRenderDrawColor(render, 0, 0, 0, 0) && SDL_RenderClear(render) &&
      SDL_SetTextureBlendMode(item.target, SDL_BLENDMODE_NONE) &&
      SDL_SetTextureBlendMode(item.dirtyFrame, SDL_BLENDMODE_NONE);
  const bool copied = snapshotPrepared &&
                      SDL_RenderTexture(render, item.target, nullptr, nullptr);
  const bool sourceBlendRestored =
      sourceBlendSaved && SDL_SetTextureBlendMode(item.target, sourceBlendMode);
  const bool snapshotBlendSet =
      SDL_SetTextureBlendMode(item.dirtyFrame, SDL_BLENDMODE_BLEND);
  const bool drawColorRestored =
      drawColorSaved &&
      SDL_SetRenderDrawColor(render, drawR, drawG, drawB, drawA);
  const bool targetRestored = SDL_SetRenderTarget(render, item.target);

  if (!snapshotPrepared || !copied || !sourceBlendRestored || !targetRestored ||
      !snapshotBlendSet || !drawColorRestored) {
    KLOG_ERROR(RENDER, "Failed to preserve canvas frame: %s", SDL_GetError());
    item.hasDirtyFrame = false;
    return false;
  }

  item.hasDirtyFrame = true;
  return true;
}
} // namespace

void Canvas::RegisterCanva(
    Kotonoha_Scene_Status (*drawingPoint)(KOTONOHA_SCENE_CALL), Sint16 zIndex,
    SDL_FRect place, void *userData, const char *name) {
  if (drawingPoint == nullptr || userData == nullptr) {
    return;
  }

  CanvasItem item{};
  item.drawingPoint = drawingPoint;
  item.zIndex = zIndex;
  item.place = place;
  item.targetWidth = 0;
  item.targetHeight = 0;
  item.userData = userData;
  item.target = nullptr;
  item.dirtyFrame = nullptr;
  item.swapTexture = false;
  item.keepLastFrame = false;
  item.hasDirtyFrame = false;
  item.name = name != nullptr ? name : "";
  item.lastStatus = KOTONOHA_SCENE_NULL;

  drawingList.push_back(item);
  std::sort(drawingList.begin(), drawingList.end(), CanvasItemLess);
}

void Canvas::UnregisterCanva(
    Kotonoha_Scene_Status (*drawingPoint)(KOTONOHA_SCENE_CALL)) {
  for (auto &item : drawingList) {
    if (item.drawingPoint == drawingPoint) {
      DestroyCanvasItemTextures(item);
    }
  }

  drawingList.erase(std::remove_if(drawingList.begin(), drawingList.end(),
                                   [drawingPoint](const CanvasItem &item) {
                                     return item.drawingPoint == drawingPoint;
                                   }),
                    drawingList.end());
}

void Canvas::UnregisterCanvaUserData(void *userData) {
  if (userData == nullptr) return;

  for (auto &item : drawingList) {
    if (item.userData == userData) {
      DestroyCanvasItemTextures(item);
    }
  }

  drawingList.erase(std::remove_if(drawingList.begin(), drawingList.end(),
                                   [userData](const CanvasItem &item) {
                                     return item.userData == userData;
                                   }),
                    drawingList.end());
}

void Canvas::UpdateCanva(
    Kotonoha_Scene_Status (*drawingPoint)(KOTONOHA_SCENE_CALL), Sint16 zIndex,
    SDL_FRect place) {
  bool needsResort = false;

  for (auto &item : drawingList) {
    if (drawingPoint != nullptr && item.drawingPoint != drawingPoint) {
      continue;
    }

    if (item.place.w != place.w || item.place.h != place.h) {
      item.swapTexture = true;
    }

    if (zIndex != -1 && item.zIndex != zIndex) {
      item.zIndex = zIndex;
      needsResort = true;
    }

    item.place = place;
  }

  if (needsResort) {
    std::sort(drawingList.begin(), drawingList.end(), CanvasItemLess);
  }
}

void Canvas::UpdateTargetSize(int width, int height) {
  const int nextWidth = std::max(1, width);
  const int nextHeight = std::max(1, height);
  for (auto &item : drawingList) {
    if (item.targetWidth != nextWidth || item.targetHeight != nextHeight) {
      item.targetWidth = nextWidth;
      item.targetHeight = nextHeight;
      item.swapTexture = true;
    }
  }
}

std::vector<CanvasLayerInfo> Canvas::GetLayerInfo() const {
  std::vector<CanvasLayerInfo> layers;
  layers.reserve(drawingList.size());
  for (const CanvasItem &item : drawingList) {
    CanvasLayerInfo info;
    info.name = item.name;
    info.zIndex = item.zIndex;
    info.place = item.place;
    info.status = item.lastStatus;
    info.retained = item.keepLastFrame;
    info.texture = item.target;
    layers.push_back(info);
  }
  return layers;
}

void Canvas::BeginFrame() { minDrawnZIndex = 32767; }

bool Canvas::DrewAtOrBelow(Sint16 zIndex) const {
  return minDrawnZIndex <= zIndex;
}

SDL_AppResult Canvas::RenderCanvas(SDL_Window *window, SDL_Renderer *render) {
  if (render == nullptr) {
    return SDL_APP_FAILURE;
  }

  BeginFrame();
  SDL_AppResult status = SDL_APP_CONTINUE;

  for (auto &item : drawingList) {
    if (item.userData == nullptr || item.drawingPoint == nullptr) {
      continue;
    }

    if (!IsValidSize(item.place)) {
      continue;
    }

    if (item.swapTexture) {
      // Resize the live render target immediately, but keep a retained
      // DRAW_LAST frame available until the callback produces a frame at the
      // new size.  CaptureDirtyFrame will recreate dirtyFrame once that happens.
      DestroyCanvasItemTarget(item);
      item.swapTexture = false;
    }

    if (item.target == nullptr) {
      const int targetWidth =
          item.targetWidth > 0 ? item.targetWidth
                               : std::max(1, static_cast<int>(item.place.w));
      const int targetHeight =
          item.targetHeight > 0 ? item.targetHeight
                                : std::max(1, static_cast<int>(item.place.h));
      item.target = SDL_CreateTexture(
          render, SDL_PIXELFORMAT_RGBA8888, SDL_TEXTUREACCESS_TARGET,
          targetWidth, targetHeight);

      if (item.target == nullptr) {
        KLOG_ERROR(RENDER, "Failed to create canvas texture: %s",
                   SDL_GetError());
        status = SDL_APP_FAILURE;
        continue;
      }
      if (!SDL_SetTextureBlendMode(item.target, SDL_BLENDMODE_BLEND)) {
        KLOG_ERROR(RENDER, "Failed to configure canvas texture blending: %s",
                   SDL_GetError());
        status = SDL_APP_FAILURE;
      }
    }

    if (!SDL_SetRenderTarget(render, item.target)) {
      KLOG_ERROR(RENDER, "Failed to set canvas render target: %s",
                 SDL_GetError());
      status = SDL_APP_FAILURE;
      continue;
    }
    const Kotonoha_Scene_Status result =
        item.drawingPoint(window, render, item.userData, item.target);
    item.lastStatus = result;

    if (result == KOTONOHA_SCENE_FATAL_ERROR) {
      // A component has declared its render state unusable.  Do not silently
      // convert that into a successful frame; restore the backbuffer and let
      // Gameplay propagate the failure to its owner.
      if (!SDL_SetRenderTarget(render, nullptr)) {
        KLOG_ERROR(RENDER,
                   "Failed to restore backbuffer after fatal canvas callback: %s",
                   SDL_GetError());
      }
      return SDL_APP_FAILURE;
    }
    const bool hasNewFrame = result == KOTONOHA_SCENE_DRAW ||
                             result == KOTONOHA_SCENE_DRAW_LAST ||
                             result == KOTONOHA_SCENE_DRAW_OVERLAYED;

    if (result == KOTONOHA_SCENE_DRAW_LAST) {
      item.keepLastFrame = true;
    } else if (result == KOTONOHA_SCENE_DRAW ||
               result == KOTONOHA_SCENE_DRAW_OVERLAYED) {
      item.keepLastFrame = false;
    }

    if (result == KOTONOHA_SCENE_COMPLETE) {
      // O componente terminou: solta o último frame para não cobrir as
      // camadas inferiores (ex.: imagem após o vídeo).
      item.keepLastFrame = false;
      item.hasDirtyFrame = false;
    }

    if (hasNewFrame && !CaptureDirtyFrame(render, item)) {
      status = SDL_APP_FAILURE;
    }

    if (result == KOTONOHA_SCENE_NULL || result == KOTONOHA_SCENE_COMPLETE ||
        result == KOTONOHA_SCENE_WAITING) {
      if (!SDL_SetRenderTarget(render, nullptr)) {
        KLOG_ERROR(RENDER,
                   "Failed to restore backbuffer after canvas callback: %s",
                   SDL_GetError());
        return SDL_APP_FAILURE;
      }
      SDL_Texture *retained =
          item.hasDirtyFrame &&
                  (item.keepLastFrame || result == KOTONOHA_SCENE_WAITING)
              ? item.dirtyFrame
              : nullptr;
      if (retained != nullptr &&
          !SDL_RenderTexture(render, retained, nullptr, &item.place)) {
        KLOG_ERROR(RENDER, "Failed to draw retained canvas item: %s",
                   SDL_GetError());
        status = SDL_APP_FAILURE;
      } else if (retained != nullptr) {
        minDrawnZIndex = std::min(minDrawnZIndex, item.zIndex);
      }
      continue;
    }

    if (item.target != nullptr) {
      if (!SDL_SetRenderTarget(render, nullptr)) {
        KLOG_ERROR(RENDER,
                   "Failed to restore backbuffer after canvas callback: %s",
                   SDL_GetError());
        return SDL_APP_FAILURE;
      }
      if (SDL_RenderTexture(render, item.target, nullptr, &item.place) &&
          hasNewFrame) {
        minDrawnZIndex = std::min(minDrawnZIndex, item.zIndex);
      } else if (hasNewFrame) {
        KLOG_ERROR(RENDER, "Failed to draw canvas texture: %s", SDL_GetError());
        status = SDL_APP_FAILURE;
      }
    }
  }
  return status;
}

int Canvas::CanvasCount() { return static_cast<int>(drawingList.size()); }

void Canvas::Reset() {
  for (auto &item : drawingList) {
    DestroyCanvasItemTextures(item);
  }

  minDrawnZIndex = 32767;
}

Canvas::~Canvas() { Canvas::Reset(); }

} // namespace Kotonoha
