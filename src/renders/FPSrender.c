#include <Kotonoha/renders/FPSrender.h>

struct FPS_common
{
	TTF_Font *font;
	SDL_Texture *texture;
	SDL_FRect rect;
	SDL_Color color;
	char text[32];
	Uint64 lastTime;
	size_t count;
};

static struct FPS_common *fpsCommon = NULL;

static SDL_Texture *
CreateFPSTexture(SDL_Renderer *render, const char *text, TTF_Font *font, SDL_Color color,
				 size_t textLen)
{
	// Criar uma superfície para o texto
	SDL_Surface *surface = TTF_RenderText_Blended(font, text, textLen, color);
	if (!surface)
	{
		SDL_LogError(SDL_LOG_CATEGORY_ERROR, "Failed to render text: %s", SDL_GetError());
		return NULL;
	}

	// Criar a textura a partir da superfície
	SDL_Texture *texture = SDL_CreateTextureFromSurface(render, surface);
	SDL_DestroySurface(surface);
	if (!texture)
	{
		SDL_LogError(SDL_LOG_CATEGORY_ERROR, "Failed to create texture: %s", SDL_GetError());
	}

	return texture;
}

enum Kotonoha_Scene_Status Kotonoha_FPSrender(KOTONOHA_SCENE_CALL)
{
	// Inicializa a estrutura FPS_common na primeira chamada
	if (fpsCommon == NULL)
	{
		fpsCommon = (struct FPS_common *)SDL_malloc(sizeof(struct FPS_common));
		if (!fpsCommon)
		{
			SDL_LogError(SDL_LOG_CATEGORY_ERROR, "Failed to allocate FPS_common");
			return KOTONOHA_SCENE_NULL;
		}

		// Carregar a fonte para o contador de FPS
		fpsCommon->font = TTF_OpenFont("assets/fonts/ConcertOne-Regular.ttf", 28);
		if (!fpsCommon->font)
		{
			SDL_LogError(SDL_LOG_CATEGORY_ERROR, "Failed to load font: %s", SDL_GetError());
			SDL_free(fpsCommon);
			fpsCommon = NULL;
			return KOTONOHA_SCENE_NULL;
		}

		// Configuração inicial da estrutura
		fpsCommon->color = (SDL_Color){255, 0, 255, 255};
		fpsCommon->lastTime = SDL_GetTicks();
		fpsCommon->rect = (SDL_FRect){0, 0, 0, 0};
		fpsCommon->count = 0;
		fpsCommon->texture = NULL;
	}

	// Limpa o renderizador se houver dados de usuário
	if (userData != NULL)
	{
		SDL_RenderClear(render);
	}

	// Atualizar FPS a cada segundo
	if (SDL_GetTicks() - fpsCommon->lastTime >= 1000)
	{
		// Formatar o texto de FPS
		SDL_snprintf(fpsCommon->text, sizeof(fpsCommon->text), "FPS: %zu - %.2fms",
					 fpsCommon->count, 1000.0f / (float)fpsCommon->count);

		// Destruir a textura anterior se ela existir
		if (fpsCommon->texture != NULL)
		{
			SDL_DestroyTexture(fpsCommon->texture);
		}

		// Calcular o tamanho do texto para ajustar o retângulo
		int textWidth = 0, textHeight = 0;
		size_t textLen = SDL_strlen(fpsCommon->text);
		TTF_GetStringSize(fpsCommon->font, fpsCommon->text, textLen, &textWidth, &textHeight);
		fpsCommon->rect.w = (float)textWidth;
		fpsCommon->rect.h = (float)textHeight;

		// Criar uma nova textura para o texto
		fpsCommon->texture = CreateFPSTexture(render, fpsCommon->text, fpsCommon->font,
											  fpsCommon->color, textLen);
		if (!fpsCommon->texture)
		{
			return KOTONOHA_SCENE_NULL;
		}

		// Reiniciar o contador e atualizar o tempo
		fpsCommon->count = 0;
		fpsCommon->lastTime = SDL_GetTicks();
	}

	// Incrementar o contador de frames
	fpsCommon->count++;

	// Renderizar a textura do FPS
	if (fpsCommon->texture)
	{
		SDL_RenderTexture(render, fpsCommon->texture, NULL, &fpsCommon->rect);
	}

	return KOTONOHA_SCENE_DRAW_OVERLAYED;
}