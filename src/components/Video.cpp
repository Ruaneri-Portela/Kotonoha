#include <Kotonoha/components/Video.hpp>

namespace Kotonoha {

Video::Video(Kotonoha_time *timeManager) : timeManager(timeManager) {
  lock = SDL_CreateMutex();
}

bool Video::Register(const char *path, Uint64 startTime, Uint64 endTime) {
  Kotonoha_videoData *object =
      Kotonoha_VideoRenderInit(path, timeManager, startTime, endTime);
  if (object == nullptr) {
    return false;
  }

  SDL_LockMutex(lock);
  videos.push_back(object);
  SDL_UnlockMutex(lock);
  return true;
}

Kotonoha_Scene_Status Video::Render(KOTONOHA_SCENE_CALL) {
  auto *here = static_cast<Video *>(userData);
  Kotonoha_Scene_Status returnStatus = KOTONOHA_SCENE_WAITING;

  SDL_LockMutex(here->lock);
  auto it = here->videos.begin();

  while (it != here->videos.end()) {
    Kotonoha_videoData *currentVideo = *it;

    Kotonoha_Scene_Status status =
        Kotonoha_VideoRenderProcess(currentVideo, render);

    // Processa conforme o status de cada vídeo
    switch (status) {
    case KOTONOHA_SCENE_DRAW:
      SDL_RenderTexture(render, currentVideo->texture, nullptr, nullptr);
      returnStatus = KOTONOHA_SCENE_DRAW;
      ++it; // Avança o iterador se o vídeo for desenhado
      break;

    case KOTONOHA_SCENE_COMPLETE:
      if (returnStatus != KOTONOHA_SCENE_DRAW) {
        SDL_RenderTexture(render, currentVideo->texture, nullptr, nullptr);
        returnStatus = KOTONOHA_SCENE_DRAW_LAST;
      }
      it = here->videos.erase(it); // Remove o vídeo e avança o iterador
      Kotonoha_VideoRenderShutdown(&currentVideo);
      break;

    default:
      ++it; // Avança o iterador para outros casos
      break;
    }
  }

  // Define returnStatus com base na lista de vídeos
  returnStatus =
      (here->videos.empty() && returnStatus != KOTONOHA_SCENE_DRAW_LAST)
          ? KOTONOHA_SCENE_COMPLETE
          : returnStatus;
  SDL_UnlockMutex(here->lock);
  return returnStatus; // Retorna o status final
}

void Video::Reset() {
  SDL_LockMutex(lock);
  for (auto &video : videos) {
    Kotonoha_VideoRenderShutdown(&video);
  }
  videos.clear();
  SDL_UnlockMutex(lock);
}

Video::~Video() { Reset(); }
} // namespace Kotonoha
