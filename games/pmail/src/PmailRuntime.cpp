#include "PmailRuntime.hpp"

#include <Kotonoha/core/Engine.hpp>
#include <Kotonoha/core/FileSystem.hpp>

#include <SDL3/SDL.h>

#include <fstream>
#include <iostream>
#include <utility>

namespace Kotonoha::PmailPort {

PmailRuntime::PmailRuntime(std::string gameDirectory)
    : Runtime("Pure Mail", "0.1.0", "me.hirameki.kotonoha.pmail"),
      gameDirectory_(std::move(gameDirectory)) {}

bool PmailRuntime::loadScriptPack() {
  const std::vector<std::string> candidates = {
      gameDirectory_ + "/data/pack/script.dat",
      gameDirectory_ + "/pack/script.dat",
      gameDirectory_ + "/script.dat",
      gameDirectory_ + "/SCRIPT.DAT",
      gameDirectory_ + "/data/pack/SCRIPT.DAT",
  };
  for (const auto &path : candidates) {
    std::ifstream in(path, std::ios::binary | std::ios::ate);
    if (!in)
      continue;
    const auto sz = in.tellg();
    if (sz <= 0)
      continue;
    in.seekg(0, std::ios::beg);
    std::vector<uint8_t> data(static_cast<size_t>(sz));
    if (!in.read(reinterpret_cast<char *>(data.data()), sz))
      continue;
    std::cout << "[PmailPort] Loaded script pack: " << path << " ("
              << data.size() << " bytes)" << std::endl;
    game_ = std::make_unique<PmailGame>(std::move(data));
    return true;
  }
  status_ = "script.dat not found under: " + gameDirectory_;
  std::cerr << "[PmailPort] " << status_ << std::endl;
  return false;
}

void PmailRuntime::initGlobal(Kotonoha::Engine &engine) {
  engine_ = &engine;
  if (game_)
    return;
  if (loadScriptPack()) {
    if (game_->init()) {
      running_ = true;
      status_ = "Running: " + game_->currentScript();
    } else {
      status_ = "Failed to initialize PMAIL script VM";
    }
  }
}

void PmailRuntime::initMenu(Kotonoha::Engine &) {
  // No separate menu yet; gameplay/runtime handles execution.
}

void PmailRuntime::initGameplay(Kotonoha::Engine &) {
  if (game_ && !running_) {
    if (game_->init())
      running_ = true;
  }
}

void PmailRuntime::render(Kotonoha::RendererRef renderer, float) {
  // Minimal visual feedback (black background). Text/UI rendering will be
  // implemented as native syscalls and assets are wired.
  SDL_SetRenderDrawColor(&renderer, 0, 0, 0, 255);
  SDL_RenderClear(&renderer);
  (void)engine_;
}

void PmailRuntime::update() {
  if (!game_ || !running_)
    return;
  game_->pump(50000);
  if (!game_->message().empty()) {
    // Keep latest message in status for debugNode/overlay use.
    status_ = game_->currentScript();
  }
  if (game_->ended()) {
    status_ = "Script ended: " + game_->currentScript();
    running_ = false;
  }
}

bool PmailRuntime::onEvent(Kotonoha::EventRef event) {
  if (!game_)
    return false;
  if (event.type == SDL_EVENT_KEY_DOWN && !event.key.repeat) {
    if (event.key.key == SDLK_SPACE || event.key.key == SDLK_RETURN ||
        event.key.key == SDLK_AC_BACK) {
      if (game_->waitingForClick())
        game_->click();
      return true;
    }
  } else if (event.type == SDL_EVENT_MOUSE_BUTTON_DOWN) {
    if (event.button.button == SDL_BUTTON_LEFT) {
      if (game_->waitingForClick())
        game_->click();
      return true;
    }
  }
  return false;
}

std::string PmailRuntime::debugNode() const {
  if (game_) {
    char buf[64];
    std::snprintf(buf, sizeof(buf), "pc=%u", (unsigned)game_->vm().pc());
    return game_->currentScript() + " | " + buf + " | " + status_;
  }
  return status_;
}

} // namespace Kotonoha::PmailPort
