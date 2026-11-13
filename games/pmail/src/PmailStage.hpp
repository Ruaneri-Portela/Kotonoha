#pragma once

#include <Kotonoha/core/Scene.hpp>
#include <Pmail/Port.hpp>
#include <SnowRadish/SnowAssets.hpp>
#include <SDL3/SDL.h>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace Kotonoha::PmailPort {

class PmailGame;

/**
 * PmailStage represents the main gameplay scene (the "stage").
 * It draws the background and all active sprites.
 */
class PmailStage final : public Kotonoha::Scene {
public:
    ~PmailStage() override;
    PmailStage(Kotonoha::Engine &engine, 
               PmailGame &game, 
               Kotonoha::Providers::SnowAssets &assets);

    void update() override;
    void render(Kotonoha::RendererRef renderer, float scale) override;

private:
    PmailGame &game_;
    Kotonoha::Providers::SnowAssets &assets_;

    // Background caching
    std::string renderedBackground_;
    SDL_Texture *backgroundTexture_ = nullptr;

    // Sprite caching
    struct SpriteTex {
        SDL_Texture *tex = nullptr;
        float w = 0, h = 0;
    };
    std::map<std::string, SpriteTex> spriteCache_;

    void updateBackground(Kotonoha::RendererRef renderer);
    void renderSprites(Kotonoha::RendererRef renderer);
};

} // namespace Kotonoha::PmailPort
