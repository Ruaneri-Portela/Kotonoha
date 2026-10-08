#include <Kotonoha/components/Image.hpp>
#include <Kotonoha/SchoolDaysAbcTexture.hpp>
#include <Kotonoha/SchoolDaysSceneTime.h>
#include <SDL3/SDL_render.h>
#include <algorithm>
#include <cctype>
#include <map>

namespace Kotonoha {

	namespace {
		static std::string Lower(std::string value) {
			std::transform(value.begin(), value.end(), value.begin(),
				[](unsigned char c) { return static_cast<char>(std::tolower(c)); });
			return value;
		}

		static void DestroyPicture(Kotonoha_Picture* picture) {
			if (picture == nullptr) {
				return;
			}

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

		static SDL_Texture* CreateAbcTexture(SDL_Renderer* renderer,
			const char* path) {
			SDL_Surface* staging = Kotonoha_imageCreateSurface(path, -1, -1);
			if (staging == nullptr) return nullptr;
			SDL_Texture* texture = nullptr;
			if (staging->format == SDL_PIXELFORMAT_RGBA32 &&
				SDL_LockSurface(staging)) {
				PrepareStraightAlphaForLinearFiltering(
					static_cast<Uint8*>(staging->pixels),
					staging->w, staging->h, staging->pitch);
				texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGBA32,
					SDL_TEXTUREACCESS_STATIC, staging->w, staging->h);
				if (texture != nullptr &&
					(!SDL_UpdateTexture(texture, nullptr,
						staging->pixels, staging->pitch) ||
					 !SDL_SetTextureBlendMode(texture, SDL_BLENDMODE_BLEND) ||
					 !SDL_SetTextureScaleMode(texture, SDL_SCALEMODE_LINEAR))) {
					SDL_DestroyTexture(texture);
					texture = nullptr;
				}
				SDL_UnlockSurface(staging);
			}
			SDL_DestroySurface(staging);
			return texture;
		}
	} // namespace

	void Image::DrawAbcForBase(Kotonoha_Picture* picture,
		SDL_Renderer* renderer, Uint64 atMs) {
		for (const auto& item : abcGroups) {
			auto* group = item.get();
			if (!group->bound || group->ownerBaseGeneration == 0 ||
				group->ownerBaseGeneration != activeBaseGeneration ||
				group->ownerBaseGeneration != picture->baseGeneration ||
				atMs < group->startTime || atMs >= group->endTime ||
				atMs < group->bindingStartTime ||
				atMs >= group->bindingEndTime || group->activeIndex > 2)
				continue;
			const Uint8 index = group->activeIndex;
			if (group->resources.paths[index].empty()) continue;
			if (group->textures[index] == nullptr) {
				group->textures[index] = CreateAbcTexture(renderer,
					group->resources.paths[index].c_str());
			}
			if (group->textures[index] != nullptr)
				SDL_RenderTexture(renderer, group->textures[index], nullptr, nullptr);
		}
	}

	Image::Image(Kotonoha_time* time) : timeManager(time), lock(nullptr) {
		lock = SDL_CreateMutex();
		if (lock == nullptr) {
			SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
				"Failed to create image mutex: %s",
				SDL_GetError());
		}
	}

	Uint64 Image::Register(const char* path, Uint64 startTime, Uint64 endTime,
		Uint8 id) {
		if (path == nullptr || *path == '\0') {
			SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
				"Invalid image path.");
			return 0;
		}

		Kotonoha_Picture* object = new Kotonoha_Picture();
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
		const std::string full = basePath;
		const size_t separator = full.find_last_of("/\\");
		const std::string directory = separator == std::string::npos
			? "." : full.substr(0, separator);
		const std::string name = full.substr(separator == std::string::npos
			? 0 : separator + 1);
		if (name.size() < 5 || Lower(name.substr(name.size() - 4)) != ".png")
			return;
		const std::string stem = name.substr(0, name.size() - 4);
		struct Scan {
			const std::string* stem;
			const std::string* directory;
			std::map<std::string, SchoolDaysAbcGroupResources> groups;
		} scan{&stem, &directory, {}};
		SDL_EnumerateDirectory(directory.c_str(),
			[](void* user, const char*, const char* filename) -> SDL_EnumerationResult {
				auto* scan = static_cast<Scan*>(user);
				const std::string name = filename;
				const std::string lower = Lower(name);
				const std::string prefix = Lower(*scan->stem);
				if (name.size() <= scan->stem->size() + 6 ||
					lower.compare(0, prefix.size(), prefix) != 0 ||
					lower.substr(lower.size() - 4) != ".png")
					return SDL_ENUM_CONTINUE;
				const size_t statePos = lower.size() - 6;
				if (lower[statePos] != '.' ||
					(lower[statePos + 1] < 'a' || lower[statePos + 1] > 'c'))
					return SDL_ENUM_CONTINUE;
				const std::string key = lower.substr(prefix.size(),
					statePos - prefix.size());
				if (key.empty()) return SDL_ENUM_CONTINUE;
				auto& group = scan->groups[key];
				group.baseStem = *scan->stem;
				group.key = key;
				group.paths[lower[statePos + 1] - 'a'] =
					*scan->directory + "/" + name;
				return SDL_ENUM_CONTINUE;
			}, &scan);
		for (auto& pair : scan.groups) {
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

	enum Kotonoha_Scene_Status Image::Render(KOTONOHA_SCENE_CALL) {
		Kotonoha_Scene_Status status = KOTONOHA_SCENE_NULL;
		auto* here = static_cast<Image*>(userData);
		if (here == nullptr || render == nullptr) {
			return status;
		}

		if (here->timeManager == nullptr || here->lock == nullptr) {
			return status;
		}

		bool cleaned = false;

		SDL_LockMutex(here->lock);
		for (auto it = here->pictures.begin(); it != here->pictures.end();) {
			Kotonoha_Picture* picture = *it;
			if (picture == nullptr) {
				it = here->pictures.erase(it);
				continue;
			}
			const Kotonoha_SceneTick sceneTick =
				Kotonoha_MillisecondsToSceneTick(Kotonoha_timeGet(here->timeManager));
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

			const Uint64 current =
				Kotonoha_timeGetFromEvent(here->timeManager,
					picture->startTime,
					picture->endTime,
					&inRange,
					&diff);

			if (picture->texture == nullptr) {
				picture->texture =
					Kotonoha_imageCreateTexture(render, picture->path, -1, -1);

				if (picture->texture == nullptr) {
					SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
						"Failed to create image texture: %s",
						picture->path ? picture->path : "<null>");
					++it;
					continue;
				}

				// FFmpeg outputs straight RGBA, including semitransparent bases.
				SDL_SetTextureBlendMode(picture->texture, SDL_BLENDMODE_BLEND);
			}

			if (sceneTick >= endTick) {
				// The image canvas can still contain the previous ABC texture.
				// Clear only this transparent layer before retaining the base.
				Uint8 r = 0, g = 0, b = 0, a = 0;
				SDL_GetRenderDrawColor(render, &r, &g, &b, &a);
				SDL_SetRenderDrawColor(render, 0, 0, 0, 0);
				SDL_RenderClear(render);
				SDL_SetRenderDrawColor(render, r, g, b, a);
				SDL_RenderTexture(render, picture->texture, nullptr, nullptr);
				status = picture->id > 0 ? status : KOTONOHA_SCENE_DRAW_LAST;

				Kotonoha_Picture* toDestroy = picture;
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

			if (!picture->canRender) {
				picture->lastTime = current;
				++it;
				continue;
			}

			if (!cleaned) {
				Uint8 r = 0, g = 0, b = 0, a = 0;
				SDL_GetRenderDrawColor(render, &r, &g, &b, &a);
				SDL_SetRenderDrawColor(render, 0, 0, 0, 0);
				SDL_RenderClear(render);
				SDL_SetRenderDrawColor(render, r, g, b, a);
				cleaned = true;
			}

			SDL_RenderTexture(render, picture->texture, nullptr, nullptr);
			here->DrawAbcForBase(picture, render,
				Kotonoha_timeGet(here->timeManager));
			if (status != KOTONOHA_SCENE_DRAW_LAST)
				status = KOTONOHA_SCENE_DRAW;
			picture->lastTime = current;
			++it;
		}

		SDL_UnlockMutex(here->lock);
		return status;
	}

	void Image::Reset() {
		const auto clearGroups = [this]() {
			activeBaseGeneration = 0;
			for (auto& group : abcGroups)
				for (auto*& texture : group->textures)
					if (texture != nullptr) {
						SDL_DestroyTexture(texture);
						texture = nullptr;
					}
			abcGroups.clear();
		};
		if (lock == nullptr) {
			clearGroups();
			for (auto* picture : pictures) {
				DestroyPicture(picture);
			}
			pictures.clear();
			return;
		}

		SDL_LockMutex(lock);
		clearGroups();
		for (auto* picture : pictures) {
			DestroyPicture(picture);
		}
		pictures.clear();
		SDL_UnlockMutex(lock);
	}

	Image::~Image() {
		Reset();

		if (lock != nullptr) {
			SDL_DestroyMutex(lock);
			lock = nullptr;
		}
	}

} // namespace Kotonoha
