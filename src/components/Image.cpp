#include <Kotonoha/components/Image.hpp>
#include <Kotonoha/speciais/Upscaling.h>
#include <algorithm>
#include <cctype>

namespace Kotonoha {

namespace {

std::string Lower(std::string value) {
  std::transform(
      value.begin(), value.end(), value.begin(),
      [](unsigned char c) { return static_cast<char>(std::tolower(c)); });

  return value;
}

bool IsHttpPath(const char *path) {
  return path != nullptr && (SDL_strncasecmp(path, "http://", 7) == 0 ||
                             SDL_strncasecmp(path, "https://", 8) == 0);
}

std::string BuildDeterministicAbcPath(const char *basePath,
                                      const std::string &key, char state) {
  if (basePath == nullptr || key.empty() ||
      key.find_first_of("/\\?#") != std::string::npos) {
    return {};
  }

  std::string path(basePath);

  const size_t suffixStart = path.find_first_of("?#");
  const size_t pathEnd =
      suffixStart == std::string::npos ? path.size() : suffixStart;

  if (pathEnd < 4 || Lower(path.substr(pathEnd - 4, 4)) != ".png") {
    return {};
  }

  path.insert(pathEnd - 4, "." + key + "." + state);

  return path;
}

void ReleaseImageLoad(Kotonoha_ImageLoad *load) {
  if (load == nullptr) {
    return;
  }

  KotonohaImageReleaseFrame(&load->frame, &load->buffer);

  load->path = nullptr;
  load->failed = false;
  load->missingIgnored = false;

  SDL_SetAtomicInt(&load->state, 0);
}

bool PrepareImageTexture(SDL_Renderer *renderer, const char *path,
                         Kotonoha_ImageLoad *load, SDL_Texture **texture,
                         bool ignoreMissingAssets) {
  if (renderer == nullptr || path == nullptr || load == nullptr ||
      texture == nullptr) {
    SDL_LogError(SDL_LOG_CATEGORY_RENDER,
                 "Invalid arguments for image loading");
    return false;
  }

  if (*texture != nullptr || load->failed) {
    return *texture != nullptr;
  }

  // Decodificação síncrona direta sem threads
  load->path = path;
  SDL_SetAtomicInt(&load->state, 1);

  load->frame = KotonohaImageDecodeNativeFrame(load->path, ignoreMissingAssets);

  if (load->frame != nullptr) {
    *texture = KotonohaImageCreateTextureFromFrame(renderer, load->frame);
    KotonohaImageReleaseFrame(&load->frame, &load->buffer);

    if (*texture == nullptr) {
      load->failed = true;
    }
  } else {
    load->failed = true;
    load->missingIgnored = ignoreMissingAssets;
  }

  SDL_SetAtomicInt(&load->state, 0);

  return *texture != nullptr;
}

void DestroyPicture(Kotonoha_Picture *picture) {
  if (picture == nullptr) {
    return;
  }

  ReleaseImageLoad(&picture->load);

  if (picture->texture != nullptr) {
    SDL_DestroyTexture(picture->texture);
    picture->texture = nullptr;
  }

  if (picture->path != nullptr) {
    SDL_free(picture->path);
    picture->path = nullptr;
  }

  delete picture;
}

bool ClearTransparent(SDL_Renderer *renderer) {
  Uint8 drawR = 0;
  Uint8 drawG = 0;
  Uint8 drawB = 0;
  Uint8 drawA = 0;
  if (!SDL_GetRenderDrawColor(renderer, &drawR, &drawG, &drawB, &drawA)) {
    SDL_LogError(SDL_LOG_CATEGORY_RENDER,
                 "Failed to save renderer draw color: %s", SDL_GetError());
    return false;
  }

  const bool cleared = SDL_SetRenderDrawColor(renderer, 0, 0, 0, 0) &&
                       SDL_RenderClear(renderer);
  const bool restored =
      SDL_SetRenderDrawColor(renderer, drawR, drawG, drawB, drawA);
  if (!cleared || !restored) {
    SDL_LogError(SDL_LOG_CATEGORY_RENDER,
                 "Failed to clear image layer transparently: %s",
                 SDL_GetError());
    return false;
  }
  return true;
}

bool RenderTextureToCanvasTarget(SDL_Renderer *renderer, SDL_Texture *texture,
                                 SDL_Texture *target) {
  float width = 0.0f;
  float height = 0.0f;
  if (renderer == nullptr || texture == nullptr || target == nullptr ||
      !SDL_GetTextureSize(target, &width, &height) || width <= 0.0f ||
      height <= 0.0f) {
    SDL_LogError(SDL_LOG_CATEGORY_RENDER,
                 "Invalid image or canvas target for rendering: %s",
                 SDL_GetError());
    return false;
  }

  const SDL_FRect destination{0.0f, 0.0f, width, height};
  if (!SDL_RenderTexture(renderer, texture, nullptr, &destination)) {
    SDL_LogError(SDL_LOG_CATEGORY_RENDER,
                 "Failed to render image texture to canvas target: %s",
                 SDL_GetError());
    return false;
  }
  return true;
}

} // namespace

AbcGroup::~AbcGroup() {
  for (size_t index = 0; index < 3; ++index) {
    ReleaseImageLoad(&loads[index]);

    if (textures[index] != nullptr) {
      SDL_DestroyTexture(textures[index]);
      textures[index] = nullptr;
    }
  }
}

Image::Image(Kotonoha_Time *time, bool ignoreMissingAssets)
    : timeManager(time), ignoreMissingAssets(ignoreMissingAssets),
      lock(nullptr) {
  lock = SDL_CreateMutex();

  if (lock == nullptr) {
    SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                 "Failed to create image mutex: %s", SDL_GetError());
  }
}

Image::~Image() {
  Reset();
  if (lock != nullptr) {
    SDL_DestroyMutex(lock);
    lock = nullptr;
  }
}

void Image::DiscoverAbcGroups(const char *basePath, Uint64 startTime,
                              Uint64 endTime, Uint64 ownerBaseGeneration) {
  if (basePath == nullptr || IsHttpPath(basePath)) {
    return;
  }

  const std::string fullPath = basePath;

  const size_t separator = fullPath.find_last_of("/\\");
  const std::string directory =
      separator == std::string::npos ? "." : fullPath.substr(0, separator);

  const std::string filename =
      fullPath.substr(separator == std::string::npos ? 0 : separator + 1);

  const std::string lowerFilename = Lower(filename);

  if (lowerFilename.size() <= 4 ||
      lowerFilename.compare(lowerFilename.size() - 4, 4, ".png") != 0) {
    return;
  }

  const std::string stem = filename.substr(0, filename.size() - 4);

  struct Scan {
    std::string stem;
    std::string directory;
    std::vector<std::unique_ptr<AbcGroup>> groups;
  };

  Scan scan{stem, directory, {}};

  if (!SDL_EnumerateDirectory(
          directory.c_str(),
          [](void *opaque, const char *, const char *entry) {
            auto *scan = static_cast<Scan *>(opaque);

            const std::string name(entry);
            const std::string lower = Lower(name);
            const std::string lowerStem = Lower(scan->stem);

            if (lower.size() <= lowerStem.size() + 6 ||
                lower.compare(0, lowerStem.size(), lowerStem) != 0 ||
                lower.compare(lower.size() - 4, 4, ".png") != 0) {
              return SDL_ENUM_CONTINUE;
            }

            const size_t statePosition = lower.size() - 6;

            if (lower[statePosition] != '.' || lower[statePosition + 1] < 'a' ||
                lower[statePosition + 1] > 'c') {
              return SDL_ENUM_CONTINUE;
            }

            const std::string key = lower.substr(
                lowerStem.size(), statePosition - lowerStem.size());

            if (key.empty()) {
              return SDL_ENUM_CONTINUE;
            }

            AbcGroup *group = nullptr;

            for (auto &candidate : scan->groups) {
              if (candidate->key == key) {
                group = candidate.get();
                break;
              }
            }

            if (group == nullptr) {
              scan->groups.emplace_back(std::make_unique<AbcGroup>());

              group = scan->groups.back().get();
              group->key = key;
            }

            const Uint8 state =
                static_cast<Uint8>(lower[statePosition + 1] - 'a');

            group->paths[state] = scan->directory + "/" + name;

            return SDL_ENUM_CONTINUE;
          },
          &scan)) {
    SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                "Failed to scan for A/B/C mouth overlays beside %s: %s",
                basePath, SDL_GetError());
  }

  for (auto &group : scan.groups) {
    group->ownerBaseGeneration = ownerBaseGeneration;
    group->startTime = startTime;
    group->endTime = endTime;

    abcGroups.emplace_back(std::move(group));
  }
}

void Image::DrawAbcForBase(Kotonoha_Picture *picture, SDL_Renderer *renderer,
                           Uint64 atMs) {
  if (picture == nullptr || renderer == nullptr ||
      picture->texture == nullptr || picture->id != 0) {
    return;
  }

  for (const auto &group : abcGroups) {
    if (!group) {
      continue;
    }

    if (group->ownerBaseGeneration != picture->baseGeneration) {
      continue;
    }

    if (!group->bound || atMs < group->startTime || atMs >= group->endTime ||
        atMs < group->bindingStartTime || atMs >= group->bindingEndTime) {
      continue;
    }

    const Uint8 index = group->activeIndex;

    if (index > 2) {
      continue;
    }

    const std::string &path = group->paths[index];

    if (path.empty()) {
      continue;
    }

    if (group->textures[index] == nullptr) {
      PrepareImageTexture(renderer, path.c_str(), &group->loads[index],
                          &group->textures[index], ignoreMissingAssets);

      if (group->textures[index] == nullptr) {
        continue;
      }

      if (!SDL_SetTextureBlendMode(group->textures[index],
                                   SDL_BLENDMODE_BLEND) ||
          !SDL_SetTextureScaleMode(group->textures[index],
                                   SDL_SCALEMODE_LINEAR)) {
        SDL_LogError(SDL_LOG_CATEGORY_RENDER,
                     "Failed to configure mouth overlay %s: %s", path.c_str(),
                     SDL_GetError());

        SDL_DestroyTexture(group->textures[index]);
        group->textures[index] = nullptr;

        continue;
      }
    }

    if (group->textures[index] == nullptr) {
      continue;
    }
    SDL_RenderTexture(renderer, group->textures[index], nullptr, nullptr);
  }
}

void Image::RemoveAbcGroupsForBase(Uint64 baseGeneration) {
  abcGroups.erase(
      std::remove_if(abcGroups.begin(), abcGroups.end(),
                     [baseGeneration](const std::unique_ptr<AbcGroup> &group) {
                       return group &&
                              group->ownerBaseGeneration == baseGeneration;
                     }),
      abcGroups.end());
}

Uint64 Image::Register(const char *path, Uint64 startTime, Uint64 endTime,
                       Uint8 id) {
  if (path == nullptr || *path == '\0') {
    SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Invalid image path.");
    return 0;
  }

  Kotonoha_Picture *object = new Kotonoha_Picture();

  object->path = SDL_strdup(path);
  object->texture = nullptr;
  object->startTime = startTime;
  object->endTime = endTime;
  object->lastTime = 0;
  object->id = id;
  object->canRender = true;
  object->baseGeneration = 0;
  object->load.frame = nullptr;
  object->load.buffer = nullptr;
  object->load.failed = false;

  if (object->path == nullptr) {
    SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                 "Failed to duplicate image path.");
    delete object;
    return 0;
  }

  // Pré-decodifica o frame da imagem em background se possível
  object->load.frame = KotonohaImageDecodeFrame(path, -1, -1, &object->load.buffer, ignoreMissingAssets);

  if (lock == nullptr) {
    DestroyPicture(object);
    return 0;
  }

  SDL_LockMutex(lock);

  if (id == 0) {
    object->baseGeneration = nextBaseGeneration++;
  }

  const Uint64 generation = object->baseGeneration;

  pictures.push_back(object);

  if (id == 0) {
    DiscoverAbcGroups(path, startTime, endTime, generation);
  }

  SDL_UnlockMutex(lock);

  return generation;
}

void Image::SetAbcState(const std::string &key, Uint8 index,
                        Uint64 voiceStartMs, Uint64 voiceEndMs) {
  if (lock == nullptr || key.empty() || index > 2 ||
      voiceStartMs >= voiceEndMs) {
    return;
  }

  const std::string normalizedKey = Lower(key);

  SDL_LockMutex(lock);

  for (const auto *picture : pictures) {
    if (picture == nullptr || picture->id != 0 || picture->path == nullptr ||
        voiceStartMs < picture->startTime || voiceStartMs >= picture->endTime) {
      continue;
    }

    bool groupExists = false;

    for (const auto &group : abcGroups) {
      if (group && group->ownerBaseGeneration == picture->baseGeneration &&
          group->key == normalizedKey) {
        groupExists = true;
        break;
      }
    }

    if (groupExists) {
      continue;
    }

    auto group = std::make_unique<AbcGroup>();

    group->key = normalizedKey;
    group->ownerBaseGeneration = picture->baseGeneration;
    group->startTime = picture->startTime;
    group->endTime = picture->endTime;

    for (size_t state = 0; state < 3; ++state) {
      group->paths[state] = BuildDeterministicAbcPath(
          picture->path, normalizedKey, static_cast<char>('a' + state));
    }

    abcGroups.emplace_back(std::move(group));
  }

  for (auto &group : abcGroups) {
    if (!group || group->key != normalizedKey ||
        voiceStartMs < group->startTime || voiceStartMs >= group->endTime) {
      continue;
    }

    group->bound = true;
    group->bindingStartTime = voiceStartMs;
    group->bindingEndTime = voiceEndMs;

    if (!group->paths[index].empty()) {
      group->activeIndex = index;
    }
  }

  SDL_UnlockMutex(lock);
}

void Image::RenderTexture(SDL_Renderer *render, SDL_Texture *target,
                          Kotonoha_Picture *picture, bool &cleaned) {
  if (!render || !picture || !picture->texture) {
    return;
  }

  if (!cleaned) {
    if (!ClearTransparent(render)) {
      return;
    }
    cleaned = true;
  }

  SDL_Texture *textureToRender = picture->texture;
  bool upscaled = false;
  const float factor = KotonohaUpscalingInitFactor();

  if (factor > 1.0f && IsUpscalingActive()) {
    float pictureWidth = 0.0f;
    float pictureHeight = 0.0f;

    if (!SDL_GetTextureSize(picture->texture, &pictureWidth, &pictureHeight)) {
      SDL_LogError(SDL_LOG_CATEGORY_RENDER,
                   "Failed to get picture texture size: %s", SDL_GetError());
      return;
    }

    const int requiredWidth = static_cast<int>(pictureWidth);
    const int requiredHeight = static_cast<int>(pictureHeight);

    if (requiredWidth <= 0 || requiredHeight <= 0) {
      SDL_LogError(SDL_LOG_CATEGORY_RENDER,
                   "Invalid picture texture size: %dx%d", requiredWidth,
                   requiredHeight);
      return;
    }

    if (!EnsureUpscalingTextures(render, requiredWidth, requiredHeight,
                                 factor)) {
      SDL_LogError(SDL_LOG_CATEGORY_RENDER,
                   "Failed to ensure upscaling textures for picture");
    } else if (!SDL_SetRenderTarget(render, extraTexture)) {
      SDL_LogWarn(SDL_LOG_CATEGORY_RENDER,
                  "Could not activate image upscaling target; drawing source "
                  "texture directly: %s",
                  SDL_GetError());
    } else if (!ClearTransparent(render)) {
      if (!SDL_SetRenderTarget(render, target)) {
        SDL_LogError(SDL_LOG_CATEGORY_RENDER,
                     "Failed to restore image render target: %s",
                     SDL_GetError());
      }
    } else {
      const SDL_FRect pictureDestination{0.0f, 0.0f, pictureWidth,
                                         pictureHeight};
      if (!SDL_RenderTexture(render, picture->texture, nullptr,
                             &pictureDestination)) {
        SDL_LogWarn(SDL_LOG_CATEGORY_RENDER,
                    "Could not copy image into upscaling target; drawing "
                    "source texture directly: %s",
                    SDL_GetError());
      } else {
        if (picture->id == 0) {
          DrawAbcForBase(picture, render, KotonohaTimeGet(timeManager));
        }
        if (!SDL_SetRenderTarget(render, target)) {
          SDL_LogError(SDL_LOG_CATEGORY_RENDER,
                       "Failed to restore image render target: %s",
                       SDL_GetError());
        } else if (ProcessUpscale(render, extraTexture)) {
          textureToRender = upScaledTexture;
          upscaled = true;
        }
      }

      if (SDL_GetRenderTarget(render) != target &&
          !SDL_SetRenderTarget(render, target)) {
        SDL_LogError(SDL_LOG_CATEGORY_RENDER,
                     "Failed to restore image render target: %s",
                     SDL_GetError());
        return;
      }
    }
  }

  if (!RenderTextureToCanvasTarget(render, textureToRender, target)) {
    return;
  }

  if (!upscaled && picture->id == 0) {
    DrawAbcForBase(picture, render, KotonohaTimeGet(timeManager));
  }
}

Kotonoha_Scene_Status Image::Render(KOTONOHA_SCENE_CALL) {
  Kotonoha_Scene_Status status = KOTONOHA_SCENE_NULL;

  auto *here = static_cast<Image *>(userData);

  if (here == nullptr || render == nullptr) {
    return status;
  }

  if (here->timeManager == nullptr || here->lock == nullptr) {
    return status;
  }

  bool cleaned = false;

  SDL_LockMutex(here->lock);

  for (auto it = here->pictures.begin(); it != here->pictures.end();) {
    Kotonoha_Picture *picture = *it;

    if (picture == nullptr) {
      it = here->pictures.erase(it);
      continue;
    }

    Sint64 diff = 0;
    bool inRange = false;

    const Uint64 current =
        KotonohaTimeGetFromEvent(here->timeManager, picture->startTime,
                                 picture->endTime, &inRange, &diff);

    if (picture->texture == nullptr) {
      if (!inRange && diff < -10000) {
        ++it;
        continue;
      }

      PrepareImageTexture(render, picture->path, &picture->load,
                          &picture->texture, here->ignoreMissingAssets);

      if (picture->texture == nullptr) {
        if (picture->load.failed && !picture->load.missingIgnored) {
          SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                       "Failed to create image texture: %s",
                       picture->path ? picture->path : "<null>");
        }

        ++it;
        continue;
      }

      if (!SDL_SetTextureBlendMode(picture->texture, SDL_BLENDMODE_BLEND)) {
        SDL_LogError(SDL_LOG_CATEGORY_RENDER,
                     "Failed to set image texture blend mode: %s",
                     SDL_GetError());

        SDL_DestroyTexture(picture->texture);
        picture->texture = nullptr;

        ++it;
        continue;
      }
    }

    if (diff > 0) {
      if (picture->id == 0) {
        here->RenderTexture(render, target, picture, cleaned);
        here->RemoveAbcGroupsForBase(picture->baseGeneration);

        if (status != KOTONOHA_SCENE_DRAW) {
          status = KOTONOHA_SCENE_DRAW_LAST;
        }
      }

      Kotonoha_Picture *toDestroy = picture;
      it = here->pictures.erase(it);
      DestroyPicture(toDestroy);

      continue;
    }

    if (!inRange) {
      if (status != KOTONOHA_SCENE_DRAW && status != KOTONOHA_SCENE_DRAW_LAST) {
        status = KOTONOHA_SCENE_WAITING;
      }

      ++it;
      continue;
    }

    if (picture->id == 1 && (current - picture->lastTime) > 750) {
      picture->canRender = !picture->canRender;
    }

    if (!picture->canRender) {
      picture->lastTime = current;
      ++it;
      continue;
    }

    here->RenderTexture(render, target, picture, cleaned);
    if (status != KOTONOHA_SCENE_DRAW_LAST) {
      status = KOTONOHA_SCENE_DRAW;
    }

    picture->lastTime = current;
    ++it;
  }

  SDL_UnlockMutex(here->lock);
  return status;
}

void Image::Reset() {
  ResetUpscaling();
  abcGroups.clear();

  std::vector<Kotonoha_Picture *> oldPictures;

  if (lock != nullptr) {
    SDL_LockMutex(lock);
  }

  oldPictures.swap(pictures);

  if (lock != nullptr) {
    SDL_UnlockMutex(lock);
  }

  for (auto *picture : oldPictures) {
    DestroyPicture(picture);
  }

  oldPictures.clear();
}

} // namespace Kotonoha