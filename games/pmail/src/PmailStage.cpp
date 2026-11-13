#include "PmailStage.hpp"
#include <Pmail/Port.hpp>
#include <algorithm>
#include <iostream>

namespace Kotonoha::PmailPort {

PmailStage::PmailStage(Kotonoha::Engine &engine, 
                       PmailGame &game, 
                       Kotonoha::Providers::SnowAssets &assets)
    : Kotonoha::Scene(engine), game_(game), assets_(assets) {}

PmailStage::~PmailStage() {
    if (backgroundTexture_) SDL_DestroyTexture(backgroundTexture_);
    for (auto &[name, sprite] : spriteCache_)
        if (sprite.tex) SDL_DestroyTexture(sprite.tex);
}

void PmailStage::update() {
    // The stage doesn't really own game logic, it just reflects it.
}

void PmailStage::render(Kotonoha::RendererRef renderer, float scale) {
    // 1. Draw Background
    if (game_.filled()) {
        const uint32_t color = game_.fillColor();
        SDL_SetRenderDrawColor(&renderer, Uint8(color >> 16),
                               Uint8(color >> 8), Uint8(color), 255);
        SDL_RenderClear(&renderer);
    }
    updateBackground(renderer);
    if (backgroundTexture_) {
        const SDL_FRect stage{0, 0, 800, 600};
        SDL_RenderTexture(&renderer, backgroundTexture_, nullptr, &stage);
    }

    // 2. Draw Sprites
    renderSprites(renderer);
}

void PmailStage::updateBackground(Kotonoha::RendererRef renderer) {
    std::string bgName = game_.background();
    if (bgName.empty()) {
        renderedBackground_.clear();
        if (backgroundTexture_) { SDL_DestroyTexture(backgroundTexture_); backgroundTexture_ = nullptr; }
        return;
    }

    if (renderedBackground_ != bgName) {
        if (backgroundTexture_) {
            SDL_DestroyTexture(backgroundTexture_);
            backgroundTexture_ = nullptr;
        }

        try {
            auto surface = assets_.loadSurfaceAsset("graphic/" + bgName + ".CRGB");
            if (surface) {
                backgroundTexture_ = SDL_CreateTextureFromSurface(&renderer, surface.get());
                renderedBackground_ = bgName;
            }
        } catch (...) {
            renderedBackground_ = bgName;
        }
    }
}

void PmailStage::renderSprites(Kotonoha::RendererRef renderer) {
    for (const auto &sp : game_.sprites()) {
        if (sp.name.empty()) continue;

        auto it = spriteCache_.find(sp.name);
        if (it == spriteCache_.end()) {
            // Load sprite. We'll try common extensions.
            SpriteTex st;
            const char *exts[] = {".CHAR", ".chd", ".img", ".evd", ".CRGB"};
            for (const char *ext : exts) {
                std::string fullPath = sp.name + ext;
                try {
                auto surface = assets_.loadSurfaceAsset("graphic/" + fullPath);
                if (surface) {
                    st.tex = SDL_CreateTextureFromSurface(&renderer, surface.get());
                    if (st.tex) {
                        SDL_GetTextureSize(st.tex, &st.w, &st.h);
                        break;
                    }
                }
                } catch (...) {}
            }
            it = spriteCache_.emplace(sp.name, st).first;
        }

        if (it->second.tex) {
            const auto placement = assets_.Sprite(sp.name);
            SDL_FRect dst{float(sp.x) + (placement.valid ? placement.x : 0.0f),
                          float(sp.y) + (placement.valid ? placement.y : 0.0f),
                          it->second.w, it->second.h};
            // Apply tint
            SDL_SetTextureColorMod(it->second.tex,
                                   Uint8(std::clamp(sp.tint[0], 0, 0x100) * 255 / 0x100),
                                   Uint8(std::clamp(sp.tint[1], 0, 0x100) * 255 / 0x100),
                                   Uint8(std::clamp(sp.tint[2], 0, 0x100) * 255 / 0x100));
            SDL_SetTextureBlendMode(it->second.tex, SDL_BLENDMODE_BLEND);
            SDL_RenderTexture(&renderer, it->second.tex, nullptr, &dst);
        }
    }
}

} // namespace Kotonoha::PmailPort
