#pragma once

#include "DdlcAssets.hpp"

#include <Kotonoha/core/SDL.hpp>

#include <memory>
#include <string>

namespace Kotonoha {
class Engine;
}

namespace Ddlc {

// Coordinates the Ren'Py VM, presentation and audio for the engine scenes.
class GameSession {
public:
    GameSession(Kotonoha::Engine& engine, std::shared_ptr<GameAssets> assets);
    ~GameSession();
    GameSession(const GameSession&) = delete;
    GameSession& operator=(const GameSession&) = delete;

    void update(bool suspended);
    bool event(Kotonoha::EventRef event);
    bool inMenu() const;
    bool playing() const;
    bool paused() const;
    bool choosing() const;
    bool showingDialogue() const;
    std::string debugNode() const;
    void debugJump(const std::string& label);

    void beginFrame(Kotonoha::RendererRef renderer, float pixelScale);
    void drawStage(Kotonoha::RendererRef renderer);
    void drawDialogue(Kotonoha::RendererRef renderer);
    void drawChoices(Kotonoha::RendererRef renderer);
    void drawMenu(Kotonoha::RendererRef renderer);
    void drawPause(Kotonoha::RendererRef renderer);
    void endFrame(Kotonoha::RendererRef renderer);
    void abortFrame(Kotonoha::RendererRef renderer) noexcept;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace Ddlc
