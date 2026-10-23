#include <Kotonoha/components/Image.hpp>
#include <Kotonoha/SchoolDaysAbcTexture.hpp>
#include <Kotonoha/SchoolDaysImageAssetResolver.hpp>
#include <Kotonoha/SceneTime.h>
#include <SDL3/SDL_render.h>
#include <algorithm>
#include <cctype>
#include <map>

namespace Kotonoha {

namespace {

std::string Lower(std::string value) {
  std::transform(value.begin(), value.end(), value.begin(),
                 [](unsigned char c) {
                   return static_cast<char>(std::tolower(c));
                 });
  return value;
}

bool IsHttpPath(const char *path) {
  return path != nullptr && (SDL_strncasecmp(path, "http://", 7) == 0 ||
                             SDL_strncasecmp(path, "https://", 8) == 0);
}

void ReleaseImageLoad(Kotonoha_ImageLoad *load) {
  if (load == nullptr) {
    return;
  }

  if (load->thread != nullptr) {
    SDL_WaitThread(load->thread, nullptr);
    load->thread = nullptr;
  }

  KotonohaImageReleaseFrame(&load->frame, &load->buffer);
  load->path = nullptr;
  load->failed = false;
  load->missingIgnored = false;
  SDL_SetAtomicInt(&load->state, 0);
}

bool DecodeLazyTexture(SDL_Renderer *renderer, const char *path,
                       Kotonoha_ImageLoad *load, SDL_Texture **texture,
                       bool ignoreMissingAssets, bool prepareLinearAbc) {
  if (renderer == nullptr || path == nullptr || *path == '\0' ||
      load == nullptr || texture == nullptr) {
    return false;
  }

  if (*texture != nullptr || load->failed) {
    return *texture != nullptr;
  }

  load->path = path;
  load->missingIgnored = false;
  SDL_SetAtomicInt(&load->state, 1);

  load->frame = KotonohaImageDecodeFrame(path, -1, -1, &load->buffer,
                                         ignoreMissingAssets);
  if (load->frame != nullptr) {
    if (prepareLinearAbc) {
      PrepareStraightAlphaForLinearFiltering(
          load->frame->data[0], load->frame->width, load->frame->height,
          load->frame->linesize[0]);
    }

    *texture = KotonohaImageCreateTextureFromFrame(renderer, load->frame);
    KotonohaImageReleaseFrame(&load->frame, &load->buffer);

    if (*texture != nullptr && prepareLinearAbc &&
        (!SDL_SetTextureBlendMode(*texture, SDL_BLENDMODE_BLEND) ||
         !SDL_SetTextureScaleMode(*texture, SDL_SCALEMODE_LINEAR))) {
      SDL_LogError(SDL_LOG_CATEGORY_RENDER,
                   "Failed to configure linear ABC texture %s: %s", path,
                   SDL_GetError());
      SDL_DestroyTexture(*texture);
      *texture = nullptr;
    }

    load->failed = *texture == nullptr;
  } else {
    load->missingIgnored = ignoreMissingAssets;
    load->failed = true;
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

void ClearTransparentLayer(SDL_Renderer *renderer, bool &cleaned) {
  if (renderer == nullptr || cleaned) {
    return;
  }

  Uint8 r = 0, g = 0, b = 0, a = 0;
  SDL_GetRenderDrawColor(renderer, &r, &g, &b, &a);
  SDL_SetRenderDrawColor(renderer, 0, 0, 0, 0);
  SDL_RenderClear(renderer);
  SDL_SetRenderDrawColor(renderer, r, g, b, a);
  cleaned = true;
}

} // namespace

Image::AbcGroup::~AbcGroup() {
  for (size_t index = 0; index < 3; ++index) {
    ReleaseImageLoad(&loads[index]);
    if (textures[index] != nullptr) {
      SDL_DestroyTexture(textures[index]);
      textures[index] = nullptr;
    }
  }
}

void Image::DrawAbcForBase(Kotonoha_Picture *picture, SDL_Renderer *renderer,
                           Uint64 atMs) {
  if (picture == nullptr || renderer == nullptr || picture->id != 0 ||
      picture->texture == nullptr) {
    return;
  }

  for (const auto &item : abcGroups) {
    auto *group = item.get();
    if (group == nullptr || !group->bound ||
        group->ownerBaseGeneration == 0 ||
        group->ownerBaseGeneration != activeBaseGeneration ||
        group->ownerBaseGeneration != picture->baseGeneration ||
        atMs < group->startTime || atMs >= group->endTime ||
        atMs < group->bindingStartTime || atMs >= group->bindingEndTime ||
        group->activeIndex > 2) {
      continue;
    }

    const Uint8 index = group->activeIndex;
    const std::string &path = group->resources.paths[index];
    if (path.empty()) {
      continue;
    }

    if (group->textures[index] == nullptr) {
      DecodeLazyTexture(renderer, path.c_str(), &group->loads[index],
                        &group->textures[index], ignoreMissingAssets, true);
    }

    if (group->textures[index] != nullptr) {
      SDL_RenderTexture(renderer, group->textures[index], nullptr, nullptr);
    }
  }
}

Image::Image(Kotonoha_time *time) : Image(time, false) {}

Image::Image(Kotonoha_time *time, bool ignoreMissing)
    : timeManager(time), ignoreMissingAssets(ignoreMissing),
      lock(SDL_CreateMutex()) {
  if (lock == nullptr) {
    SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                 "Failed to create image mutex: %s", SDL_GetError());
  }
}

	Uint64 Image::Register(const char* path, Uint64 startTime, Uint64 endTime,
		Uint8 id) {
		if (path == nullptr || *path == '\0') {
			SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
				"Invalid image path.");
			return 0;
		}

			Kotonoha_Picture* object = new Kotonoha_Picture{};
		object->path = SDL_strdup(path);
		object->texture = nullptr;
		object->startTime = startTime;
		object->endTime = endTime;
		object->lastTime = 0;
		object->id = id;
		object->canRender = true;
		object->baseGeneration = 0;

		if (object->path == nullptr) {
			SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
				"Failed to duplicate image path.");
			delete object;
			return 0;
		}

		if (lock == nullptr) {
			DestroyPicture(object);
			return 0;
		}

		SDL_LockMutex(lock);
		object->baseGeneration = nextBaseGeneration++;
		const Uint64 generation = object->baseGeneration;
		pictures.push_back(object);
		if (id == 0)
			DiscoverAbcGroups(path, startTime, endTime, generation);
		SDL_UnlockMutex(lock);
		return generation;
	}

		void Image::DiscoverAbcGroups(const char* basePath, Uint64 startTime,
			Uint64 endTime, Uint64 ownerBaseGeneration) {
			if (basePath == nullptr || IsHttpPath(basePath)) return;
		const std::string full = basePath;
		const size_t separator = full.find_last_of("/\\");
		const std::string directory = separator == std::string::npos
			? "." : full.substr(0, separator);
		const std::string name = full.substr(separator == std::string::npos
			? 0 : separator + 1);
		if (name.size() < 5 || Lower(name.substr(name.size() - 4)) != ".png")
			return;
		const std::string stem = name.substr(0, name.size() - 4);
			std::map<std::string, SchoolDaysAbcGroupResources> groups;
			const auto consider = [&](const std::string& resourcePath) {
				const size_t slash = resourcePath.find_last_of("/\\");
				const std::string child = resourcePath.substr(
					slash == std::string::npos ? 0 : slash + 1);
				const std::string lower = Lower(child);
				const std::string prefix = Lower(stem);
				if (child.size() <= stem.size() + 6 ||
					lower.compare(0, prefix.size(), prefix) != 0 ||
					lower.substr(lower.size() - 4) != ".png") return;
				const size_t statePos = lower.size() - 6;
				if (lower[statePos] != '.' ||
					(lower[statePos + 1] < 'a' || lower[statePos + 1] > 'c')) return;
				const std::string key = lower.substr(prefix.size(), statePos - prefix.size());
				if (key.empty()) return;
				auto& group = groups[key];
				group.baseStem = stem;
				group.key = key;
				AssignSchoolDaysAbcResource(
					group, static_cast<uint8_t>(lower[statePos + 1] - 'a'), resourcePath);
			};

			if (SchoolDaysAssetReferenceIsVirtual(directory)) {
				std::vector<std::string> children;
				std::string listError;
				if (ListSchoolDaysAssetDirectoryReference(directory, &children, &listError)) {
					for (const auto& child : children) consider(child);
				} else if (!listError.empty()) {
					SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
						"[SD-GPK] ABC directory listing failed for %s: %s",
						directory.c_str(), listError.c_str());
				}
			} else {
				struct ScanNames { std::vector<std::string> names; } scan;
				SDL_EnumerateDirectory(directory.c_str(),
					[](void* user, const char*, const char* filename) -> SDL_EnumerationResult {
						auto* scan = static_cast<ScanNames*>(user);
						if (filename != nullptr) scan->names.emplace_back(filename);
						return SDL_ENUM_CONTINUE;
					}, &scan);
				for (const auto& child : scan.names) consider(directory + "/" + child);
			}
			for (auto& pair : groups) {
			if (pair.second.collision) continue;
			std::unique_ptr<AbcGroup> group(new AbcGroup());
			group->resources = std::move(pair.second);
			group->ownerBaseGeneration = ownerBaseGeneration;
			group->startTime = startTime;
			group->endTime = endTime;
			abcGroups.push_back(std::move(group));
		}
	}

	bool Image::ActivateBase(Uint64 baseGeneration) {
		if (lock == nullptr || baseGeneration == 0) return false;
		SDL_LockMutex(lock);
		bool found = false;
		for (const auto* picture : pictures)
			if (picture != nullptr &&
				picture->baseGeneration == baseGeneration) { found = true; break; }
		if (found && activeBaseGeneration != baseGeneration) {
			for (auto& group : abcGroups) {
				group->bound = false;
				group->activeIndex = 0;
			}
			activeBaseGeneration = baseGeneration;
		}
		SDL_UnlockMutex(lock);
		return found;
	}

	Uint64 Image::ActiveBaseGeneration() const {
		if (lock == nullptr) return 0;
		SDL_LockMutex(lock);
		const Uint64 result = activeBaseGeneration;
		SDL_UnlockMutex(lock);
		return result;
	}

	bool Image::IsActiveGroup(const AbcGroup* group, Uint64 atMs) {
		if (lock == nullptr || group == nullptr) return false;
		SDL_LockMutex(lock);
		const bool active = group->bound &&
			group->ownerBaseGeneration == activeBaseGeneration &&
			group->startTime <= atMs && atMs < group->endTime &&
			group->bindingStartTime <= atMs && atMs < group->bindingEndTime;
		SDL_UnlockMutex(lock);
		return active;
	}

	Image::AbcGroup* Image::FindAbcGroup(const std::string& key, Uint64 atMs) {
		if (lock == nullptr || key.empty()) return nullptr;
		SDL_LockMutex(lock);
		AbcGroup* found = nullptr;
		for (const auto& group : abcGroups)
			if (group->ownerBaseGeneration == activeBaseGeneration &&
				group->resources.key == Lower(key) &&
				group->startTime <= atMs && atMs < group->endTime)
				found = group.get();
		SDL_UnlockMutex(lock);
		return found;
	}

	bool Image::BindAbcGroup(AbcGroup* group, Uint64 ownerBaseGeneration,
		Uint64 voiceStartMs, Uint64 voiceEndMs) {
		if (lock == nullptr || group == nullptr || voiceStartMs >= voiceEndMs)
			return false;
		SDL_LockMutex(lock);
		const bool eligible = ownerBaseGeneration != 0 &&
			ownerBaseGeneration == activeBaseGeneration &&
			group->ownerBaseGeneration == ownerBaseGeneration;
		if (eligible) {
			group->activeIndex = 0;
			group->bindingStartTime = voiceStartMs;
			group->bindingEndTime = voiceEndMs;
			group->bound = true;
		}
		SDL_UnlockMutex(lock);
		return eligible;
	}

	void Image::UnbindAbcGroup(AbcGroup* group) {
		if (lock == nullptr || group == nullptr) return;
		SDL_LockMutex(lock);
		group->bound = false;
		group->activeIndex = 0;
		SDL_UnlockMutex(lock);
	}

		void Image::SelectAbcState(AbcGroup* group, Uint8 index) {
		if (lock == nullptr || group == nullptr || index > 2) return;
		SDL_LockMutex(lock);
		if (!group->resources.paths[index].empty()) group->activeIndex = index;
		SDL_UnlockMutex(lock);
	}

void Image::RenderTexture(SDL_Renderer *render, SDL_Texture *target,
                          Kotonoha_Picture *picture, bool &cleaned) {
  if (render == nullptr || picture == nullptr || picture->texture == nullptr) {
    return;
  }

#if defined(KOTONOHA_HAS_ANIME4K) || defined(KOTONOHA_HAS_METALFX)
  const float factor = KotonohaUpscalingGetFactor();
  if (factor > 1.0f && IsUpscalingActive()) {
    float pictureWidth = 0.0f;
    float pictureHeight = 0.0f;
    if (SDL_GetTextureSize(picture->texture, &pictureWidth, &pictureHeight)) {
      const int requiredWidth = static_cast<int>(pictureWidth);
      const int requiredHeight = static_cast<int>(pictureHeight);
      if (requiredWidth > 0 && requiredHeight > 0 &&
          EnsureUpscalingTextures(render, requiredWidth, requiredHeight,
                                  factor)) {
        SDL_Rect originalViewport{};
        SDL_Rect originalClip{};
        float originalScaleX = 1.0f;
        float originalScaleY = 1.0f;
        Uint8 r = 0, g = 0, b = 0, a = 0;
        const bool viewportEnabled = SDL_RenderViewportSet(render);
        const bool clipEnabled = SDL_RenderClipEnabled(render);

        SDL_GetRenderViewport(render, &originalViewport);
        SDL_GetRenderClipRect(render, &originalClip);
        SDL_GetRenderScale(render, &originalScaleX, &originalScaleY);
        SDL_GetRenderDrawColor(render, &r, &g, &b, &a);

        if (SDL_SetRenderTarget(render, extraTexture)) {
          SDL_SetRenderViewport(render, nullptr);
          SDL_SetRenderClipRect(render, nullptr);
          SDL_SetRenderScale(render, 1.0f, 1.0f);
          SDL_SetRenderDrawColor(render, 0, 0, 0, 0);
          SDL_RenderClear(render);

          const SDL_FRect destination = {0.0f, 0.0f, pictureWidth,
                                         pictureHeight};
          SDL_RenderTexture(render, picture->texture, nullptr, &destination);
          if (picture->id == 0) {
            DrawAbcForBase(picture, render, Kotonoha_timeGet(timeManager));
          }

          const bool upscaled = ProcessUpscale(render, extraTexture);

          // Upscaler backends are allowed to change the render target. Canvas
          // owns this target, so restore it explicitly before drawing output.
          SDL_SetRenderTarget(render, target);
          SDL_SetRenderViewport(render,
                                viewportEnabled ? &originalViewport : nullptr);
          SDL_SetRenderClipRect(render, clipEnabled ? &originalClip : nullptr);
          SDL_SetRenderScale(render, originalScaleX, originalScaleY);
          SDL_SetRenderDrawColor(render, r, g, b, a);

          ClearTransparentLayer(render, cleaned);
          SDL_RenderTexture(render, upscaled ? upScaledTexture : extraTexture,
                            nullptr, nullptr);
          return;
        }

        SDL_SetRenderTarget(render, target);
        SDL_SetRenderViewport(render,
                              viewportEnabled ? &originalViewport : nullptr);
        SDL_SetRenderClipRect(render, clipEnabled ? &originalClip : nullptr);
        SDL_SetRenderScale(render, originalScaleX, originalScaleY);
        SDL_SetRenderDrawColor(render, r, g, b, a);
      }
    }
  }
#endif

  ClearTransparentLayer(render, cleaned);
  SDL_RenderTexture(render, picture->texture, nullptr, nullptr);
  if (picture->id == 0) {
    DrawAbcForBase(picture, render, Kotonoha_timeGet(timeManager));
  }
}

Kotonoha_Scene_Status Image::RenderCallback(KOTONOHA_SCENE_CALL) {
  Kotonoha_Scene_Status status = KOTONOHA_SCENE_NULL;
  auto *here = static_cast<Image *>(userData);
  if (here == nullptr || render == nullptr || here->timeManager == nullptr ||
      here->lock == nullptr) {
    return status;
  }

  bool cleaned = false;
  const Uint64 nowMs = Kotonoha_timeGet(here->timeManager);
  const Kotonoha_SceneTick sceneTick =
      Kotonoha_MillisecondsToSceneTick(nowMs);

  SDL_LockMutex(here->lock);
  for (auto it = here->pictures.begin(); it != here->pictures.end();) {
    Kotonoha_Picture *picture = *it;
    if (picture == nullptr) {
      it = here->pictures.erase(it);
      continue;
    }

    const Kotonoha_SceneTick endTick =
        Kotonoha_MillisecondsToSceneTick(picture->endTime);
    if (picture->path == nullptr ||
        here->activeBaseGeneration != picture->baseGeneration) {
      if (sceneTick >= endTick) {
        it = here->pictures.erase(it);
        DestroyPicture(picture);
        continue;
      }
      ++it;
      continue;
    }

    Sint64 diff = 0;
    bool inRange = false;
    const Uint64 current = Kotonoha_timeGetFromEvent(
        here->timeManager, picture->startTime, picture->endTime, &inRange,
        &diff);

    if (picture->texture == nullptr) {
      DecodeLazyTexture(render, picture->path, &picture->load,
                        &picture->texture, here->ignoreMissingAssets, false);
      if (picture->texture == nullptr) {
        if (sceneTick >= endTick) {
          it = here->pictures.erase(it);
          DestroyPicture(picture);
          continue;
        }
        if (status != KOTONOHA_SCENE_DRAW &&
            status != KOTONOHA_SCENE_DRAW_LAST) {
          status = KOTONOHA_SCENE_WAITING;
        }
        ++it;
        continue;
      }

      // Decoded RGBA is straight alpha; Canvas target is transparent RGBA.
      if (!SDL_SetTextureBlendMode(picture->texture, SDL_BLENDMODE_BLEND)) {
        SDL_LogError(SDL_LOG_CATEGORY_RENDER,
                     "Failed to set image texture blend mode: %s",
                     SDL_GetError());
        SDL_DestroyTexture(picture->texture);
        picture->texture = nullptr;
        picture->load.failed = true;
        ++it;
        continue;
      }
    }

    if (sceneTick >= endTick) {
      // Draw the final base on a freshly cleared transparent layer. The ABC
      // voice/base windows are half-open, so no mouth overlay survives here.
      here->RenderTexture(render, target, picture, cleaned);
      status = picture->id > 0 ? status : KOTONOHA_SCENE_DRAW_LAST;

      Kotonoha_Picture *toDestroy = picture;
      it = here->pictures.erase(it);
      DestroyPicture(toDestroy);
      continue;
    }

    if (!inRange) {
      if (status != KOTONOHA_SCENE_DRAW &&
          status != KOTONOHA_SCENE_DRAW_LAST) {
        status = KOTONOHA_SCENE_WAITING;
      }
      ++it;
      continue;
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
#if defined(KOTONOHA_HAS_ANIME4K) || defined(KOTONOHA_HAS_METALFX)
  ResetUpscaling();
#endif

  std::vector<Kotonoha_Picture *> oldPictures;
  if (lock != nullptr) {
    SDL_LockMutex(lock);
  }

  activeBaseGeneration = 0;
  abcGroups.clear();
  oldPictures.swap(pictures);

  if (lock != nullptr) {
    SDL_UnlockMutex(lock);
  }

  for (auto *picture : oldPictures) {
    DestroyPicture(picture);
  }
}

std::vector<MediaBufferInfo> Image::GetBufferInfo() const {
  std::vector<MediaBufferInfo> result;
  if (lock == nullptr || timeManager == nullptr) return result;

  SDL_LockMutex(lock);
  const Uint64 currentTime = KotonohaTimeGet(timeManager);
  for (const Kotonoha_Picture *picture : pictures) {
    if (picture == nullptr) continue;
    MediaBufferInfo info;
    info.id = reinterpret_cast<Uint64>(picture);
    info.type = "Image";
    if (picture->path != nullptr) info.path = picture->path;
    info.texture = picture->texture;

    const bool pictureActive =
        currentTime >= picture->startTime && currentTime < picture->endTime;
    if (picture->id == 0) {
      for (const auto &group : abcGroups) {
        if (!group || group->ownerBaseGeneration != picture->baseGeneration ||
            !pictureActive || !group->bound || currentTime < group->startTime ||
            currentTime >= group->endTime ||
            currentTime < group->bindingStartTime ||
            currentTime >= group->bindingEndTime || group->activeIndex > 2)
          continue;
        SDL_Texture *overlay = group->textures[group->activeIndex];
        if (overlay != nullptr)
          info.overlays.push_back(
              {group->resources.paths[group->activeIndex], overlay});
      }
    }
    result.push_back(std::move(info));
  }
  SDL_UnlockMutex(lock);
  return result;
}

	Image::~Image() {
		Reset();

		if (lock != nullptr) {
			SDL_DestroyMutex(lock);
			lock = nullptr;
		}
	}

} // namespace Kotonoha
