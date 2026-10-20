#include <Kotonoha/components/Video.hpp>

namespace Kotonoha {

Video::Video(Kotonoha_Time *timeManager, Kotonoha_Demuxer *demuxer,
             float cacheDurationSeconds)
    : timeManager(timeManager), demuxer(demuxer),
      cacheDurationSeconds(cacheDurationSeconds) {
  lock = SDL_CreateMutex();
  if (lock == nullptr) {
    SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                 "Failed to create video mutex: %s", SDL_GetError());
  }
}

bool Video::Register(const char *path, Uint64 startTime, Uint64 endTime,
                     bool useOrsFrameTimeline, Uint64 orsEndFrame) {
  if (timeManager == nullptr) {
    SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Video time manager is null.");
    return false;
  }

  if (path == nullptr || *path == '\0') {
    SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Video path is invalid.");
    return false;
  }

  Kotonoha_VideoData *object = KotonohaVideoRenderInit(
      path, timeManager, startTime, endTime, useOrsFrameTimeline, orsEndFrame,
      cacheDurationSeconds, demuxer);
  if (object == nullptr) {
    return false;
  }

  if (lock == nullptr) {
    KotonohaVideoRenderShutdown(&object);
    return false;
  }

  SDL_LockMutex(lock);
  videos.push_back(object);
  SDL_UnlockMutex(lock);
  return true;
}

void Video::RenderTexture(SDL_Renderer *render, SDL_Texture *texture) {
  if (render == nullptr || texture == nullptr) {
    return;
  }

  const float factor = KotonohaUpscalingGetFactor();

  if (factor > 1.0f && IsUpscalingActive()) {
    float videoWidth = 0.0f;
    float videoHeight = 0.0f;

    if (!SDL_GetTextureSize(texture, &videoWidth, &videoHeight)) {
      SDL_LogError(SDL_LOG_CATEGORY_RENDER,
                   "Failed to get video texture size: %s", SDL_GetError());
      return;
    }

    const int requiredWidth = static_cast<int>(videoWidth);
    const int requiredHeight = static_cast<int>(videoHeight);

    if (requiredWidth <= 0 || requiredHeight <= 0) {
      SDL_LogError(SDL_LOG_CATEGORY_RENDER, "Invalid video texture size: %dx%d",
                   requiredWidth, requiredHeight);
      return;
    }

    if (!EnsureUpscalingTextures(render, requiredWidth, requiredHeight,
                                 factor)) {
      SDL_LogError(SDL_LOG_CATEGORY_RENDER,
                   "Failed to ensure upscaling textures for video");
      return;
    }

    if (ProcessUpscale(render, texture)) {
      SDL_RenderTexture(render, upScaledTexture, nullptr, nullptr);
      return;
    }
  }

  SDL_RenderTexture(render, texture, nullptr, nullptr);
}

Kotonoha_Scene_Status Video::Render(KOTONOHA_SCENE_CALL) {
  Video *here = static_cast<Video *>(userData);
  if (here == nullptr || here->lock == nullptr) {
    return KOTONOHA_SCENE_COMPLETE;
  }

  if (!SDL_SetRenderDrawColor(render, 0, 0, 0, 255) ||
      !SDL_RenderClear(render)) {
    SDL_LogError(SDL_LOG_CATEGORY_RENDER, "Failed to clear video canvas: %s",
                 SDL_GetError());
    return KOTONOHA_SCENE_WAITING;
  }

  Kotonoha_Scene_Status returnStatus = KOTONOHA_SCENE_NULL;
  std::vector<Kotonoha_VideoData *> activeVideosCopy;
  std::vector<Kotonoha_VideoData *> finishedVideos;

  SDL_LockMutex(here->lock);
  activeVideosCopy = here->videos;
  SDL_UnlockMutex(here->lock);

  for (Kotonoha_VideoData *currentVideo : activeVideosCopy) {
    if (currentVideo == nullptr) {
      continue;
    }

    const Kotonoha_Scene_Status status =
        KotonohaVideoRenderProcess(currentVideo, render);

    if (status == KOTONOHA_SCENE_DRAW || status == KOTONOHA_SCENE_COMPLETE) {
      if (currentVideo->texture != nullptr) {
        here->RenderTexture(render, currentVideo->texture);
      }

      if (status == KOTONOHA_SCENE_DRAW) {
        returnStatus = KOTONOHA_SCENE_DRAW;
      } else {
        if (returnStatus != KOTONOHA_SCENE_DRAW) {
          returnStatus = KOTONOHA_SCENE_DRAW_LAST;
        }
        finishedVideos.push_back(currentVideo);
      }
      continue;
    }

    if (status == KOTONOHA_SCENE_WAITING) {
      if (returnStatus != KOTONOHA_SCENE_DRAW &&
          returnStatus != KOTONOHA_SCENE_DRAW_LAST) {
        returnStatus = KOTONOHA_SCENE_WAITING;
      }
      continue;
    }
  }

  SDL_LockMutex(here->lock);

  if (!finishedVideos.empty()) {
    for (Kotonoha_VideoData *finished : finishedVideos) {
      for (auto it = here->videos.begin(); it != here->videos.end(); ++it) {
        if (*it == finished) {
          here->videos.erase(it);
          break;
        }
      }
    }
  }

  const bool isEmpty = here->videos.empty();

  if (returnStatus == KOTONOHA_SCENE_DRAW && here->heldTexture != nullptr) {
    SDL_DestroyTexture(here->heldTexture);
    here->heldTexture = nullptr;
  }

  if (returnStatus != KOTONOHA_SCENE_DRAW && here->heldTexture != nullptr) {
    here->RenderTexture(render, here->heldTexture);
    returnStatus = KOTONOHA_SCENE_DRAW;
  }

  SDL_UnlockMutex(here->lock);

  for (Kotonoha_VideoData *video : finishedVideos) {
    KotonohaVideoRenderShutdown(&video);
  }

  // Vídeos futuros ainda não iniciados não devem manter o último frame do
  // vídeo anterior cobrindo as camadas inferiores.
  if (returnStatus == KOTONOHA_SCENE_NULL || (isEmpty &&
      returnStatus != KOTONOHA_SCENE_DRAW &&
      returnStatus != KOTONOHA_SCENE_DRAW_LAST)) {
    return KOTONOHA_SCENE_COMPLETE;
  }

  return returnStatus;
}

void Video::Reset() {
  ResetUpscaling();

  std::vector<Kotonoha_VideoData *> oldVideos;

  if (lock != nullptr) {
    SDL_LockMutex(lock);
  }

  for (Kotonoha_VideoData *v : videos) {
    if (v != nullptr && v->texture != nullptr) {
      if (heldTexture != nullptr) {
        SDL_DestroyTexture(heldTexture);
      }
      heldTexture = v->texture;
      v->texture = nullptr;
      break;
    }
  }

  oldVideos.swap(videos);

  if (lock != nullptr) {
    SDL_UnlockMutex(lock);
  }

  for (Kotonoha_VideoData *video : oldVideos) {
    KotonohaVideoRenderShutdown(&video);
  }

  oldVideos.clear();
}

Video::~Video() {
  Reset();

  if (heldTexture != nullptr) {
    SDL_DestroyTexture(heldTexture);
    heldTexture = nullptr;
  }

  if (lock != nullptr) {
    SDL_DestroyMutex(lock);
    lock = nullptr;
  }
}

} // namespace Kotonoha