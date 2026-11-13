#include "PmailTextBox.hpp"
#include <Kotonoha/core/UI.hpp>
#include <Pmail/Port.hpp>
#include <algorithm>
#include <iostream>

namespace Kotonoha::PmailPort {

PmailTextBox::PmailTextBox(Kotonoha::Engine &engine, PmailGame &game,
                           Kotonoha::Providers::SnowAssets &assets)
    : Kotonoha::VNTextBox(engine, "", ""), game_(game), assets_(assets) {}

PmailTextBox::~PmailTextBox() {
  for (auto &[name, texture] : textBoxCache_)
    if (texture.tex)
      SDL_DestroyTexture(texture.tex);
}

void PmailTextBox::setDialogue(const std::string &speaker,
                               const std::string &dialogue) {
  Kotonoha::VNTextBox::setDialogue(speaker, dialogue);
  indicatorMs_ = 0;
  indicatorTick_ = SDL_GetTicks();
}

void PmailTextBox::update() {
  const auto now = SDL_GetTicks();
  if (indicatorTick_ && isDialogueComplete() && game_.waitingForClick() &&
      !engine.isStoryPaused())
    indicatorMs_ += now - indicatorTick_;
  indicatorTick_ = now;
  Kotonoha::VNTextBox::update();
}

bool PmailTextBox::onEvent(Kotonoha::EventRef event) {
  if (event.type == SDL_EVENT_MOUSE_BUTTON_DOWN &&
      event.button.button == SDL_BUTTON_RIGHT) {
    openMenu();
    return true;
  }
  if (event.type == SDL_EVENT_MOUSE_WHEEL && event.wheel.y > 0) {
    openBacklog();
    return true;
  }
  if (event.type == SDL_EVENT_KEY_DOWN && !event.key.repeat) {
    if (event.key.key == SDLK_ESCAPE) {
      openMenu();
      return true;
    }
    if (event.key.key == SDLK_B || event.key.key == SDLK_PAGEUP) {
      openBacklog();
      return true;
    }
    if (event.key.key == SDLK_RETURN || event.key.key == SDLK_SPACE) {
      if (!isDialogueComplete())
        completeDialogue();
      else
        game_.click();
      return true;
    }
  }
  if (event.type == SDL_EVENT_MOUSE_BUTTON_DOWN &&
      event.button.button == SDL_BUTTON_LEFT) {
    if (!isDialogueComplete())
      completeDialogue();
    else
      game_.click();
    return true;
  }
  return false;
}

void PmailTextBox::openMenu() {
  if (menuAction_)
    menuAction_();
}
void PmailTextBox::openBacklog() {
  if (backlogAction_)
    backlogAction_();
}

void PmailTextBox::renderFrame(Kotonoha::RendererRef renderer) {
  SDL_Texture *tex = LoadTextBoxGraphic(renderer, "waku.CHAR");
  if (!tex)
    return;
  const auto placement = assets_.Sprite("waku");
  if (!placement.valid)
    return;
  const SDL_FRect dst{placement.x, placement.y, placement.w, placement.h};
  SDL_SetTextureBlendMode(tex, SDL_BLENDMODE_BLEND);
  SDL_RenderTexture(&renderer, tex, nullptr, &dst);
}

void PmailTextBox::renderSpeaker(Kotonoha::RendererRef renderer) {
  if (getSpeaker().empty())
    return;

  engine.GetVNUI().text(renderer, getSpeaker(), 74, 463, 640,
                        SDL_Color{255, 215, 100, 255});
}

void PmailTextBox::renderDialogue(Kotonoha::RendererRef renderer) {
  engine.GetVNUI().text(renderer, getVisibleText(), 74, 492, 650,
                        SDL_Color{240, 240, 245, 255});
}

void PmailTextBox::renderIndicator(Kotonoha::RendererRef renderer) {
  if (!game_.waitingForClick())
    return;
  // 0x41b01b..0x41b12d: ten 17ms ticks, then 0,1,2,3,2,1.
  constexpr uint32_t sequence[]{0, 1, 2, 3, 2, 1};
  const auto frame = sequence[(indicatorMs_ / 170) % 6];
  auto *texture =
      LoadTextBoxGraphic(renderer, "cursor01.anim#" + std::to_string(frame));
  if (!texture)
    return;
  const auto &placement = assets_.AnimationOf("cursor01").frames.at(frame);
  const SDL_FRect dst{placement.x, placement.y, placement.w, placement.h};
  SDL_RenderTexture(&renderer, texture, nullptr, &dst);
}

SDL_Texture *PmailTextBox::LoadTextBoxGraphic(Kotonoha::RendererRef renderer,
                                              const std::string &file) {
  auto it = textBoxCache_.find(file);
  if (it != textBoxCache_.end())
    return it->second.tex;

  TextBoxTex entry;
  try {
    auto surface = assets_.loadSurfaceAsset("graphic/" + file);
    if (surface) {
      entry.tex = SDL_CreateTextureFromSurface(&renderer, surface.get());
      if (entry.tex) {
        SDL_GetTextureSize(entry.tex, &entry.w, &entry.h);
      }
    }
  } catch (...) {
  }

  textBoxCache_[file] = entry;
  return entry.tex;
}

} // namespace Kotonoha::PmailPort
