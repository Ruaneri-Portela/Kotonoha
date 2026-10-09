#include <Kotonoha/components/Image.hpp>
#include <SDL3/SDL_render.h>
#include <algorithm>
#include <cctype>

namespace Kotonoha {

	namespace {
		static std::string Lower(std::string value) {
			std::transform(value.begin(), value.end(), value.begin(),
				[](unsigned char c) { return static_cast<char>(std::tolower(c)); });
			return value;
		}

		static int SDLCALL DecodeImageFrame(void* data) {
			auto* load = static_cast<Kotonoha_imageLoad*>(data);
			load->frame = Kotonoha_imageDecodeFrame(
				load->path, -1, -1, &load->buffer, &load->ioOperation);
			SDL_SetAtomicInt(&load->state, 2);
			return 0;
		}

		static void ReleaseImageLoad(Kotonoha_imageLoad* load) {
			if (load == nullptr) {
				return;
			}
			if (load->thread != nullptr) {
				Kotonoha_IOMonitorOperationCancel(&load->ioOperation);
				SDL_WaitThread(load->thread, nullptr);
				load->thread = nullptr;
			}
			Kotonoha_imageReleaseFrame(&load->frame, &load->buffer);
			Kotonoha_IOMonitorOperationReset(&load->ioOperation);
			load->path = nullptr;
			load->owner = nullptr;
			load->failed = false;
			load->missingIgnored = false;
			SDL_SetAtomicInt(&load->state, 0);
		}

		static bool PrepareImageTexture(SDL_Renderer* renderer,
			const char* path, Kotonoha_imageLoad* load,
			SDL_Texture** texture, Kotonoha_IOMonitor* owner) {
			if (renderer == nullptr || path == nullptr || load == nullptr ||
				texture == nullptr) {
				SDL_LogError(SDL_LOG_CATEGORY_RENDER,
					"Invalid arguments for asynchronous image loading");
				return false;
			}
			if (*texture != nullptr || load->failed) {
				return *texture != nullptr;
			}

			if (load->thread != nullptr &&
				SDL_GetAtomicInt(&load->state) == 2) {
				SDL_WaitThread(load->thread, nullptr);
				load->thread = nullptr;

				if (load->frame != nullptr) {
					*texture = Kotonoha_imageCreateTextureFromFrame(
						renderer, load->frame);
					Kotonoha_imageReleaseFrame(&load->frame, &load->buffer);
					if (*texture == nullptr) {
						load->failed = true;
					}
				}
				else {
					load->failed = true;
					if (Kotonoha_IOMonitorIsIgnoredMissing(&load->ioOperation)) {
						load->missingIgnored = true;
					}
				}

				Kotonoha_IOMonitorOperationReset(&load->ioOperation);
				SDL_SetAtomicInt(&load->state, 0);
			}

			if (*texture != nullptr || load->failed) {
				return *texture != nullptr;
			}

			if (load->thread == nullptr) {
				load->path = path;
				load->buffer = nullptr;
				load->owner = owner;
				load->missingIgnored = false;
				Kotonoha_IOMonitorOperationInit(
					&load->ioOperation, owner);
				SDL_SetAtomicInt(&load->state, 1);
				load->thread = SDL_CreateThread(
					DecodeImageFrame, "Image FFmpeg decoder", load);
				if (load->thread == nullptr) {
					SDL_SetAtomicInt(&load->state, 0);
					load->failed = true;
					Kotonoha_IOMonitorOperationReset(&load->ioOperation);
					SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
						"Failed to create image decoding thread: %s",
						SDL_GetError());
				}
			}
			return false;
		}

		static void DestroyPicture(Kotonoha_Picture* picture) {
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
	} // namespace

	Image::Image(Kotonoha_time* time, Kotonoha_IOMonitor* ioMonitor)
		: timeManager(time), ioMonitor(ioMonitor), lock(nullptr) {
		lock = SDL_CreateMutex();
		if (lock == nullptr) {
			SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
				"Failed to create image mutex: %s",
				SDL_GetError());
		}
	}

	void Image::DiscoverAbcGroups(const char* basePath, Uint64 startTime,
		Uint64 endTime, Uint64 ownerBaseGeneration) {
		const std::string fullPath = basePath;
		const size_t separator = fullPath.find_last_of("/\\");
		const std::string directory = separator == std::string::npos
			? "." : fullPath.substr(0, separator);
		const std::string filename = fullPath.substr(
			separator == std::string::npos ? 0 : separator + 1);
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
		} scan{ stem, directory, {} };

		if (!SDL_EnumerateDirectory(directory.c_str(),
			[](void* opaque, const char*, const char* entry) {
				auto* scan = static_cast<Scan*>(opaque);
				const std::string name(entry);
				const std::string lower = Lower(name);
				const std::string lowerStem = Lower(scan->stem);
				if (lower.size() <= lowerStem.size() + 6 ||
					lower.compare(0, lowerStem.size(), lowerStem) != 0 ||
					lower.compare(lower.size() - 4, 4, ".png") != 0) {
					return SDL_ENUM_CONTINUE;
				}

				const size_t statePosition = lower.size() - 6;
				if (lower[statePosition] != '.' ||
					lower[statePosition + 1] < 'a' ||
					lower[statePosition + 1] > 'c') {
					return SDL_ENUM_CONTINUE;
				}

				const std::string key = lower.substr(
					lowerStem.size(), statePosition - lowerStem.size());
				if (key.empty()) {
					return SDL_ENUM_CONTINUE;
				}

				AbcGroup* group = nullptr;
				for (auto& candidate : scan->groups) {
					if (candidate->key == key) {
						group = candidate.get();
						break;
					}
				}
				if (group == nullptr) {
					scan->groups.emplace_back(new AbcGroup());
					group = scan->groups.back().get();
					group->key = key;
				}

				const Uint8 state =
					static_cast<Uint8>(lower[statePosition + 1] - 'a');
				group->paths[state] = scan->directory + "/" + name;
				return SDL_ENUM_CONTINUE;
			}, &scan)) {
			SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
				"Failed to scan for A/B/C mouth overlays beside %s: %s",
				basePath, SDL_GetError());
		}

		for (auto& group : scan.groups) {
			group->ownerBaseGeneration = ownerBaseGeneration;
			group->startTime = startTime;
			group->endTime = endTime;
			abcGroups.emplace_back(std::move(group));
		}
	}

	void Image::DrawAbcForBase(Kotonoha_Picture* picture,
		SDL_Renderer* renderer, Uint64 atMs) {
		for (const auto& group : abcGroups) {
			if (!group->bound || group->ownerBaseGeneration != picture->baseGeneration ||
				atMs < group->startTime || atMs >= group->endTime ||
				atMs < group->bindingStartTime ||
				atMs >= group->bindingEndTime || group->activeIndex > 2) {
				continue;
			}

			const Uint8 index = group->activeIndex;
			if (group->paths[index].empty()) {
				continue;
			}
			if (group->textures[index] == nullptr) {
				PrepareImageTexture(renderer, group->paths[index].c_str(),
					&group->loads[index], &group->textures[index], ioMonitor);
				if (group->textures[index] != nullptr &&
					(!SDL_SetTextureBlendMode(
						group->textures[index], SDL_BLENDMODE_BLEND) ||
					 !SDL_SetTextureScaleMode(
						group->textures[index], SDL_SCALEMODE_LINEAR))) {
					SDL_LogError(SDL_LOG_CATEGORY_RENDER,
						"Failed to configure mouth overlay %s: %s",
						group->paths[index].c_str(), SDL_GetError());
					SDL_DestroyTexture(group->textures[index]);
					group->textures[index] = nullptr;
				}
			}
			if (group->textures[index] != nullptr &&
				!SDL_RenderTexture(renderer, group->textures[index], nullptr, nullptr)) {
				SDL_LogError(SDL_LOG_CATEGORY_RENDER,
					"Failed to draw mouth overlay %s: %s",
					group->paths[index].c_str(), SDL_GetError());
			}
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
		object->load.owner = ioMonitor;

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

	void Image::SetAbcState(const std::string& key, Uint8 index,
		Uint64 voiceStartMs, Uint64 voiceEndMs) {
		if (lock == nullptr || key.empty() || index > 2 ||
			voiceStartMs >= voiceEndMs) {
			return;
		}

		const std::string normalizedKey = Lower(key);
		SDL_LockMutex(lock);
		for (auto& group : abcGroups) {
			if (group->key != normalizedKey ||
				voiceStartMs < group->startTime ||
				voiceStartMs >= group->endTime) {
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
				PrepareImageTexture(
					render, picture->path, &picture->load, &picture->texture,
					here->ioMonitor);

				if (picture->texture == nullptr) {
					if (picture->load.failed &&
						!picture->load.missingIgnored) {
						SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
							"Failed to create image texture: %s",
							picture->path ? picture->path : "<null>");
					}
					if (picture->load.ioOperation.stalled &&
						status != KOTONOHA_SCENE_DRAW &&
						status != KOTONOHA_SCENE_DRAW_LAST) {
						status = KOTONOHA_SCENE_WAITING;
					}
					++it;
					continue;
				}

				if (!SDL_SetTextureBlendMode(
					picture->texture, SDL_BLENDMODE_BLEND)) {
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
				SDL_RenderTexture(render, picture->texture, nullptr, nullptr);
				if (picture->id == 0) {
					here->DrawAbcForBase(
						picture, render, Kotonoha_timeGet(here->timeManager));
				}
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

			if (picture->id == 1 && (current - picture->lastTime) > 750) {
				picture->canRender = !picture->canRender;
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
			if (picture->id == 0) {
				here->DrawAbcForBase(
					picture, render, Kotonoha_timeGet(here->timeManager));
			}
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
			for (auto& group : abcGroups) {
				for (size_t index = 0; index < 3; ++index) {
					ReleaseImageLoad(&group->loads[index]);
					SDL_Texture*& texture = group->textures[index];
					if (texture != nullptr) {
						SDL_DestroyTexture(texture);
						texture = nullptr;
					}
				}
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
