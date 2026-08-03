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

	bool Video::Register(const char* path, Uint64 startTime, Uint64 endTime) {
		if (timeManager == nullptr) {
			SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
				"Video time manager is null.");
			return false;
		}

		if (path == nullptr || *path == '\0') {
			SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
				"Video path is invalid.");
			return false;
		}

		Kotonoha_videoData* object =
			Kotonoha_VideoRenderInit(path, timeManager, startTime, endTime);
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
		videos.push_back(object);
		SDL_UnlockMutex(lock);
		return true;
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
			Kotonoha_videoData* currentVideo = *it;
			if (currentVideo == nullptr) {
				it = here->videos.erase(it);
				continue;
			}

			const Kotonoha_Scene_Status status =
				Kotonoha_VideoRenderProcess(currentVideo, render);

			switch (status) {
			case KOTONOHA_SCENE_DRAW:
				SDL_RenderTexture(render, currentVideo->texture, nullptr, nullptr);
				returnStatus = KOTONOHA_SCENE_DRAW;
				++it;
				break;

			case KOTONOHA_SCENE_COMPLETE:
				if (returnStatus != KOTONOHA_SCENE_DRAW) {
					SDL_RenderTexture(render, currentVideo->texture, nullptr, nullptr);
					returnStatus = KOTONOHA_SCENE_DRAW_LAST;
				}
				finishedVideos.push_back(currentVideo);
				it = here->videos.erase(it);
				break;
			case KOTONOHA_SCENE_WAITING:
				returnStatus = KOTONOHA_SCENE_WAITING;
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
			for (auto& video : videos) {
				Kotonoha_VideoRenderShutdown(&video);
			}
			videos.clear();
			return;
		}

		std::vector<Kotonoha_videoData*> oldVideos;

		SDL_LockMutex(lock);
		oldVideos.swap(videos);
		SDL_UnlockMutex(lock);

		for (Kotonoha_videoData* video : oldVideos) {
			Kotonoha_VideoRenderShutdown(&video);
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