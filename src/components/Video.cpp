#include <Kotonoha/components/Video.hpp>
#include <SDL3/SDL_oldnames.h>
#include <SDL3/SDL_render.h>

namespace Kotonoha {

	Video::Video(Kotonoha_time* timeManager, Kotonoha_IOMonitor* ioMonitor)
		: timeManager(timeManager), ioMonitor(ioMonitor) {
		lock = SDL_CreateMutex();
		if (lock == nullptr) {
			SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
				"Failed to create video mutex: %s",
				SDL_GetError());
		}
	}

	bool Video::Register(const char* path, Uint64 startTime, Uint64 endTime,
		bool useOrsFrameTimeline,
		Uint64 orsEndFrame) {
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
			Kotonoha_VideoRenderInit(
				path,
				timeManager,
				startTime,
				endTime,
				useOrsFrameTimeline,
				orsEndFrame,
				ioMonitor);
		if (object == nullptr) {
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

	void Video::RenderTexture(
		SDL_Renderer *render,
		SDL_Texture *texture)
	{
		if (render == nullptr || texture == nullptr) {
			return;
		}

		const float factor = Kotonoha_UpscalingFactor();

		if (factor > 1.0f && IsUpscalingActive()) {
			float videoWidth = 0.0f;
			float videoHeight = 0.0f;

			if (!SDL_GetTextureSize(texture, &videoWidth, &videoHeight)) {
				SDL_LogError(
					SDL_LOG_CATEGORY_RENDER,
					"Failed to get video texture size: %s",
					SDL_GetError()
				);
				return;
			}

			const int requiredWidth = static_cast<int>(videoWidth);
			const int requiredHeight = static_cast<int>(videoHeight);

			if (requiredWidth <= 0 || requiredHeight <= 0) {
				SDL_LogError(
					SDL_LOG_CATEGORY_RENDER,
					"Invalid video texture size: %dx%d",
					requiredWidth,
					requiredHeight
				);
				return;
			}

			if (!EnsureUpscalingTextures(
					render,
					requiredWidth,
					requiredHeight,
					factor)) {
				SDL_LogError(
					SDL_LOG_CATEGORY_RENDER,
					"Failed to ensure upscaling textures for video"
				);
				return;
			}
			
			if (ProcessUpscale(render, texture)) {
				SDL_RenderTexture(
					render,
					upScaledTexture,
					nullptr,
					nullptr
				);
				return;
			}
		}

		// Caminho normal, sem upscaling.
		SDL_RenderTexture(
			render,
			texture,
			nullptr,
			nullptr
		);
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

		if (status == KOTONOHA_SCENE_DRAW ||
			status == KOTONOHA_SCENE_COMPLETE) {
			if (currentVideo->texture != nullptr) {
				here->RenderTexture(render, currentVideo->texture);
			}

			if (status == KOTONOHA_SCENE_DRAW) {
				returnStatus = KOTONOHA_SCENE_DRAW;
				++it;
			} else {
				if (returnStatus != KOTONOHA_SCENE_DRAW) {
					returnStatus = KOTONOHA_SCENE_DRAW_LAST;
				}

				finishedVideos.push_back(currentVideo);
				it = here->videos.erase(it);
			}

			continue;
		}

			if (status == KOTONOHA_SCENE_WAITING) {
				if (returnStatus != KOTONOHA_SCENE_DRAW &&
					returnStatus != KOTONOHA_SCENE_DRAW_LAST) {
					returnStatus = KOTONOHA_SCENE_WAITING;
				}
				++it;
				continue;
			}

			++it;
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
		ResetUpscaling();
		std::vector<Kotonoha_videoData*> oldVideos;

		if (lock != nullptr) {
			SDL_LockMutex(lock);
		}

		oldVideos.swap(videos);

		if (lock != nullptr) {
			SDL_UnlockMutex(lock);
		}


		for (Kotonoha_videoData* video : oldVideos) {
			Kotonoha_VideoRenderShutdown(&video);
		}

		oldVideos.clear();
	}

	Video::~Video() {
		Reset();
		if (lock != nullptr) {
			SDL_DestroyMutex(lock);
			lock = nullptr;
		}
	}

} // namespace Kotonoha
