#include <Kotonoha/routing/SchoolDaysPresentedFrame.hpp>

namespace Kotonoha {

SchoolDaysPresentedFrame::~SchoolDaysPresentedFrame() {
    Release();
}

bool SchoolDaysPresentedFrame::Capture(SDL_Renderer* renderer) {
    if (renderer == nullptr) return false;

    if (SDL_GetRenderTarget(renderer) != nullptr) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                    "[SD-COMPOSITOR] capture skipped: renderer target is not the backbuffer");
        return false;
    }

    SDL_Surface* surface = SDL_RenderReadPixels(renderer, nullptr);
    if (surface == nullptr) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                     "[SD-COMPOSITOR] backbuffer capture failed: %s",
                     SDL_GetError());
        return false;
    }

    SDL_Texture* next = SDL_CreateTextureFromSurface(renderer, surface);
    if (next == nullptr) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                     "[SD-COMPOSITOR] retained texture creation failed: %s",
                     SDL_GetError());
        SDL_DestroySurface(surface);
        return false;
    }

    SDL_SetTextureBlendMode(next, SDL_BLENDMODE_NONE);

    Release();
    texture = next;
    width = surface->w;
    height = surface->h;
    SDL_DestroySurface(surface);

    SDL_Log("[SD-COMPOSITOR] capture presented-frame size=%dx%d", width, height);
    return true;
}

bool SchoolDaysPresentedFrame::Present(SDL_Renderer* renderer) const {
    if (renderer == nullptr || texture == nullptr) return false;

    if (SDL_GetRenderTarget(renderer) != nullptr) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                    "[SD-COMPOSITOR] retained-frame present skipped: renderer target is not the backbuffer");
        return false;
    }

    if (!SDL_RenderTexture(renderer, texture, nullptr, nullptr)) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                     "[SD-COMPOSITOR] retained-frame present failed: %s",
                     SDL_GetError());
        return false;
    }
    return true;
}

void SchoolDaysPresentedFrame::Release() {
    if (texture != nullptr) {
        SDL_DestroyTexture(texture);
        texture = nullptr;
    }
    width = 0;
    height = 0;
}

} // namespace Kotonoha
