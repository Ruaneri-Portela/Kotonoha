#include <Kotonoha/renders/FPSrender.h>
#include <Kotonoha/utils/OverlayRenderUtils.h>

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
	float windowScale;
};

static struct FPS_common* fpsCommon = NULL;

static void DestroyFPSTextures(void);

void Kotonoha_FPSRenderShutdown(void) {
	if (fpsCommon == NULL) {
		return;
	}
	DestroyFPSTextures();
	if (fpsCommon->font != NULL) {
		TTF_CloseFont(fpsCommon->font);
	}
	SDL_free(fpsCommon);
	fpsCommon = NULL;
}

static void DestroyFPSTextures(void) {
	if (fpsCommon->shadow != NULL) {
		SDL_DestroyTexture(fpsCommon->shadow);
		fpsCommon->shadow = NULL;
	}
	if (fpsCommon->texture != NULL) {
		SDL_DestroyTexture(fpsCommon->texture);
		fpsCommon->texture = NULL;
	}
}

static bool UpdateFPSTexture(SDL_Renderer* render) {
	SDL_Texture* newShadow = NULL;
	SDL_Texture* newTexture = NULL;
	float textWidth = 0.0f;
	float textHeight = 0.0f;

	if (!Kotonoha_OverlayCreateTextTexture(render, fpsCommon->font,
			fpsCommon->text, fpsCommon->shadowColor, &newShadow,
			&textWidth, &textHeight)) {
		return false;
	}
	if (!Kotonoha_OverlayCreateTextTexture(render, fpsCommon->font,
			fpsCommon->text, fpsCommon->color, &newTexture,
			&textWidth, &textHeight)) {
		SDL_DestroyTexture(newShadow);
		return false;
	}

	DestroyFPSTextures();
	fpsCommon->shadow = newShadow;
	fpsCommon->texture = newTexture;
	fpsCommon->rect.w = textWidth;
	fpsCommon->rect.h = textHeight;
	return true;
}

static bool UpdateFPSFont(SDL_Renderer* render, float windowScale) {
	TTF_Font* newFont = Kotonoha_OverlayOpenFont(
		"assets/fonts/ConcertOne-Regular.ttf", 28.0f, windowScale);
	if (newFont == NULL) {
		return false;
	}

	TTF_Font* oldFont = fpsCommon->font;
	const float oldScale = fpsCommon->windowScale;
	fpsCommon->font = newFont;
	fpsCommon->windowScale = windowScale;
	if (!UpdateFPSTexture(render)) {
		fpsCommon->font = oldFont;
		fpsCommon->windowScale = oldScale;
		TTF_CloseFont(newFont);
		return false;
	}
	if (oldFont != NULL) {
		TTF_CloseFont(oldFont);
	}
	return true;
}

enum Kotonoha_Scene_Status Kotonoha_FPSRender(KOTONOHA_SCENE_CALL) {
	(void)eventQueu;
	(void)target;

	int outputWidth = 0;
	int outputHeight = 0;
	float windowScale = 1.0f;
	if (!Kotonoha_OverlayGetMetrics(
			window, render, &outputWidth, &outputHeight, &windowScale)) {
		return KOTONOHA_SCENE_NULL;
	}

	if (fpsCommon == NULL) {
		fpsCommon = (struct FPS_common*)SDL_calloc(1, sizeof(struct FPS_common));
		if (fpsCommon == NULL) {
			SDL_LogError(SDL_LOG_CATEGORY_ERROR,
				"Failed to allocate FPS renderer state");
			return KOTONOHA_SCENE_NULL;
		}

		fpsCommon->color = (SDL_Color){ 255, 0, 255, 255 };
		fpsCommon->shadowColor = (SDL_Color){ 0, 0, 0, 160 };
		fpsCommon->lastTime = SDL_GetTicks();
		SDL_snprintf(fpsCommon->text, sizeof(fpsCommon->text),
			"FPS: 0 - 0.00ms");
		if (!UpdateFPSFont(render, windowScale)) {
			SDL_free(fpsCommon);
			fpsCommon = NULL;
			return KOTONOHA_SCENE_NULL;
		}
	}
	else if (fpsCommon->windowScale != windowScale &&
		!UpdateFPSFont(render, windowScale)) {
		return KOTONOHA_SCENE_NULL;
	}

	if (userData != NULL) {
		SDL_RenderClear(render);
	}

	if (SDL_GetTicks() - fpsCommon->lastTime >= 1000) {
		const float milliseconds =
			fpsCommon->count > 0
			? 1000.0f / (float)fpsCommon->count
			: 0.0f;
		SDL_snprintf(fpsCommon->text, sizeof(fpsCommon->text),
			"FPS: %zu - %.2fms", fpsCommon->count, milliseconds);
		if (!UpdateFPSTexture(render)) {
			return KOTONOHA_SCENE_NULL;
		}

		fpsCommon->count = 0;
		fpsCommon->lastTime = SDL_GetTicks();
	}
	fpsCommon->count++;

	fpsCommon->rect.x = 8.0f * windowScale;
	fpsCommon->rect.y = 8.0f * windowScale;
	if (fpsCommon->rect.x + fpsCommon->rect.w > (float)outputWidth) {
		fpsCommon->rect.x = (float)outputWidth - fpsCommon->rect.w;
		if (fpsCommon->rect.x < 0.0f) {
			fpsCommon->rect.x = 0.0f;
		}
	}
	if (fpsCommon->rect.y + fpsCommon->rect.h > (float)outputHeight) {
		fpsCommon->rect.y = (float)outputHeight - fpsCommon->rect.h;
		if (fpsCommon->rect.y < 0.0f) {
			fpsCommon->rect.y = 0.0f;
		}
	}
	if (fpsCommon->shadow != NULL) {
		SDL_FRect shadowRect = fpsCommon->rect;
		shadowRect.x += 2.0f * windowScale;
		shadowRect.y += 2.0f * windowScale;
		if (!SDL_RenderTexture(render, fpsCommon->shadow, NULL, &shadowRect)) {
			SDL_LogError(SDL_LOG_CATEGORY_RENDER,
				"Failed to draw FPS shadow: %s", SDL_GetError());
		}
	}
	if (fpsCommon->texture != NULL &&
		!SDL_RenderTexture(render, fpsCommon->texture, NULL, &fpsCommon->rect)) {
		SDL_LogError(SDL_LOG_CATEGORY_RENDER,
			"Failed to draw FPS text: %s", SDL_GetError());
	}

	return KOTONOHA_SCENE_DRAW_OVERLAYED;
}
