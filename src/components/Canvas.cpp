#include <algorithm>
#include <Kotonoha/components/Canvas.hpp>

namespace Kotonoha {

	namespace {
		static bool CanvasItemLess(const CanvasItem& a,
			const CanvasItem& b) {
			return a.zIndex < b.zIndex;
		}

		static bool IsValidSize(const SDL_FRect& place) {
			return place.w > 0.0f && place.h > 0.0f;
		}
	} // namespace

	void Canvas::RegisterCanva(
		Kotonoha_Scene_Status(*drawingPoint)(KOTONOHA_SCENE_CALL),
		Sint16 zIndex,
		SDL_FRect place,
		void* userData) {
		if (drawingPoint == nullptr || userData == nullptr) {
			return;
		}

		CanvasItem item{};
		item.drawingPoint = drawingPoint;
		item.zIndex = zIndex;
		item.place = place;
		item.userData = userData;
		item.target = nullptr;
		item.swapTexture = false;

		drawingList.push_back(item);
		std::sort(drawingList.begin(), drawingList.end(), CanvasItemLess);
	}

	void Canvas::UnregisterCanva(
		Kotonoha_Scene_Status(*drawingPoint)(KOTONOHA_SCENE_CALL)) {
		for (auto& item : drawingList) {
			if (item.drawingPoint == drawingPoint && item.target != nullptr) {
				SDL_DestroyTexture(item.target);
				item.target = nullptr;
			}
		}

		drawingList.erase(
			std::remove_if(drawingList.begin(), drawingList.end(),
				[drawingPoint](const CanvasItem& item) {
					return item.drawingPoint == drawingPoint;
				}),
			drawingList.end());
	}

	void Canvas::UpdateCanva(
		Kotonoha_Scene_Status(*drawingPoint)(KOTONOHA_SCENE_CALL),
		Sint16 zIndex,
		SDL_FRect place) {
		if (drawingPoint == nullptr) {
			dirtyPlace = place;
		}

		bool needsResort = false;

		for (auto& item : drawingList) {
			if (drawingPoint != nullptr && item.drawingPoint != drawingPoint) {
				continue;
			}

			if (item.place.w != place.w || item.place.h != place.h) {
				item.swapTexture = true;
			}

			if (zIndex != -1 && item.zIndex != zIndex) {
				item.zIndex = zIndex;
				needsResort = true;
			}

			item.place = place;
		}

		if (needsResort) {
			std::sort(drawingList.begin(), drawingList.end(), CanvasItemLess);
		}
	}

	void Canvas::BeginFrame() {
		minDrawnZIndex = 32767;
	}

	bool Canvas::DrewAtOrBelow(Sint16 zIndex) const {
		return minDrawnZIndex <= zIndex;
	}

	SDL_AppResult Canvas::RenderCanvas(SDL_Window* window,
		SDL_Renderer* render,
		struct Kotonoha_eventStack* eventQueu) {
		if (render == nullptr) {
			return SDL_APP_FAILURE;
		}

		BeginFrame();
		SDL_AppResult status = SDL_APP_CONTINUE;

		if (dirtyTexture != nullptr) {
			if (SDL_RenderTexture(render, dirtyTexture, nullptr, &dirtyPlace)) {
				minDrawnZIndex = std::min(minDrawnZIndex, dirtyZIndex);
			}
			else {
				SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
					"Failed to draw retained canvas texture: %s", SDL_GetError());
				status = SDL_APP_FAILURE;
			}
		}

		for (auto& item : drawingList) {
			if (item.userData == nullptr || item.drawingPoint == nullptr) {
				continue;
			}

			if (!IsValidSize(item.place)) {
				continue;
			}

			if (item.swapTexture && item.target != nullptr) {
				SDL_DestroyTexture(item.target);
				item.target = nullptr;
				item.swapTexture = false;
			}

			if (item.target == nullptr) {
				item.target = SDL_CreateTexture(
					render,
					SDL_PIXELFORMAT_RGBA8888,
					SDL_TEXTUREACCESS_TARGET,
					static_cast<int>(item.place.w),
					static_cast<int>(item.place.h));

				if (item.target == nullptr) {
					SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
						"Failed to create canvas texture: %s", SDL_GetError());
					status = SDL_APP_FAILURE;
					continue;
				}
				if (!SDL_SetTextureBlendMode(item.target, SDL_BLENDMODE_BLEND)) {
					SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
						"Failed to configure canvas texture blending: %s",
						SDL_GetError());
					status = SDL_APP_FAILURE;
				}
			}

			if (!SDL_SetRenderTarget(render, item.target)) {
				SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
					"Failed to set canvas render target: %s", SDL_GetError());
				status = SDL_APP_FAILURE;
				continue;
			}
			const Kotonoha_Scene_Status result =
				item.drawingPoint(window, render, eventQueu, item.userData, item.target);

			if (result == KOTONOHA_SCENE_NULL || result == KOTONOHA_SCENE_COMPLETE) {
				if (!SDL_SetRenderTarget(render, nullptr)) {
					SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
						"Failed to restore backbuffer after canvas callback: %s",
						SDL_GetError());
					return SDL_APP_FAILURE;
				}
				continue;
			}

			if (result == KOTONOHA_SCENE_DRAW_LAST) {
				if (dirtyTexture != nullptr) {
					SDL_DestroyTexture(dirtyTexture);
				}
				dirtyTexture = item.target;
				dirtyPlace = item.place;
				dirtyZIndex = item.zIndex;
				item.target = nullptr;

				if (!SDL_SetRenderTarget(render, nullptr)) {
					SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
						"Failed to restore backbuffer after canvas callback: %s",
						SDL_GetError());
					return SDL_APP_FAILURE;
				}
				if (dirtyTexture != nullptr &&
					SDL_RenderTexture(render, dirtyTexture, nullptr, &dirtyPlace)) {
					minDrawnZIndex = std::min(minDrawnZIndex, item.zIndex);
				}
				else if (dirtyTexture != nullptr) {
					SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
						"Failed to draw retained canvas texture: %s", SDL_GetError());
					status = SDL_APP_FAILURE;
				}
				continue;
			}

			if (result == KOTONOHA_SCENE_WAITING) {
				if (!SDL_SetRenderTarget(render, nullptr)) {
					SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
						"Failed to restore backbuffer after waiting canvas callback: %s",
						SDL_GetError());
					return SDL_APP_FAILURE;
				}
				continue;
			}

			if (item.target != nullptr) {
				if (!SDL_SetRenderTarget(render, nullptr)) {
					SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
						"Failed to restore backbuffer after canvas callback: %s",
						SDL_GetError());
					return SDL_APP_FAILURE;
				}
				if (SDL_RenderTexture(render, item.target, nullptr, &item.place) &&
					(result == KOTONOHA_SCENE_DRAW ||
						result == KOTONOHA_SCENE_DRAW_OVERLAYED)) {
					minDrawnZIndex = std::min(minDrawnZIndex, item.zIndex);
				}
				else if (result == KOTONOHA_SCENE_DRAW ||
					result == KOTONOHA_SCENE_DRAW_OVERLAYED) {
					SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
						"Failed to draw canvas texture: %s", SDL_GetError());
					status = SDL_APP_FAILURE;
				}
			}
		}
		return status;
	}

	int Canvas::CanvasCount() {
		return static_cast<int>(drawingList.size());
	}

	void Canvas::Reset() {
		for (auto& item : drawingList) {
			if (item.target != nullptr) {
				SDL_DestroyTexture(item.target);
				item.target = nullptr;
			}
		}

		if (dirtyTexture != nullptr) {
			SDL_DestroyTexture(dirtyTexture);
			dirtyTexture = nullptr;
		}
		dirtyZIndex = 32767;
		minDrawnZIndex = 32767;
	}

	Canvas::~Canvas() {
		Canvas::Reset();
	}

} // namespace Kotonoha