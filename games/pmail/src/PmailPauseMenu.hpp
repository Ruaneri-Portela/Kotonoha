#pragma once

#include <Kotonoha/components/InGameMenu.hpp>
#include <Pmail/Port.hpp>

namespace Kotonoha::PmailPort {

class PmailGame;

/**
 * PmailPauseMenu is an InGameMenu for when the game is paused.
 */
class PmailPauseMenu final : public Kotonoha::InGameMenu {
public:
    PmailPauseMenu(Kotonoha::Engine &engine, PmailGame &game);

    void setBacklogAction(std::function<void()> action) { backlogAction_ = std::move(action); }
protected:
    void openBacklog() override { close(); if (backlogAction_) backlogAction_(); }
private:
    PmailGame &game_;
    std::function<void()> backlogAction_;
};

} // namespace Kotonoha::PmailPort
