#include <Kotonoha/renders/FPSRender.h>

struct FPS_common {
	TTF_Font* font;
	SDL_Texture* texture;
	SDL_Texture* shadow;
	SDL_FRect rect;
	SDL_Color color;
	SDL_Color shadowColor;
	char text[32];
	Uint64 lastTime;
	size_t count;
};

static struct FPS_common* fpsCommon = NULL;

static SDL_Texture* CreateFPSTexture(SDL_Renderer* render, const char* text,
	TTF_Font* font, SDL_Color color,
	size_t textLen) {
	SDL_Surface* surface = TTF_RenderText_Blended(font, text, textLen, color);
	if (!surface) {
		SDL_LogError(SDL_LOG_CATEGORY_ERROR, "Failed to render text: %s",
			SDL_GetError());
		return NULL;
	}

	SDL_Texture* texture = SDL_CreateTextureFromSurface(render, surface);
	SDL_DestroySurface(surface);

	if (!texture) {
		SDL_LogError(SDL_LOG_CATEGORY_ERROR, "Failed to create texture: %s",
			SDL_GetError());
	}

	return texture;
}

static bool UpdateFPSTexture(SDL_Renderer* render) {
	int textWidth = 0, textHeight = 0;
	size_t textLen = SDL_strlen(fpsCommon->text);

	if (!TTF_GetStringSize(fpsCommon->font, fpsCommon->text, textLen, &textWidth,
		&textHeight)) {
		SDL_LogError(SDL_LOG_CATEGORY_ERROR, "TTF_GetStringSize failed: %s",
			SDL_GetError());
		return false;
	}

	SDL_Texture* newShadow =
		CreateFPSTexture(render, fpsCommon->text, fpsCommon->font,
			fpsCommon->shadowColor, textLen);
	if (!newShadow) {
		return false;
	}

	SDL_Texture* newTexture = CreateFPSTexture(
		render, fpsCommon->text, fpsCommon->font, fpsCommon->color, textLen);
	if (!newTexture) {
		SDL_DestroyTexture(newShadow);
		return false;
	}

	if (fpsCommon->shadow != NULL) {
		SDL_DestroyTexture(fpsCommon->shadow);
	}

	if (fpsCommon->texture != NULL) {
		SDL_DestroyTexture(fpsCommon->texture);
	}

	fpsCommon->shadow = newShadow;
	fpsCommon->texture = newTexture;
	fpsCommon->rect.w = (float)textWidth;
	fpsCommon->rect.h = (float)textHeight;

	return true;
}

enum Kotonoha_Scene_Status Kotonoha_FPSRender(KOTONOHA_SCENE_CALL) {
	if (fpsCommon == NULL) {
		fpsCommon = (struct FPS_common*)SDL_malloc(sizeof(struct FPS_common));
		if (!fpsCommon) {
			SDL_LogError(SDL_LOG_CATEGORY_ERROR, "Failed to allocate FPS_common");
			return KOTONOHA_SCENE_NULL;
		}

		SDL_zero(*fpsCommon);

		fpsCommon->font = TTF_OpenFont("assets/fonts/ConcertOne-Regular.ttf", 28);
		if (!fpsCommon->font) {
			SDL_LogError(SDL_LOG_CATEGORY_ERROR, "Failed to load font: %s",
				SDL_GetError());
			SDL_free(fpsCommon);
			fpsCommon = NULL;
			return KOTONOHA_SCENE_NULL;
		}

		fpsCommon->color = (SDL_Color){ 255, 0, 255, 255 };
		fpsCommon->shadowColor = (SDL_Color){ 0, 0, 0, 160 };
		fpsCommon->lastTime = 0;
		fpsCommon->rect = (SDL_FRect){ 0, 0, 0, 0 };
		fpsCommon->count = 0;
		fpsCommon->texture = NULL;
		fpsCommon->shadow = NULL;

		SDL_snprintf(fpsCommon->text, sizeof(fpsCommon->text), "FPS: 0 - 0.00ms");
		if (!UpdateFPSTexture(render)) {
			return KOTONOHA_SCENE_NULL;
		}
	}

	if (userData != NULL) {
		SDL_RenderClear(render);
	}

	if (SDL_GetTicks() - fpsCommon->lastTime >= 1000) {
		float ms =
			(fpsCommon->count > 0) ? (1000.0f / (float)fpsCommon->count) : 0.0f;

		SDL_snprintf(fpsCommon->text, sizeof(fpsCommon->text), "FPS: %zu - %.2fms",
			fpsCommon->count, ms);

		if (!UpdateFPSTexture(render)) {
			return KOTONOHA_SCENE_NULL;
		}

		fpsCommon->count = 0;
		fpsCommon->lastTime = SDL_GetTicks();
	}

	fpsCommon->count++;

	if (fpsCommon->texture) {
		float shadowOffsetX = 2.0f;
		float shadowOffsetY = 2.0f;

		if (fpsCommon->shadow) {
			SDL_FRect shadowRect = fpsCommon->rect;
			shadowRect.x += shadowOffsetX;
			shadowRect.y += shadowOffsetY;
			SDL_RenderTexture(render, fpsCommon->shadow, NULL, &shadowRect);
		}

		SDL_RenderTexture(render, fpsCommon->texture, NULL, &fpsCommon->rect);
	}

	return KOTONOHA_SCENE_DRAW_OVERLAYED;
}