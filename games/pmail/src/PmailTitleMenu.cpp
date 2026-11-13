#include "PmailTitleMenu.hpp"
#include <Pmail/Port.hpp>
#include <algorithm>
#include <iostream>

namespace Kotonoha::PmailPort {

PmailTitleMenu::PmailTitleMenu(Kotonoha::Engine &engine, 
                               PmailGame &game, 
                               Kotonoha::Providers::SnowAssets &assets)
    : Kotonoha::GameMenu(engine), game_(game), assets_(assets) {}

int PmailTitleMenu::itemCount() const {
    return game_.titleCount();
}

SDL_FRect PmailTitleMenu::itemRect(int index) const {
    return {189.0f, float(326 + 62 * index), 423.0f, 55.0f};
}

void PmailTitleMenu::activate(int index) {
    game_.titleChoose(index);
}

void PmailTitleMenu::renderTitle(Kotonoha::RendererRef renderer) {
    const char letter = char('a' + game_.titleVariant());
    std::string bgFile = "TITL00" + std::string(1, letter) + ".CRGB";
    
    SDL_Texture *tex = LoadTitleGraphic(renderer, bgFile);
    if (tex) {
        const SDL_FRect stage{0, 0, 800, 600};
        SDL_RenderTexture(&renderer, tex, nullptr, &stage);
    }
}

void PmailTitleMenu::renderItem(Kotonoha::RendererRef renderer, int index, bool focused) {
    const char letter = char('a' + game_.titleVariant());
    std::string file;
    if (focused) {
        file = std::string("TITL0") + char('1' + index) + letter + ".IMG";
    } else {
        file = "TITL0" + std::to_string(index + 1) + ".IMG";
    }

    SDL_Texture *tex = LoadTitleGraphic(renderer, file);
    if (tex) {
        SDL_FRect dst = itemRect(index);
        SDL_SetTextureBlendMode(tex, SDL_BLENDMODE_BLEND);
        SDL_RenderTexture(&renderer, tex, nullptr, &dst);
    }
}

SDL_Texture *PmailTitleMenu::LoadTitleGraphic(Kotonoha::RendererRef renderer, const std::string &file) {
    auto it = titleCache_.find(file);
    if (it != titleCache_.end()) return it->second.tex;

    TitleTex entry;
    try {
        auto surface = assets_.loadSurfaceAsset("graphic/" + file);
        if (surface) {
            entry.tex = SDL_CreateTextureFromSurface(&renderer, surface.get());
            if (entry.tex) {
                SDL_GetTextureSize(entry.tex, &entry.w, &entry.h);
            }
        }
    } catch (...) {}

    if (entry.tex) titleCache_[file] = entry;
    return entry.tex;
}

} // namespace Kotonoha::PmailPort
