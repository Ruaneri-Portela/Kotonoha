#pragma once
#include <vector>

extern "C"
{
#include <SDL3/SDL.h>
#include <Kotonoha/Kotonoha.h>
#include <Kotonoha/utils/UserEvents.h>
}

struct CanvasItem
{
	Kotonoha_Scene_Status (*drawingPoint)(KOTONOHA_SCENE_CALL);
	SDL_Texture *target;

	Sint16 zIndex;
	SDL_FRect place;
	bool swapTexture;
	bool holdTexture;

	void *userData;
};

namespace Kotonoha
{
	class Canvas
	{
	private:
		std::vector<CanvasItem> drawingList = std::vector<CanvasItem>();
		SDL_Texture *dirtyTexture = nullptr;
		SDL_FRect dirtyPlace = {0, 0, 0, 0};

	public:
		void RegisterCanva(Kotonoha_Scene_Status (*drawingPoint)(KOTONOHA_SCENE_CALL), Sint16 zIndex, SDL_FRect place, void *userData);

		void UnregisterCanva(Kotonoha_Scene_Status (*drawingPoint)(KOTONOHA_SCENE_CALL));

		void UpdateCanva(Kotonoha_Scene_Status (*drawingPoint)(KOTONOHA_SCENE_CALL), Sint16 zIndex, SDL_FRect place);

		SDL_AppResult RenderCanvas(SDL_Window *window, SDL_Renderer *render, struct Kotonoha_eventStack *eventQueu);

		int CanvasCount();

		~Canvas();
	};
}