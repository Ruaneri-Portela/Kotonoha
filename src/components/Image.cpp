#include <Kotonoha/components/Image.hpp>

namespace Kotonoha {

Image::Image(Kotonoha_time *time) : timeManager(time) {
  lock = SDL_CreateMutex();
}

void Image::Register(const char *path, Uint64 startTime, Uint64 endTime,
                     Uint8 id) {
  // Criar um novo objeto Kotonoha_Picture
  Kotonoha_Picture *object = new Kotonoha_Picture();
  object->path = SDL_strdup(path); // Definir o caminho da imagem
  object->texture = NULL;
  object->startTime = startTime; // Definir o tempo de in�cio
  object->endTime = endTime;     // Definir o tempo de t�rmino
  object->lastTime = 0;          // Definir o �ltimo tempo
  object->id = id;               // Definir o id
  object->canRender = true;      // Definir se pode renderizar

  // Adicionar o objeto � lista de imagens
  SDL_LockMutex(lock);
  pictures.push_back(object);
  SDL_UnlockMutex(lock);
}

enum Kotonoha_Scene_Status Image::Render(KOTONOHA_SCENE_CALL) {
  auto *here = static_cast<Image *>(userData);
  Kotonoha_Scene_Status status = KOTONOHA_SCENE_COMPLETE;

  bool cleanuped = false;
  bool drawed = false;

  SDL_LockMutex(here->lock);
  for (auto it = here->pictures.begin(); it != here->pictures.end();) {
    auto *picture = *it;
    Sint64 diff = 0;
    bool inRange = false;

    Uint64 current =
        Kotonoha_timeGetFromEvent(here->timeManager, picture->startTime,
                                  picture->endTime, &inRange, &diff);
    if (picture->texture == NULL) {
      picture->texture =
          Kotonoha_imageCreateTexture(render, picture->path, -1, -1);
      SDL_SetTextureBlendMode(picture->texture,
                              SDL_BLENDMODE_BLEND_PREMULTIPLIED);
    }

    // Remover imagem se tempo de exibi��o terminou
    if (diff > 0) {
      SDL_RenderTexture(render, picture->texture, nullptr, nullptr);
      status = KOTONOHA_SCENE_DRAW_LAST;
      SDL_DestroyTexture(picture->texture);
      it = here->pictures.erase(it);
      continue;
    }

    // Atualizar status se imagem n�o est� no intervalo de tempo
    if (!inRange) {
      status =
          (status == KOTONOHA_SCENE_DRAW || status == KOTONOHA_SCENE_DRAW_LAST
               ? status
               : KOTONOHA_SCENE_WAITING);
      ++it;
      continue;
    }

    // Alternar exibi��o para id espec�fico em intervalo de tempo
    // Movimento de chunks de imagem
    {
      if (picture->id == 1 && (current - picture->lastTime) > 750) {
        picture->canRender = !picture->canRender;
      }

      if (!picture->canRender) {
        ++it;
        continue;
      }
    }

    if (!cleanuped) {
      SDL_RenderClear(render);
      cleanuped = true;
    }
    SDL_RenderTexture(render, picture->texture, nullptr, nullptr);
    status = KOTONOHA_SCENE_DRAW;
    picture->lastTime = current;
    ++it;
  }
  SDL_UnlockMutex(here->lock);
  return status;
}

void Image::Reset() {
  SDL_LockMutex(lock);
  for (auto *picture : pictures) {
    SDL_DestroyTexture(picture->texture);
    SDL_free(picture->path);
    delete picture;
  }
  pictures.clear();
  SDL_UnlockMutex(lock);
}

Image::~Image() {
  Reset();
  SDL_DestroyMutex(lock);
}

} // namespace Kotonoha
