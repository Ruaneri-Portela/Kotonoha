#include <Kotonoha/renders/TextRender.h>
#include <SDL3/SDL_render.h>

// Função para desenhar a textura a partir de uma imagem ASS
inline static SDL_Texture* Kotonoha_TextRenderDrawText(SDL_Renderer* render,
	ASS_Image* img) {
	if (img == NULL || img->bitmap == NULL || img->w <= 0 || img->h <= 0) {
		return NULL;
	}

	SDL_Texture* texture =
		SDL_CreateTexture(render, SDL_PIXELFORMAT_RGBA32,
			SDL_TEXTUREACCESS_STREAMING, img->w, img->h);
	if (texture == NULL) {
		SDL_LogError(SDL_LOG_CATEGORY_ERROR, "SDL_CreateTexture failed: %s",
			SDL_GetError());
		return NULL;
	}

	if (!SDL_SetTextureBlendMode(texture, SDL_BLENDMODE_BLEND)) {
		SDL_LogError(SDL_LOG_CATEGORY_ERROR,
			"SDL_SetTextureBlendMode failed: %s", SDL_GetError());
		SDL_DestroyTexture(texture);
		return NULL;
	}

	Uint8* pixels = NULL;
	int pitch = 0;
	if (!SDL_LockTexture(texture, NULL, (void**)&pixels, &pitch)) {
		SDL_LogError(SDL_LOG_CATEGORY_ERROR, "SDL_LockTexture failed: %s",
			SDL_GetError());
		SDL_DestroyTexture(texture);
		return NULL;
	}

	unsigned char* bitmap = img->bitmap;
	const Uint8 red = (Uint8)((img->color >> 24) & 0xff);
	const Uint8 green = (Uint8)((img->color >> 16) & 0xff);
	const Uint8 blue = (Uint8)((img->color >> 8) & 0xff);
	const Uint8 colorAlpha = (Uint8)(255 - (img->color & 0xff));
	for (int y = 0; y < img->h; ++y) {
		Uint8* rowPixels = pixels + (y * pitch);
		for (int x = 0; x < img->w; ++x) {
			const Uint8 coverage = bitmap[x];
			const Uint8 alpha = (Uint8)((coverage * colorAlpha) / 255);
			rowPixels[x * 4 + 0] = red;
			rowPixels[x * 4 + 1] = green;
			rowPixels[x * 4 + 2] = blue;
			rowPixels[x * 4 + 3] = alpha;
		}
		bitmap += img->stride;
	}

	SDL_UnlockTexture(texture);
	return texture;
}

// Função para inicializar os dados de renderização de texto
struct Kotonoha_subtitles*
	Kotonoha_TextRenderInit(struct Kotonoha_time* time,
		struct Kotonoha_Game* gameCtx) {
	struct Kotonoha_subtitles* object =
		SDL_malloc(sizeof(struct Kotonoha_subtitles));
	if (!object) {
		SDL_LogError(SDL_LOG_CATEGORY_ERROR,
			"Failed to allocate memory for Kotonoha_subtitles");
		return NULL;
	}

	// Inicializa os dados do objeto de renderização de texto
	object->ass_library = gameCtx->ass_library;
	object->ass_renderer = gameCtx->ass_renderer;
	object->track = NULL;
	object->time = time;
	object->subTexture = NULL;

	return object;
}

// Função para destruir os dados de renderização de texto e liberar a memória
void Kotonoha_TextRenderShutdown(struct Kotonoha_subtitles** object) {
	if (object && *object) {
		if ((*object)->subTexture) {
			SDL_DestroyTexture((*object)->subTexture);
		}
		if ((*object)->track) {
			ass_free_track((*object)->track);
		}
		SDL_free(*object);
		*object = NULL;
	}
}

// Função principal para renderizar o texto
enum Kotonoha_Scene_Status Kotonoha_TextRenderDraw(KOTONOHA_SCENE_CALL) {
	(void)window;
	(void)eventQueu;

	if (!userData) {
		return KOTONOHA_SCENE_FATAL_ERROR;
	}

	struct Kotonoha_subtitles* environment =
		(struct Kotonoha_subtitles*)userData;

	if (target == NULL || !environment->track) {
		return KOTONOHA_SCENE_NULL;
	}

	Uint8 targetR = 0;
	Uint8 targetG = 0;
	Uint8 targetB = 0;
	Uint8 targetA = 0;
	SDL_BlendMode targetBlendMode = SDL_BLENDMODE_NONE;
	if (!SDL_GetRenderDrawColor(render, &targetR, &targetG, &targetB,
			&targetA) ||
		!SDL_GetRenderDrawBlendMode(render, &targetBlendMode) ||
		!SDL_SetRenderDrawBlendMode(render, SDL_BLENDMODE_NONE) ||
		!SDL_SetRenderDrawColor(render, 0, 0, 0, 0) ||
		!SDL_RenderClear(render)) {
		SDL_LogError(SDL_LOG_CATEGORY_ERROR,
			"Failed to clear persistent subtitle canvas: %s", SDL_GetError());
		SDL_SetRenderDrawColor(render, targetR, targetG, targetB, targetA);
		SDL_SetRenderDrawBlendMode(render, targetBlendMode);
		return KOTONOHA_SCENE_FATAL_ERROR;
	}
	if (!SDL_SetRenderDrawColor(render, targetR, targetG, targetB, targetA) ||
		!SDL_SetRenderDrawBlendMode(render, targetBlendMode)) {
		SDL_LogError(SDL_LOG_CATEGORY_ERROR,
			"Failed to restore renderer state after clearing subtitle canvas: %s",
			SDL_GetError());
		return KOTONOHA_SCENE_FATAL_ERROR;
	}

	ass_set_storage_size(environment->ass_renderer, target->w, target->h);
	ass_set_frame_size(environment->ass_renderer, target->w, target->h);

	int asChanged = 0;
	ASS_Image* frame =
		ass_render_frame(environment->ass_renderer, environment->track,
			Kotonoha_timeGet(environment->time), &asChanged);

	if (environment->subTexture != NULL) {
		SDL_DestroyTexture(environment->subTexture);
		environment->subTexture = NULL;
	}

	environment->subTexture =
		SDL_CreateTexture(render, SDL_PIXELFORMAT_RGBA32,
			SDL_TEXTUREACCESS_TARGET, target->w, target->h);
	if (environment->subTexture == NULL) {
		SDL_LogError(SDL_LOG_CATEGORY_ERROR, "Failed to create subTexture: %s",
			SDL_GetError());
		return KOTONOHA_SCENE_FATAL_ERROR;
	}
	if (!SDL_SetTextureBlendMode(environment->subTexture, SDL_BLENDMODE_BLEND)) {
		SDL_LogError(SDL_LOG_CATEGORY_ERROR,
			"Failed to set subtitle texture blend mode: %s", SDL_GetError());
		SDL_DestroyTexture(environment->subTexture);
		environment->subTexture = NULL;
		return KOTONOHA_SCENE_FATAL_ERROR;
	}

	if (!SDL_SetRenderTarget(render, environment->subTexture)) {
		SDL_LogError(SDL_LOG_CATEGORY_ERROR,
			"Failed to set render target for subtitles: %s", SDL_GetError());
		SDL_DestroyTexture(environment->subTexture);
		environment->subTexture = NULL;
		return KOTONOHA_SCENE_FATAL_ERROR;
	}

	Uint8 oldR = 0, oldG = 0, oldB = 0, oldA = 0;
	if (!SDL_GetRenderDrawColor(render, &oldR, &oldG, &oldB, &oldA) ||
		!SDL_SetRenderDrawBlendMode(render, SDL_BLENDMODE_NONE) ||
		!SDL_SetRenderDrawColor(render, 0, 0, 0, 0) ||
		!SDL_RenderClear(render)) {
		SDL_LogError(SDL_LOG_CATEGORY_ERROR,
			"Failed to clear subtitle target: %s", SDL_GetError());
		SDL_SetRenderTarget(render, target);
		SDL_SetRenderDrawColor(render, oldR, oldG, oldB, oldA);
		SDL_SetRenderDrawBlendMode(render, targetBlendMode);
		SDL_DestroyTexture(environment->subTexture);
		environment->subTexture = NULL;
		return KOTONOHA_SCENE_FATAL_ERROR;
	}
	if (!SDL_SetRenderDrawColor(render, oldR, oldG, oldB, oldA) ||
		!SDL_SetRenderDrawBlendMode(render, targetBlendMode)) {
		SDL_LogError(SDL_LOG_CATEGORY_ERROR,
			"Failed to restore renderer state after subtitle clear: %s",
			SDL_GetError());
		SDL_SetRenderTarget(render, target);
		SDL_DestroyTexture(environment->subTexture);
		environment->subTexture = NULL;
		return KOTONOHA_SCENE_FATAL_ERROR;
	}

	bool renderedAny = false;
	for (ASS_Image* img = frame; img != NULL; img = img->next) {
		SDL_Texture* texture = Kotonoha_TextRenderDrawText(render, img);
		if (texture == NULL) {
			continue;
		}

		SDL_FRect dst = { (float)img->dst_x, (float)img->dst_y,
			(float)img->w, (float)img->h };
		if (SDL_RenderTexture(render, texture, NULL, &dst)) {
			renderedAny = true;
		}
		SDL_DestroyTexture(texture);
	}

	if (!SDL_SetRenderTarget(render, target)) {
		SDL_LogError(SDL_LOG_CATEGORY_ERROR,
			"Failed to restore render target after subtitle draw: %s",
			SDL_GetError());
		SDL_DestroyTexture(environment->subTexture);
		environment->subTexture = NULL;
		return KOTONOHA_SCENE_FATAL_ERROR;
	}

	if (renderedAny) {
		if (!SDL_RenderTexture(render, environment->subTexture, NULL, NULL)) {
			SDL_LogError(SDL_LOG_CATEGORY_ERROR,
				"Failed to composite subtitle texture: %s", SDL_GetError());
			return KOTONOHA_SCENE_FATAL_ERROR;
		}
	}
	else {
		SDL_DestroyTexture(environment->subTexture);
		environment->subTexture = NULL;
		return KOTONOHA_SCENE_WAITING;
	}

	return KOTONOHA_SCENE_DRAW_OVERLAYED;
}
