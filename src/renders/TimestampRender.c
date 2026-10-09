#include <Kotonoha/renders/TimestampRender.h>
#include <Kotonoha/utils/OverlayRenderUtils.h>
#include <Kotonoha/utils/Time.h>

struct Timestamp_common {
	TTF_Font* font;
	SDL_Texture* texture;
	SDL_Texture* shadow;
	SDL_FRect rect;
	SDL_Color color;
	SDL_Color shadowColor;
	char text[32];
	Uint64 lastTime;
	float windowScale;
};

static struct Timestamp_common* timestampCommon = NULL;

void Kotonoha_TimestampRenderShutdown(void) {
	if (timestampCommon == NULL) {
		return;
	}
	if (timestampCommon->texture != NULL) {
		SDL_DestroyTexture(timestampCommon->texture);
	}
	if (timestampCommon->shadow != NULL) {
		SDL_DestroyTexture(timestampCommon->shadow);
	}
	if (timestampCommon->font != NULL) {
		TTF_CloseFont(timestampCommon->font);
	}
	SDL_free(timestampCommon);
	timestampCommon = NULL;
}

static bool UpdateTimestampTexture(SDL_Renderer* render) {
	SDL_Texture* newTexture = NULL;
	SDL_Texture* newShadow = NULL;
	float textWidth = 0.0f;
	float textHeight = 0.0f;
	if (!Kotonoha_OverlayCreateTextTexture(render, timestampCommon->font,
			timestampCommon->text, timestampCommon->shadowColor, &newShadow,
			&textWidth, &textHeight)) {
		return false;
	}
	if (!Kotonoha_OverlayCreateTextTexture(render, timestampCommon->font,
			timestampCommon->text, timestampCommon->color, &newTexture,
			&textWidth, &textHeight)) {
		SDL_DestroyTexture(newShadow);
		return false;
	}

	if (timestampCommon->texture != NULL) {
		SDL_DestroyTexture(timestampCommon->texture);
	}
	if (timestampCommon->shadow != NULL) {
		SDL_DestroyTexture(timestampCommon->shadow);
	}
	timestampCommon->texture = newTexture;
	timestampCommon->shadow = newShadow;
	timestampCommon->rect.w = textWidth;
	timestampCommon->rect.h = textHeight;
	return true;
}

static bool UpdateTimestampFont(SDL_Renderer* render, float windowScale) {
	TTF_Font* newFont = Kotonoha_OverlayOpenFont(
		"assets/fonts/ConcertOne-Regular.ttf", 28.0f, windowScale);
	if (newFont == NULL) {
		return false;
	}

	TTF_Font* oldFont = timestampCommon->font;
	const float oldScale = timestampCommon->windowScale;
	timestampCommon->font = newFont;
	timestampCommon->windowScale = windowScale;
	if (!UpdateTimestampTexture(render)) {
		timestampCommon->font = oldFont;
		timestampCommon->windowScale = oldScale;
		TTF_CloseFont(newFont);
		return false;
	}
	if (oldFont != NULL) {
		TTF_CloseFont(oldFont);
	}
	return true;
}

enum Kotonoha_Scene_Status Kotonoha_TimestampRender(KOTONOHA_SCENE_CALL) {
	(void)eventQueu;
	(void)target;

	if (userData == NULL) {
		return KOTONOHA_SCENE_NULL;
	}

	int outputWidth = 0;
	int outputHeight = 0;
	float windowScale = 1.0f;
	if (!Kotonoha_OverlayGetMetrics(
			window, render, &outputWidth, &outputHeight, &windowScale)) {
		return KOTONOHA_SCENE_NULL;
	}

	if (timestampCommon == NULL) {
		timestampCommon =
			(struct Timestamp_common*)SDL_calloc(1, sizeof(struct Timestamp_common));
		if (timestampCommon == NULL) {
			SDL_LogError(SDL_LOG_CATEGORY_ERROR,
				"Failed to allocate timestamp renderer state");
			return KOTONOHA_SCENE_NULL;
		}

		timestampCommon->color = (SDL_Color){ 0, 255, 255, 255 };
		timestampCommon->shadowColor = (SDL_Color){ 0, 0, 0, 160 };
		SDL_snprintf(timestampCommon->text, sizeof(timestampCommon->text),
			"00:00:00 :TS");
		if (!UpdateTimestampFont(render, windowScale)) {
			SDL_free(timestampCommon);
			timestampCommon = NULL;
			return KOTONOHA_SCENE_NULL;
		}
	}
	else if (timestampCommon->windowScale != windowScale &&
		!UpdateTimestampFont(render, windowScale)) {
		return KOTONOHA_SCENE_NULL;
	}

	if (SDL_GetTicks() - timestampCommon->lastTime >= 100) {
		const Uint64 timestamp = Kotonoha_timeGet((struct Kotonoha_time*)userData);
		const Uint64 totalSeconds = timestamp / 1000;
		const Uint64 milliseconds = timestamp % 1000;
		const Uint64 minutes = totalSeconds / 60;
		const Uint64 seconds = totalSeconds % 60;
		const Uint64 deciseconds = milliseconds / 10;

		SDL_snprintf(timestampCommon->text, sizeof(timestampCommon->text),
			"%02lu:%02lu:%02lu :TS",
			(unsigned long)minutes,
			(unsigned long)seconds,
			(unsigned long)deciseconds);
		if (!UpdateTimestampTexture(render)) {
			return KOTONOHA_SCENE_NULL;
		}
		timestampCommon->lastTime = SDL_GetTicks();
	}

	timestampCommon->rect.x =
		(float)outputWidth - timestampCommon->rect.w - 8.0f * windowScale;
	timestampCommon->rect.y = 8.0f * windowScale;
	if (timestampCommon->rect.x < 0.0f) {
		timestampCommon->rect.x = 0.0f;
	}
	if (timestampCommon->rect.y + timestampCommon->rect.h >
		(float)outputHeight) {
		timestampCommon->rect.y =
			(float)outputHeight - timestampCommon->rect.h;
		if (timestampCommon->rect.y < 0.0f) {
			timestampCommon->rect.y = 0.0f;
		}
	}

	if (timestampCommon->shadow != NULL) {
		SDL_FRect shadowRect = timestampCommon->rect;
		shadowRect.x += 2.0f * windowScale;
		shadowRect.y += 2.0f * windowScale;
		if (!SDL_RenderTexture(render, timestampCommon->shadow,
				NULL, &shadowRect)) {
			SDL_LogError(SDL_LOG_CATEGORY_RENDER,
				"Failed to draw timestamp shadow: %s", SDL_GetError());
		}
	}
	if (timestampCommon->texture != NULL &&
		!SDL_RenderTexture(
			render, timestampCommon->texture, NULL, &timestampCommon->rect)) {
		SDL_LogError(SDL_LOG_CATEGORY_RENDER,
			"Failed to draw timestamp text: %s", SDL_GetError());
	}

	return KOTONOHA_SCENE_DRAW_OVERLAYED;
}
