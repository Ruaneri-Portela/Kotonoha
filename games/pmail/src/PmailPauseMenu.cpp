#include "PmailPauseMenu.hpp"
#include <Pmail/Port.hpp>
#include <iostream>

namespace Kotonoha::PmailPort {

PmailPauseMenu::PmailPauseMenu(Kotonoha::Engine &engine, PmailGame &game)
    : Kotonoha::InGameMenu(engine), game_(game) {
  // For now, just one item.
}

} // namespace Kotonoha::PmailPort
