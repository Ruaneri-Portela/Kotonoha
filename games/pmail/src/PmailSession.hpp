#pragma once

#include <Pmail/Vm.hpp>
#include <Kotonoha/core/Runtime.hpp>
#include <SnowRadish/SnowAssets.hpp>
#include <SDL3/SDL.h>
#include <map>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace Kotonoha::PmailPort {

class PmailGame;
class PmailComponents;

/**
 * PmailSession manages the high-level state of the PMAIL game instance.
 * It holds the PmailGame (the VM host) and manages the transition between
 * different game states (Title, Gameplay, etc.) using the Kotonoha Session model.
 */
class PmailSession {
public:
    explicit PmailSession(std::shared_ptr<PmailGame> game, 
                         Kotonoha::Engine &engine,
                         std::shared_ptr<Kotonoha::Providers::SnowAssets> assets);
    ~PmailSession();

    // Session overrides
    void update();
    void render(Kotonoha::RendererRef renderer);
    void onEvent(Kotonoha::EventRef event);

    // Session management
    void init();
    void shutdown();
    void setMenuActions(std::function<void()> save, std::function<void()> load,
                        std::function<void()> title);
    void closeMenus(bool clearBacklog = false);

    // Getters
    std::shared_ptr<PmailGame> game() const { return game_; }
    Kotonoha::Providers::SnowAssets *assets() const { return assets_.get(); }
    bool isAtTitle() const;

    // Native handler
    void handleNative(int id, const std::vector<::Pmail::Arg> &args);

private:
    void updateTitle();
    void updateGameplay();
    void renderTitle(Kotonoha::RendererRef renderer);
    void renderGameplay(Kotonoha::RendererRef renderer);

    // Title helping
    SDL_Texture *LoadTitleGraphic(Kotonoha::RendererRef renderer, const std::string &file);
    void RenderTitle(Kotonoha::RendererRef renderer);
    bool HandleTitleEvent(Kotonoha::EventRef event, SDL_FPoint stagePoint, bool haveStagePoint);

    std::shared_ptr<PmailGame> game_;
    Kotonoha::Engine &engine_;
    std::shared_ptr<Kotonoha::Providers::SnowAssets> assets_;
    std::unique_ptr<PmailComponents> components_;

    // Title state
    struct TitleTex {
      SDL_Texture *tex = nullptr;
      float w = 0, h = 0;
    };
    std::map<std::string, TitleTex> titleCache_;

    // Status
    bool initialized_ = false;
};

} // namespace Kotonoha::PmailPort
