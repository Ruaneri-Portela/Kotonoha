#pragma once

#include <SDL3/SDL.h>
#include <SDL3_ttf/SDL_ttf.h>

#include <cstddef>

namespace Kotonoha {

// Game-owned projection of the shipped remaster subtitle style.  The authored
// subtitle track itself is still rendered by libass; School Days UI text uses
// these values only so choices/BackLog visually belong to the same typography.
struct SchoolDaysSubtitleTypography {
    static constexpr const char* PrimaryFontKey = "NotoSans-Regular";
    static constexpr const char* JapaneseFallbackFontKey = "NotoSansJP-Regular";
    static constexpr float PointSize = 46.0f;
    static constexpr int OutlinePixels = 3; // styles.skot Outline=2.6
    static constexpr int ShadowOffsetX = 1; // styles.skot Shadow=1.2
    static constexpr int ShadowOffsetY = 1;

    static constexpr SDL_Color Fill{248, 245, 255, 255};
    static constexpr SDL_Color Outline{46, 26, 32, 255};
    static constexpr SDL_Color Shadow{0, 0, 0, 150};

    // Returns a newly-owned RGBA surface with shadow + outline + fill. The
    // caller owns the returned surface. The font's size/outline state is
    // restored before returning.
    static SDL_Surface* Render(TTF_Font* font, const char* utf8,
                               std::size_t bytes);
};

} // namespace Kotonoha
