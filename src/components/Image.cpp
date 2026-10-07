#include <Kotonoha/components/Image.hpp>
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
	} // namespace

	void Image::DrawAbcForBase(Kotonoha_Picture* picture,
		SDL_Renderer* renderer) {
		for (const auto& item : abcGroups) {
			auto* group = item.get();
			if (group->basePath != picture->path ||
				group->activeIndex > 2) continue;
			const Uint8 index = group->activeIndex;
			if (group->resources.paths[index].empty()) continue;
			if (group->textures[index] == nullptr) {
				group->textures[index] = Kotonoha_imageCreateTexture(renderer,
					group->resources.paths[index].c_str(), -1, -1);
				if (group->textures[index] != nullptr)
					SDL_SetTextureBlendMode(group->textures[index], SDL_BLENDMODE_BLEND);
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

	void Image::Register(const char* path, Uint64 startTime, Uint64 endTime,
		Uint8 id) {
		if (path == nullptr || *path == '\0') {
			SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
				"Invalid image path.");
			return;
		}

		Kotonoha_Picture* object = new Kotonoha_Picture();
		object->path = SDL_strdup(path);
		object->texture = nullptr;
		object->startTime = startTime;
		object->endTime = endTime;
		object->lastTime = 0;
		object->id = id;
		object->canRender = true;

		if (object->path == nullptr) {
			SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
				"Failed to duplicate image path.");
			delete object;
			return;
		}

		if (lock == nullptr) {
			DestroyPicture(object);
			return;
		}

		SDL_LockMutex(lock);
		pictures.push_back(object);
		if (id == 0) DiscoverAbcGroups(path, startTime, endTime);
		SDL_UnlockMutex(lock);
	}

	void Image::DiscoverAbcGroups(const char* basePath, Uint64 startTime,
		Uint64 endTime) {
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
			group->basePath = full;
			group->startTime = startTime;
			group->endTime = endTime;
			abcGroups.push_back(std::move(group));
		}
	}

	Image::AbcGroup* Image::FindAbcGroup(const std::string& key, Uint64 atMs) {
		if (lock == nullptr || key.empty()) return nullptr;
		SDL_LockMutex(lock);
		AbcGroup* found = nullptr;
		for (const auto& group : abcGroups)
			if (group->resources.key == Lower(key) &&
				group->startTime <= atMs && atMs < group->endTime)
				found = group.get();
		SDL_UnlockMutex(lock);
		return found;
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

				SDL_SetTextureBlendMode(picture->texture,
					SDL_BLENDMODE_BLEND_PREMULTIPLIED);
			}

			if (diff > 0) {
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
				SDL_RenderClear(render);
				cleaned = true;
			}

			SDL_RenderTexture(render, picture->texture, nullptr, nullptr);
			here->DrawAbcForBase(picture, render);
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
