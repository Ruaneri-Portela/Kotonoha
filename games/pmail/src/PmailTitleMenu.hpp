#pragma once

#include <Kotonoha/components/GameMenu.hpp>
#include <Pmail/Port.hpp>
#include <SnowRadish/SnowAssets.hpp>
#include <SDL3/SDL.h>
#include <map>

namespace Kotonoha::PmailPort {

class PmailGame;

/**
 * PmailTitleMenu is a specialized GameMenu for the PMAIL title screen.
 * It draws the title background and buttons as images.
 */
class PmailTitleMenu final : public Kotonoha::GameMenu {
public:
    PmailTitleMenu(Kotonoha::Engine &engine, 
                   PmailGame &game, 
                   Kotonoha::Providers::SnowAssets &assets);

    int itemCount() const override;
    std::string itemLabel(int index) const override { return ""; }
    SDL_FRect itemRect(int index) const override;
    void activate(int index) override;
    void renderTitle(Kotonoha::RendererRef renderer) override;
    void renderItem(Kotonoha::RendererRef renderer, int index, bool focused) override;

private:
    PmailGame &game_;
    Kotonoha::Providers::SnowAssets &assets_;

    // Cache for title textures
    struct TitleTex {
        SDL_Texture *tex = nullptr;
        float w = 0, h = 0;
    };
    std::map<std::string, TitleTex> titleCache_;

    SDL_Texture *LoadTitleGraphic(Kotonoha::RendererRef renderer, const std::string &file);
};

} // namespace Kotonoha::PmailPort
