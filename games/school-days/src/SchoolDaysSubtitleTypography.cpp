#include <Kotonoha/routing/SchoolDaysSubtitleTypography.hpp>

#include <algorithm>

namespace Kotonoha {

SDL_Surface* SchoolDaysSubtitleTypography::Render(TTF_Font* font,
                                                  const char* utf8,
                                                  std::size_t bytes) {
    if (font == nullptr || utf8 == nullptr || bytes == 0u) return nullptr;

    const float oldSize = TTF_GetFontSize(font);
    const int oldOutline = TTF_GetFontOutline(font);
    auto restore = [&]() {
        (void)TTF_SetFontOutline(font, oldOutline);
        if (oldSize > 0.0f) (void)TTF_SetFontSize(font, oldSize);
    };

    if (!TTF_SetFontSize(font, PointSize) ||
        !TTF_SetFontOutline(font, OutlinePixels)) {
        restore();
        return nullptr;
    }
    SDL_Surface* outline =
        TTF_RenderText_Blended(font, utf8, bytes, Outline);
    if (outline == nullptr) {
        restore();
        return nullptr;
    }

    if (!TTF_SetFontOutline(font, 0)) {
        SDL_DestroySurface(outline);
        restore();
        return nullptr;
    }
    SDL_Surface* fill = TTF_RenderText_Blended(font, utf8, bytes, Fill);
    SDL_Surface* shadow = TTF_RenderText_Blended(font, utf8, bytes, Shadow);
    restore();
    if (fill == nullptr || shadow == nullptr) {
        if (fill != nullptr) SDL_DestroySurface(fill);
        if (shadow != nullptr) SDL_DestroySurface(shadow);
        SDL_DestroySurface(outline);
        return nullptr;
    }

    const int pad = OutlinePixels;
    const int width = std::max(outline->w, fill->w + pad * 2) + ShadowOffsetX;
    const int height = std::max(outline->h, fill->h + pad * 2) + ShadowOffsetY;
    SDL_Surface* result = SDL_CreateSurface(width, height, SDL_PIXELFORMAT_RGBA32);
    if (result == nullptr || !SDL_FillSurfaceRect(result, nullptr, 0u)) {
        if (result != nullptr) SDL_DestroySurface(result);
        SDL_DestroySurface(shadow);
        SDL_DestroySurface(fill);
        SDL_DestroySurface(outline);
        return nullptr;
    }

    (void)SDL_SetSurfaceBlendMode(shadow, SDL_BLENDMODE_BLEND);
    (void)SDL_SetSurfaceBlendMode(outline, SDL_BLENDMODE_BLEND);
    (void)SDL_SetSurfaceBlendMode(fill, SDL_BLENDMODE_BLEND);

    SDL_Rect shadowDst{pad + ShadowOffsetX, pad + ShadowOffsetY,
                       shadow->w, shadow->h};
    SDL_Rect outlineDst{0, 0, outline->w, outline->h};
    SDL_Rect fillDst{pad, pad, fill->w, fill->h};
    const bool ok = SDL_BlitSurface(shadow, nullptr, result, &shadowDst) &&
                    SDL_BlitSurface(outline, nullptr, result, &outlineDst) &&
                    SDL_BlitSurface(fill, nullptr, result, &fillDst);

    SDL_DestroySurface(shadow);
    SDL_DestroySurface(fill);
    SDL_DestroySurface(outline);
    if (!ok) {
        SDL_DestroySurface(result);
        return nullptr;
    }
    return result;
}

} // namespace Kotonoha
