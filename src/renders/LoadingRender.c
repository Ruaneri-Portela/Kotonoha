#include <Kotonoha/renders/LoadingRender.h>
#include <Kotonoha/utils/OverlayRenderUtils.h>


struct Loading_common {
    SDL_Texture* texture;
    SDL_Texture* shadow;
    float textWidth;
    float textHeight;
    float windowScale;
};


static struct Loading_common* loadingCommon = NULL;


void Kotonoha_LoadingRenderShutdown(void) {
    if (loadingCommon == NULL) {
        return;
    }

    if (loadingCommon->texture != NULL) {
        SDL_DestroyTexture(loadingCommon->texture);
    }

    if (loadingCommon->shadow != NULL) {
        SDL_DestroyTexture(loadingCommon->shadow);
    }

    SDL_free(loadingCommon);
    loadingCommon = NULL;
}


static bool InitializeLoadingRender(
    SDL_Renderer* render,
    TTF_Font* font,
    float windowScale) {

    if (render == NULL || font == NULL) {
        return false;
    }

    struct Loading_common* state =
        (struct Loading_common*)SDL_calloc(1, sizeof(struct Loading_common));

    if (state == NULL) {
        SDL_LogError(SDL_LOG_CATEGORY_RENDER,
            "Failed to allocate loading renderer state");
        return false;
    }

    state->windowScale = windowScale;

    if (!Kotonoha_OverlayCreateTextTexture(
            render,
            font,
            "Loading...",
            (SDL_Color){ 0, 0, 0, 160 },
            &state->shadow,
            &state->textWidth,
            &state->textHeight)) {
        SDL_free(state);
        return false;
    }

    if (!Kotonoha_OverlayCreateTextTexture(
            render,
            font,
            "Loading...",
            (SDL_Color){ 255, 255, 255, 255 },
            &state->texture,
            &state->textWidth,
            &state->textHeight)) {
        SDL_DestroyTexture(state->shadow);
        SDL_free(state);
        return false;
    }

    loadingCommon = state;
    return true;
}


enum Kotonoha_Scene_Status Kotonoha_LoadingRender(KOTONOHA_SCENE_CALL) {
    (void)eventQueu;
    (void)target;

    TTF_Font* font = (TTF_Font*)userData;

    if (font == NULL) {
        return KOTONOHA_SCENE_NULL;
    }

    int width = 0;
    int height = 0;
    float windowScale = 1.0f;

    if (!Kotonoha_OverlayGetMetrics(
            window, render, &width, &height, &windowScale)) {
        return KOTONOHA_SCENE_NULL;
    }

    if (loadingCommon == NULL ||
        loadingCommon->windowScale != windowScale) {
        Kotonoha_LoadingRenderShutdown();

        if (!InitializeLoadingRender(render, font, windowScale)) {
            return KOTONOHA_SCENE_NULL;
        }
    }

    SDL_FRect overlay = {
        0.0f, 0.0f, (float)width, (float)height
    };

    if (!SDL_SetRenderDrawBlendMode(render, SDL_BLENDMODE_BLEND) ||
        !SDL_SetRenderDrawColor(render, 0, 0, 0, 190) ||
        !SDL_RenderFillRect(render, &overlay)) {
        SDL_LogError(SDL_LOG_CATEGORY_RENDER,
            "Failed to draw loading overlay: %s", SDL_GetError());
        SDL_SetRenderDrawBlendMode(render, SDL_BLENDMODE_NONE);
        return KOTONOHA_SCENE_NULL;
    }

    SDL_FRect text = {
        ((float)width - loadingCommon->textWidth) * 0.5f,
        ((float)height - loadingCommon->textHeight) * 0.5f,
        loadingCommon->textWidth,
        loadingCommon->textHeight
    };

    SDL_FRect shadow = text;
    shadow.x += 2.0f * windowScale;
    shadow.y += 2.0f * windowScale;

    if (!SDL_RenderTexture(render, loadingCommon->shadow, NULL, &shadow)) {
        SDL_LogError(SDL_LOG_CATEGORY_RENDER,
            "Failed to draw loading label shadow: %s", SDL_GetError());
    }

    if (!SDL_RenderTexture(render, loadingCommon->texture, NULL, &text)) {
        SDL_LogError(SDL_LOG_CATEGORY_RENDER,
            "Failed to draw loading label: %s", SDL_GetError());
    }

    if (!SDL_SetRenderDrawBlendMode(render, SDL_BLENDMODE_NONE)) {
        SDL_LogError(SDL_LOG_CATEGORY_RENDER,
            "Failed to restore render blend mode: %s", SDL_GetError());
    }

    return KOTONOHA_SCENE_DRAW_OVERLAYED;
}