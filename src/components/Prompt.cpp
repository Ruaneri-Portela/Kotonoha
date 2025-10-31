#include <Kotonoha/components/Prompt.hpp>

namespace Kotonoha
{

	Prompt::Prompt(std::vector<std::string> prompt, int *promptResult, Uint64 start, Uint64 end, Kotonoha_time *time)
		: prompt(std::move(prompt)), promptResult(promptResult), time(time), startTime(start), endTime(end)
	{
		font = TTF_OpenFont("assets/fonts/ConcertOne-Regular.ttf", 36);
	}

	enum Kotonoha_Scene_Status Prompt::Render(KOTONOHA_SCENE_CALL)
	{
		if (userData == nullptr)
			return KOTONOHA_SCENE_NULL;

		auto *prompt = static_cast<Prompt *>(userData);

		// Verifica se o prompt est� no tempo certo para exibir
		bool inRange;
		int diff;
		Kotonoha_timeGetFromEvent(prompt->time, prompt->startTime, prompt->endTime, &inRange, &diff);
		if (!inRange)
			return KOTONOHA_SCENE_WAITING;

		SDL_RenderClear(render);

		// Coordenadas do mouse
		float mouseX = 0, mouseY = 0;
		SDL_GetMouseState(&mouseX, &mouseY);

		// Itera sobre cada op��o do prompt
		for (int i = 0; i < prompt->prompt.size(); i++)
		{
			SDL_Color color = {0, 0, 0};
			SDL_Color bgColor = {255, 255, 255};
			SDL_Rect rect = {0, 0, 0, 0};

			// Define tamanho e posi��o do ret�ngulo de cada op��o
			TTF_GetStringSize(prompt->font, prompt->prompt[i].c_str(), prompt->prompt[i].size(), &rect.w, &rect.h);
			rect.x = (target->w - rect.w) * i;
			rect.y = (target->h - rect.h) / 2;

			// Verifica se o mouse est� sobre o ret�ngulo e captura eventos
			if (mouseX > rect.x && mouseX < (rect.x + rect.w) && mouseY > rect.y && mouseY < (rect.y + rect.h))
			{
				void *persistent = nullptr;
				do
				{
					SDL_Event event = Kotonoha_eventRead(eventQueu, &persistent);
					if (event.type == SDL_EVENT_MOUSE_BUTTON_DOWN && event.button.button == SDL_BUTTON_LEFT)
					{
						if (*(prompt->promptResult) == i)
						{
							*(prompt->promptResult) = -1;
							break;
						}
						*(prompt->promptResult) = i;
						break;
					}
				} while (persistent != nullptr);

				color.b = 255; // Altera a cor quando o mouse est� sobre a op��o
			}

			// Indica a op��o selecionada
			if (*(prompt->promptResult) == i)
			{
				color.g = 255;
			}

			// Renderiza o texto da op��o
			SDL_Surface *surface = TTF_RenderText_Shaded(prompt->font, prompt->prompt[i].c_str(), prompt->prompt[i].size(), color, bgColor);
			SDL_Texture *texture = SDL_CreateTextureFromSurface(render, surface);
			SDL_FRect frect = {static_cast<float>(rect.x), static_cast<float>(rect.y), static_cast<float>(rect.w), static_cast<float>(rect.h)};

			SDL_RenderTexture(render, texture, nullptr, &frect);
			SDL_DestroySurface(surface);
			SDL_DestroyTexture(texture);
		}
		return KOTONOHA_SCENE_DRAW_OVERLAYED;
	}

	Prompt::~Prompt()
	{
		TTF_CloseFont(font);
	}

}