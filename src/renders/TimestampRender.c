#include <Kotonoha/renders/TimestampRender.h>
#include <Kotonoha/utils/Time.h>
struct Timestamp_common
{
	TTF_Font *font;
	SDL_Texture *texture;
	SDL_FRect rect;
	SDL_Color color;
	char text[32];
	Uint64 lastTime;
};

static struct Timestamp_common *timestampCommon = NULL;

static SDL_Texture *
CreateTimestampTexture(SDL_Renderer *render, const char *text, TTF_Font *font, SDL_Color color,
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

enum Kotonoha_Scene_Status Kotonoha_TimestampRender(KOTONOHA_SCENE_CALL)
{
	if(userData == NULL)
	{
		return KOTONOHA_SCENE_NULL;
	}

	struct Kotonoha_time *time = (struct Kotonoha_time *)userData;

	// Inicializa a estrutura Timestamp_common na primeira chamada
	if (timestampCommon == NULL)
	{
		timestampCommon = (struct Timestamp_common *)SDL_malloc(sizeof(struct Timestamp_common));
		if (!timestampCommon)
		{
			SDL_LogError(SDL_LOG_CATEGORY_ERROR, "Failed to allocate FPS_common");
			return KOTONOHA_SCENE_NULL;
		}

		// Carregar a fonte para o contador de FPS
		timestampCommon->font = TTF_OpenFont("assets/fonts/ConcertOne-Regular.ttf", 28);
		if (!timestampCommon->font)
		{
			SDL_LogError(SDL_LOG_CATEGORY_ERROR, "Failed to load font: %s", SDL_GetError());
			SDL_free(timestampCommon);
			timestampCommon = NULL;
			return KOTONOHA_SCENE_NULL;
		}

		// Configuração inicial da estrutura
		timestampCommon->color = (SDL_Color){0, 255, 255, 255};
		timestampCommon->lastTime = SDL_GetTicks();
		timestampCommon->rect = (SDL_FRect){0, 0, 0, 0};
		timestampCommon->texture = NULL;
	}

	// Atualizar Timestamp a cada meio segundo
	if (SDL_GetTicks() - timestampCommon->lastTime >= 100)
	{
		Uint64 timestamp = Kotonoha_timeGet(time);

		// Converter milissegundos para segundos
		Uint64 totalSeconds = timestamp / 1000;

		// Calcular horas, minutos e segundos
		Uint64 hours = totalSeconds / 3600;
		Uint64 minutes = (totalSeconds % 3600) / 60;
		Uint64 seconds = totalSeconds % 60;

			// Formatar o texto de Timestamp no formato 00:00:00
		SDL_snprintf(timestampCommon->text, sizeof(timestampCommon->text), 
					"%02lu:%02lu:%02lu:TS", 
					(unsigned long)hours, 
					(unsigned long)minutes, 
					(unsigned long)seconds);

		// Destruir a textura anterior se ela existir
		if (timestampCommon->texture != NULL)
		{
			SDL_DestroyTexture(timestampCommon->texture);
		}

		// Destruir a textura anterior se ela existir
		if (timestampCommon->texture != NULL)
		{
			SDL_DestroyTexture(timestampCommon->texture);
		}

		// Calcular o tamanho do texto para ajustar o retângulo
		int textWidth = 0, textHeight = 0;
		size_t textLen = SDL_strlen(timestampCommon->text);
		TTF_GetStringSize(timestampCommon->font, timestampCommon->text, textLen, &textWidth, &textHeight);
		timestampCommon->rect.w = (float)textWidth;
		timestampCommon->rect.h = (float)textHeight;

		// Criar uma nova textura para o texto
		timestampCommon->texture = CreateTimestampTexture(render, timestampCommon->text, timestampCommon->font,
											  timestampCommon->color, textLen);
		if (!timestampCommon->texture)
		{
			return KOTONOHA_SCENE_NULL;
		}
		timestampCommon->lastTime = SDL_GetTicks();
	}

	// Incrementar o contador de frames

	// Renderizar a textura do
	if (timestampCommon->texture)
	{
		int windowWidth, windowHeight;
		SDL_GetWindowSizeInPixels(window, &windowWidth, &windowHeight);
		timestampCommon->rect.x = windowWidth - timestampCommon->rect.w;
		SDL_RenderTexture(render, timestampCommon->texture,NULL, &timestampCommon->rect);
	}

	return KOTONOHA_SCENE_DRAW_OVERLAYED;
}