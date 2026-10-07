#include <Kotonoha/components/Video.hpp>

namespace Kotonoha {

	Video::Video(Kotonoha_time* timeManager) : timeManager(timeManager) {
		lock = SDL_CreateMutex();
		if (lock == nullptr) {
			SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
				"Failed to create video mutex: %s",
				SDL_GetError());
		}
	}

	bool Video::Prepare(const Kotonoha_orsEvent* event, const char* path) {
		if (timeManager == nullptr) {
			SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
				"Video time manager is null.");
			return false;
		}

		if (event == nullptr || path == nullptr || *path == '\0') {
			SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
				"Video path is invalid.");
			return false;
		}

		Kotonoha_videoData* object =
			Kotonoha_VideoRenderInit(
				path,
				timeManager,
				Kotonoha_SceneTickToMillisecondsCeil(event->startTick),
				Kotonoha_SceneTickToMillisecondsCeil(event->endTick),
				event->endTick);
		if (object == nullptr) {
			SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
				"Failed to initialize video: %s", path);
			return false;
		}

		if (lock == nullptr) {
			Kotonoha_VideoRenderShutdown(&object);
			return false;
		}

		SDL_LockMutex(lock);
		videos.push_back({event, object, false});
		SDL_UnlockMutex(lock);
		return true;
	}

	bool Video::Activate(const Kotonoha_orsEvent* event) {
		if (lock == nullptr) return false;
		SDL_LockMutex(lock);
		for (auto& item : videos) {
			if (item.event == event) {
				item.active = true;
				SDL_UnlockMutex(lock);
				return true;
			}
		}
		SDL_UnlockMutex(lock);
		return false;
	}

	bool Video::IsEof(const Kotonoha_orsEvent* event) {
		if (lock == nullptr) return false;
		SDL_LockMutex(lock);
		for (const auto& item : videos) {
			if (item.event == event) {
				const bool eof = item.decoder != nullptr && item.decoder->decoderEof;
				SDL_UnlockMutex(lock);
				return eof;
			}
		}
		SDL_UnlockMutex(lock);
		return false;
	}

	void Video::Remove(const Kotonoha_orsEvent* event) {
		if (lock == nullptr || event == nullptr) return;
		Kotonoha_videoData* removed = nullptr;
		SDL_LockMutex(lock);
		for (auto it = videos.begin(); it != videos.end(); ++it) {
			if (it->event == event) {
				removed = it->decoder;
				videos.erase(it);
				break;
			}
		}
		SDL_UnlockMutex(lock);
		Kotonoha_VideoRenderShutdown(&removed);
	}

	Kotonoha_Scene_Status Video::Render(KOTONOHA_SCENE_CALL) {
		Video* here = static_cast<Video*>(userData);
		if (here == nullptr) {
			return KOTONOHA_SCENE_COMPLETE;
		}

		if (here->lock == nullptr) {
			return KOTONOHA_SCENE_COMPLETE;
		}

		Kotonoha_Scene_Status returnStatus = KOTONOHA_SCENE_NULL;
		std::vector<Kotonoha_videoData*> finishedVideos;

		SDL_LockMutex(here->lock);

		for (auto it = here->videos.begin(); it != here->videos.end();) {
			Kotonoha_videoData* currentVideo = it->decoder;
			if (currentVideo == nullptr) {
				it = here->videos.erase(it);
				continue;
			}
			if (!it->active) {
				++it;
				continue;
			}

			const Kotonoha_Scene_Status status =
				Kotonoha_VideoRenderProcess(currentVideo, render);

			switch (status) {
			case KOTONOHA_SCENE_DRAW:
				if (currentVideo->texture != nullptr)
					SDL_RenderTexture(render, currentVideo->texture, nullptr, nullptr);
				returnStatus = KOTONOHA_SCENE_DRAW;
				++it;
				break;

			case KOTONOHA_SCENE_COMPLETE:
				if (returnStatus != KOTONOHA_SCENE_DRAW &&
					currentVideo->texture != nullptr) {
					SDL_RenderTexture(render, currentVideo->texture, nullptr, nullptr);
					returnStatus = KOTONOHA_SCENE_DRAW_LAST;
				}
				finishedVideos.push_back(currentVideo);
				it = here->videos.erase(it);
				break;
			case KOTONOHA_SCENE_WAITING:
				if (returnStatus != KOTONOHA_SCENE_DRAW &&
					returnStatus != KOTONOHA_SCENE_DRAW_LAST) {
					returnStatus = KOTONOHA_SCENE_WAITING;
				}
				++it;
				break;
			default:
				++it;
				break;
			}
		}

		const bool isEmpty = here->videos.empty();
		SDL_UnlockMutex(here->lock);

		for (Kotonoha_videoData* video : finishedVideos) {
			Kotonoha_VideoRenderShutdown(&video);
		}

		if (isEmpty && returnStatus != KOTONOHA_SCENE_DRAW_LAST) {
			return KOTONOHA_SCENE_COMPLETE;
		}

		return returnStatus;
	}

	void Video::Reset() {
		if (lock == nullptr) {
			for (auto& item : videos) {
				Kotonoha_VideoRenderShutdown(&item.decoder);
			}
			videos.clear();
			return;
		}

		std::vector<Item> oldVideos;

		SDL_LockMutex(lock);
		oldVideos.swap(videos);
		SDL_UnlockMutex(lock);

		for (auto& item : oldVideos) {
			Kotonoha_VideoRenderShutdown(&item.decoder);
		}
	}

	Video::~Video() {
		Reset();

		if (lock != nullptr) {
			SDL_DestroyMutex(lock);
			lock = nullptr;
		}
	}

} // namespace Kotonoha
