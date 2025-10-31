#include <algorithm>
#include <Kotonoha/components/Canvas.hpp>

namespace Kotonoha
{

	void Canvas::RegisterCanva(Kotonoha_Scene_Status (*drawingPoint)(KOTONOHA_SCENE_CALL), Sint16 zIndex, SDL_FRect place, void *userData)
	{
		CanvasItem item;
		item.drawingPoint = drawingPoint;
		item.zIndex = zIndex;
		item.place = place;
		item.userData = userData;
		item.target = nullptr;
		item.swapTexture = false;

		drawingList.push_back(item);
		std::sort(drawingList.begin(), drawingList.end(), [](const CanvasItem &a, const CanvasItem &b)
				  { return a.zIndex < b.zIndex; });
	}

	void Canvas::UnregisterCanva(Kotonoha_Scene_Status (*drawingPoint)(KOTONOHA_SCENE_CALL))
	{
		drawingList.erase(std::remove_if(drawingList.begin(), drawingList.end(),
										 [drawingPoint](const CanvasItem &item)
										 {
											 return item.drawingPoint == drawingPoint;
										 }),
						  drawingList.end());
	}

	void Canvas::UpdateCanva(Kotonoha_Scene_Status (*drawingPoint)(KOTONOHA_SCENE_CALL), Sint16 zIndex, SDL_FRect place)
	{
		if (drawingPoint == nullptr)
			dirtyPlace = place;
		for (auto it = drawingList.begin(); it != drawingList.end(); ++it)
		{
			if (drawingPoint == nullptr || it->drawingPoint == drawingPoint)
			{
				bool needsResort = false;

				// Atualiza swapTexture caso o tamanho seja alterado
				if (it->place.h != place.h || it->place.w != place.w)
				{
					it->swapTexture = true;
				}

				// Reordena se zIndex mudou e não é -1 (-1 indica "não alterar")
				if (zIndex != -1 && it->zIndex != zIndex)
				{
					needsResort = true;
					it->zIndex = zIndex;
				}

				if (needsResort)
				{
					std::sort(drawingList.begin(), drawingList.end(), [](const CanvasItem &a, const CanvasItem &b)
							  { return a.zIndex < b.zIndex; });
				}

				it->place = place;
			}
		}
	}

	SDL_AppResult Canvas::RenderCanvas(SDL_Window *window, SDL_Renderer *render, struct Kotonoha_eventStack *eventQueu)
	{
		if (dirtyTexture)
		{
			SDL_SetRenderTarget(render, nullptr);
			SDL_RenderTexture(render, dirtyTexture, nullptr, &dirtyPlace);
		}

		for (auto &item : drawingList)
		{
			if (item.userData == nullptr)
				continue;

			// Destrói e recria textura se swapTexture for verdadeiro
			if (item.swapTexture && item.target != nullptr)
			{
				SDL_DestroyTexture(item.target);
				item.target = nullptr;
				item.swapTexture = false;
			}

			// Cria textura se não houver uma existente
			if (item.target == nullptr)
			{
				item.target = SDL_CreateTexture(render, SDL_PIXELFORMAT_RGBA8888, SDL_TEXTUREACCESS_TARGET,
												static_cast<int>(item.place.w), static_cast<int>(item.place.h));
				SDL_SetTextureBlendMode(item.target, SDL_BLENDMODE_BLEND);
				SDL_SetRenderTarget(render, item.target);
				SDL_RenderClear(render);
			}

			// Configura o renderizador para a textura de destino
			SDL_SetRenderTarget(render, item.target);

			Kotonoha_Scene_Status result = item.drawingPoint(window, render, eventQueu, item.userData, item.target);

			if (result == KOTONOHA_SCENE_DRAW_LAST)
			{
				if (dirtyTexture)
					SDL_DestroyTexture(dirtyTexture);
				dirtyTexture = item.target;
				dirtyPlace = item.place;
				item.target = nullptr;
			}

			SDL_SetRenderTarget(render, nullptr);
			SDL_RenderTexture(render, item.target, nullptr, &item.place);
		}
		return SDL_APP_CONTINUE;
	}

	int Canvas::CanvasCount()
	{
		return static_cast<int>(drawingList.size());
	}

	Canvas::~Canvas()
	{
		SDL_DestroyTexture(dirtyTexture);
	}

}
