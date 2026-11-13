#pragma once

#include <Kotonoha/components/GameMenu.hpp>
#include <Kotonoha/components/VNTextBox.hpp>
#include <Kotonoha/components/InGameMenu.hpp>
#include <Kotonoha/components/VNBacklog.hpp>
#include <Pmail/Port.hpp>
#include <SnowRadish/SnowAssets.hpp>
#include <memory>

namespace Kotonoha::PmailPort {

class PmailGame;

/**
 * PmailComponents holds the various Kotonoha components used by PMAIL.
 * This allows components to be easily recreated and managed within the Session/Scene architecture.
 */
class PmailComponents {
public:
    explicit PmailComponents(PmailGame &game, Kotonoha::Engine &engine, Kotonoha::Providers::SnowAssets &assets);
    ~PmailComponents() = default;

    void init();

    void onEvent(Kotonoha::EventRef event);

    // Accessors for common components (if they are active)
    Kotonoha::VNTextBox* textBox() { return textBox_.get(); }
    Kotonoha::InGameMenu* pauseMenu() { return pauseMenu_.get(); }
    Kotonoha::VNBacklog* backlog() { return backlog_.get(); }
    Kotonoha::Scene* stage() { return stage_.get(); }

    // Lifecycle management
    void update(float dt);
    void render(Kotonoha::RendererRef renderer);

private:
    std::unique_ptr<Kotonoha::GameMenu> createTitleMenu();
    std::unique_ptr<Kotonoha::Scene> createStage();
    std::unique_ptr<Kotonoha::VNTextBox> createTextBox();
    std::unique_ptr<Kotonoha::InGameMenu> createPauseMenu();
    std::unique_ptr<Kotonoha::VNBacklog> createBacklog();

    PmailGame &game_;
    Kotonoha::Engine &engine_;
    Kotonoha::Providers::SnowAssets &assets_;

    // Active component instances
    std::unique_ptr<Kotonoha::Scene> stage_;
    std::unique_ptr<Kotonoha::VNTextBox> textBox_;
    std::unique_ptr<Kotonoha::InGameMenu> pauseMenu_;
    std::unique_ptr<Kotonoha::VNBacklog> backlog_;
};

} // namespace Kotonoha::PmailPort
