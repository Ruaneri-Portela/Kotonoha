#include <Kotonoha/Gameplay.hpp>

namespace Kotonoha
{
	void Gameplay::UpdateCanvasSize(SDL_Window *window)
	{
		int width = 0, height = 0;
		SDL_GetWindowSizeInPixels(window, &width, &height);

		// Atualiza tamanho da janela apenas se necessário
		if (width != windowWidth || height != windowHeight)
		{
			windowWidth = width;
			windowHeight = height;
		}
		else
			return;

		SDL_FRect place = {0, 0, 0, 0};
		double windowAspectRatio = static_cast<float>(width) / static_cast<float>(height);

		// Ajuste da área de desenho com base na proporção da janela e do conteúdo
		if (windowAspectRatio < aspectRatio)
		{
			// Janela mais alta que o conteúdo: barras horizontais
			place.w = static_cast<float>(width);
			place.h = static_cast<float>(width) / aspectRatio;
			place.x = 0.0f;
			place.y = static_cast<float>(height - place.h) / 2.0f; // Centralização vertical
		}
		else
		{
			// Janela mais larga que o conteúdo: barras verticais
			place.h = static_cast<float>(height);
			place.w = static_cast<float>(height) * aspectRatio;
			place.y = 0.0f;
			place.x = static_cast<float>(width - place.w) / 2.0f; // Centralização horizontal
		}

		// Atualiza o canvas para diferentes tipos de renderização
		drawCanvas->UpdateCanva(nullptr, -1, place);
	}

	Gameplay::Gameplay(const char *scriptPath, struct Kotonoha_Game *gameContext)
	{
		// Inicialização dos objetos de evento e renderização
		this->tm = Kotonoha_timeNew(true);
		this->sb = Kotonoha_TextRenderInit(this->tm, gameContext);

		// Inicializa vídeo e imagem, e registra o canvas
		this->video = new Video(this->tm);
		this->image = new Image(this->tm);
		this->audio = new Audio(static_cast<Sound *>(gameContext->soundCtx), this->tm);
		this->drawCanvas = new Canvas();

		this->drawCanvas->RegisterCanva(this->image->Render, 0, {0, 0, static_cast<float>(windowWidth), static_cast<float>(windowHeight)}, this->image);
		this->drawCanvas->RegisterCanva(this->video->Render, 1, {0, 0, static_cast<float>(windowWidth), static_cast<float>(windowHeight)}, this->video);
		this->drawCanvas->RegisterCanva(Kotonoha_TextRenderDraw, 3, {0, 0, static_cast<float>(windowWidth), static_cast<float>(windowHeight)}, this->sb);

		// Inicializa o gerenciador de eventos
		this->eventManager = new Event(scriptPath, this, gameContext);
#ifdef ANDROID
		playOnlyOnFocus = true;
		lastPauseStatus = gameContext->paused;
#endif
	}

	SDL_AppResult Gameplay::Main(struct Kotonoha_Game *gameContext)
	{
		// Verifica condição de término
		if (eventManager->CheckEnd(this))
		{
			return SDL_APP_SUCCESS;
		}

		// Checa se há um prompt a ser exibido
		if (this->putPrompt)
		{
			drawCanvas->RegisterCanva(this->prompt->Render, 2,
									  {0, 0, static_cast<float>(windowWidth),
									   static_cast<float>(windowHeight)},
									  this->prompt);
			this->putPrompt = false;
		}

		// Atualiza o tamanho do canvas e controla pausa/resumo do tempo
		UpdateCanvasSize(gameContext->window);
		gameContext->paused ? Kotonoha_timePause(this->tm) : Kotonoha_timeResume(this->tm);

		// Processamento de eventos
		void *persistent = nullptr;
		do
		{
			SDL_Event event = Kotonoha_eventRead(&gameContext->eventQueu, &persistent);

			// Tratamento de eventos de teclado
			switch (event.type)
			{
			case SDL_EVENT_KEY_DOWN:
				switch (event.key.key)
				{
				case SDLK_P:
					gameContext->paused = !this->tm->isPaused;
					break;
				case SDLK_I:
				{
					float timeInSeconds = static_cast<float>(Kotonoha_timeGet(this->tm)) / 1000.0f;
					SDL_Log("Time: %.2f, is paused %d\n", timeInSeconds, this->tm->isPaused);
					break;
				}
				case SDLK_S:
				{
					Kotonoha_eventFree(&gameContext->eventQueu);
					return SDL_APP_SUCCESS;
				}
				case SDLK_N:
					this->tm->seekTime += 5000;
					break;
				default:
					break;
				}
				break;
			case SDL_EVENT_WINDOW_FOCUS_GAINED:
				if (playOnlyOnFocus)
				{
					gameContext->paused = lastPauseStatus;
				}
				break;
			case SDL_EVENT_WINDOW_FOCUS_LOST:
				if (playOnlyOnFocus)
				{
					lastPauseStatus = gameContext->paused;
					gameContext->paused = true;
				}
				break;
			default:
				break;
			}
		} while (persistent != nullptr);

		// Renderiza o canvas e libera a fila de eventos
		SDL_AppResult result = drawCanvas->RenderCanvas(gameContext->window, gameContext->render, &gameContext->eventQueu);
		Kotonoha_eventFree(&gameContext->eventQueu);

		return result;
	}

	Gameplay::~Gameplay()
	{
		// Liberação de recursos alocados
		delete this->eventManager;
		delete this->drawCanvas;
		delete this->video;
		delete this->image;
		delete this->audio;
		if (prompt)
			delete prompt;
		Kotonoha_TextRenderShutdown(&(this->sb));
		Kotonoha_timeDestroy(this->tm);
	}
}
