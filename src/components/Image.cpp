#include <Kotonoha/components/Image.hpp>
#include <SDL3/SDL_render.h>

namespace Kotonoha {

	namespace {
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
				status = KOTONOHA_SCENE_DRAW_LAST;

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
			if (status != KOTONOHA_SCENE_DRAW_LAST)
				status = KOTONOHA_SCENE_DRAW;
			picture->lastTime = current;
			++it;
		}

		SDL_UnlockMutex(here->lock);
		return status;
	}

	void Image::Reset() {
		if (lock == nullptr) {
			for (auto* picture : pictures) {
				DestroyPicture(picture);
			}
			pictures.clear();
			return;
		}

		SDL_LockMutex(lock);
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