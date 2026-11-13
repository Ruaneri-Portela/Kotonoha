#include "PmailSession.hpp"
#include <Pmail/Port.hpp>
#include "PmailComponents.hpp"
#include <Kotonoha/core/Engine.hpp>
#include <Kotonoha/core/FileSystem.hpp>
#include <Kotonoha/core/UI.hpp>
#include <SDL3/SDL.h>
#include <iostream>

namespace Kotonoha::PmailPort {

PmailSession::PmailSession(std::shared_ptr<PmailGame> game, 
                         Kotonoha::Engine &engine,
                         std::shared_ptr<Kotonoha::Providers::SnowAssets> assets)
    : game_(std::move(game)), engine_(engine), assets_(std::move(assets)) {
    components_ = std::make_unique<PmailComponents>(*game_, engine_, *assets_);
}

PmailSession::~PmailSession() {
    for (auto &[name, entry] : titleCache_)
        if (entry.tex) SDL_DestroyTexture(entry.tex);
}

void PmailSession::init() {
    if (initialized_) return;
    components_->init();
    initialized_ = true;
}

void PmailSession::shutdown() {
    initialized_ = false;
}

void PmailSession::setMenuActions(std::function<void()> save,
                                  std::function<void()> load,
                                  std::function<void()> title) {
    components_->pauseMenu()->SetOnSave(std::move(save));
    components_->pauseMenu()->SetOnLoad(std::move(load));
    components_->pauseMenu()->SetOnReturnToMenu(std::move(title));
}

void PmailSession::closeMenus(bool clearBacklog) {
    components_->pauseMenu()->close();
    components_->backlog()->close();
    if (clearBacklog) components_->backlog()->clear();
}

void PmailSession::update() {
    if (!initialized_ || !game_) return;

    if (game_->atTitle()) {
        updateTitle();
    } else {
        updateGameplay();
    }
}

void PmailSession::render(Kotonoha::RendererRef renderer) {
    if (!initialized_ || !game_) return;

    if (game_->atTitle()) {
        renderTitle(renderer);
    } else {
        renderGameplay(renderer);
    }
}

void PmailSession::onEvent(Kotonoha::EventRef event) {
    if (!initialized_ || !game_) return;

    if (game_->atTitle()) {
        SDL_FPoint stagePoint{0, 0};
        bool haveStagePoint = false;
        if (event.type == SDL_EVENT_MOUSE_MOTION || event.type == SDL_EVENT_MOUSE_BUTTON_DOWN) {
            const auto frame = Kotonoha::UI::fit({800.0f, 600.0f}, {800, 600});
            const SDL_Event mapped = Kotonoha::UI::toFrame(event, frame);
            stagePoint.x = mapped.type == SDL_EVENT_MOUSE_MOTION ? mapped.motion.x : mapped.button.x;
            stagePoint.y = mapped.type == SDL_EVENT_MOUSE_MOTION ? mapped.motion.y : mapped.button.y;
            haveStagePoint = true;
        }
        HandleTitleEvent(event, stagePoint, haveStagePoint);
    } else {
        if (components_ && components_->backlog()->isVisible()) { components_->onEvent(event); return; }
        if (components_ && components_->pauseMenu()->IsOpen()) { components_->onEvent(event); return; }
        if (game_->choosing()) {
            const auto choices = game_->choices();
            if (event.type == SDL_EVENT_KEY_DOWN && !event.key.repeat) {
                if (event.key.key >= SDLK_1 && event.key.key <= SDLK_9) {
                    game_->choose(size_t(event.key.key - SDLK_1)); return;
                }
                if (event.key.key == SDLK_RETURN && !choices.empty()) {
                    game_->choose(0); return;
                }
            }
            if (event.type == SDL_EVENT_MOUSE_BUTTON_DOWN && event.button.button == SDL_BUTTON_LEFT) {
                for (size_t i = 0; i < choices.size(); ++i) {
                    const float y = 170.0f + float(i) * 52.0f;
                    if (event.button.x >= 100 && event.button.x < 700 &&
                        event.button.y >= y && event.button.y < y + 44) {
                        game_->choose(i); return;
                    }
                }
            }
        }
        if (components_) components_->onEvent(event);
    }
}

bool PmailSession::isAtTitle() const {
    return game_->atTitle();
}

void PmailSession::updateTitle() {
    game_->pump(10000);
}

void PmailSession::updateGameplay() {
    if (components_ && !components_->pauseMenu()->IsOpen() &&
        !components_->backlog()->isVisible())
        game_->pump(10000);
    if (components_) {
        components_->update(0.016f); // Assume 60fps for simplicity
    }
}

void PmailSession::renderTitle(Kotonoha::RendererRef renderer) {
    if (!game_) return;
    const char letter = char('a' + game_->titleVariant());
    const char *base[3] = {"TITL01.IMG", "TITL02.IMG", "TITL03.IMG"};
    const int count = std::clamp(game_->titleCount(), 1, 3);
    const int selected = game_->titleSelection();

    const std::string background = std::string("TITL00") + letter + ".CRGB";
    if (SDL_Texture *tex = LoadTitleGraphic(renderer, background)) {
        const SDL_FRect dst{0, 0, 800, 600};
        SDL_RenderTexture(&renderer, tex, nullptr, &dst);
    }
    for (int i = 0; i < count; ++i) {
        std::string file;
        if (i == selected)
            file = std::string("TITL0") + char('1' + i) + letter + ".IMG";
        else
            file = std::string(base[i]);

        SDL_Texture *tex = LoadTitleGraphic(renderer, file);
        if (!tex) tex = LoadTitleGraphic(renderer, std::string(base[i]));
        if (!tex) continue;

        const SDL_FRect dst{189.0f, float(326 + 62 * i), 423.0f, 55.0f};
        SDL_SetTextureBlendMode(tex, SDL_BLENDMODE_BLEND);
        SDL_RenderTexture(&renderer, tex, nullptr, &dst);
    }
}

void PmailSession::renderGameplay(Kotonoha::RendererRef renderer) {
    if (components_) {
        components_->render(renderer);
    }
    if (game_->choosing()) {
        const auto choices = game_->choices();
        for (size_t i = 0; i < choices.size(); ++i) {
            const SDL_FRect area{100.0f, 170.0f + float(i) * 52.0f, 600.0f, 44.0f};
            engine_.GetUI().button(renderer, area, choices[i]);
        }
    }
}

SDL_Texture *PmailSession::LoadTitleGraphic(Kotonoha::RendererRef renderer, const std::string &file) {
    if (!assets_ || file.empty()) return nullptr;
    auto it = titleCache_.find(file);
    if (it != titleCache_.end()) return it->second.tex;

    TitleTex entry;
    try {
        auto surface = assets_->loadSurfaceAsset("graphic/" + file);
        if (surface) {
            entry.tex = SDL_CreateTextureFromSurface(&renderer, surface.get());
            if (entry.tex) {
                SDL_GetTextureSize(entry.tex, &entry.w, &entry.h);
            }
        }
    } catch (...) {}

    titleCache_[file] = entry;
    return entry.tex;
}

bool PmailSession::HandleTitleEvent(Kotonoha::EventRef event, SDL_FPoint stagePoint, bool haveStagePoint) {
    if (event.type == SDL_EVENT_MOUSE_MOTION && haveStagePoint) {
        for (int i = 0; i < game_->titleCount(); ++i) {
            const float y = float(326 + 62 * i);
            if (stagePoint.x >= 189 && stagePoint.x < 189 + 423 &&
                stagePoint.y >= y && stagePoint.y < y + 55) {
                game_->titleHover(i);
                return true;
            }
        }
    }
    if (event.type == SDL_EVENT_MOUSE_BUTTON_DOWN && event.button.button == SDL_BUTTON_LEFT) {
        if (haveStagePoint) {
            for (int i = 0; i < game_->titleCount(); ++i) {
                const float y = float(326 + 62 * i);
                if (stagePoint.x >= 189 && stagePoint.x < 189 + 423 &&
                    stagePoint.y >= y && stagePoint.y < y + 55) {
                    game_->titleChoose(i);
                    return true;
                }
            }
        }
        return true;
    }
    if (event.type == SDL_EVENT_KEY_DOWN && !event.key.repeat) {
        switch (event.key.key) {
            case SDLK_UP: game_->titleMove(-1); return true;
            case SDLK_DOWN: game_->titleMove(1); return true;
            case SDLK_RETURN: game_->titleChoose(game_->titleSelection()); return true;
            case SDLK_ESCAPE: game_->titleChoose(1); return true;
            default: break;
        }
    }
    return false;
}

void PmailSession::handleNative(int, const std::vector<::Pmail::Arg> &) {
    // The runtime owns audio; the game host owns script and title commands.
}

} // namespace Kotonoha::PmailPort
