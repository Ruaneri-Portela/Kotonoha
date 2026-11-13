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
  autoElapsedMs_ = 0;
  autoCountdown_ = false;
}

void PmailTextBox::setAutoMode(bool enabled) {
  if (autoMode_ == enabled)
    return;
  autoMode_ = enabled;
  autoElapsedMs_ = 0;
  autoCountdown_ = false;
  indicatorMs_ = 0;
  indicatorTick_ = SDL_GetTicks();
}

void PmailTextBox::setPresentationPaused(bool paused) {
  if (presentationPaused_ == paused)
    return;
  presentationPaused_ = paused;
  indicatorTick_ = SDL_GetTicks();
  clock.reset();
}

void PmailTextBox::update() {
  const auto now = SDL_GetTicks();
  const auto elapsed = indicatorTick_ ? now - indicatorTick_ : 0;
  indicatorTick_ = now;
  if (presentationPaused_ || !isVisible() || engine.isStoryPaused() ||
      game_.choosing() || game_.animating()) {
    clock.reset();
    return;
  }
  const bool wasComplete = isDialogueComplete();
  Kotonoha::VNTextBox::update();
  if (!isDialogueComplete() || !game_.waitingForClick()) {
    autoCountdown_ = false;
    autoElapsedMs_ = 0;
    return;
  }
  if (wasComplete)
    indicatorMs_ += elapsed;
  if (!autoMode_)
    return;
  // pmail.exe 0x41b1f7: AutoMessageVoiceEnd defaults to 1. The 2000ms
  // AutoMessageWait timer starts after the voice, not alongside it.
  if (game_.voicePlaying && game_.voicePlaying()) {
    autoCountdown_ = false;
    autoElapsedMs_ = 0;
  } else if (!autoCountdown_) {
    autoCountdown_ = true;
    autoElapsedMs_ = 0;
  } else {
    autoElapsedMs_ += elapsed;
    if (autoElapsedMs_ >= 2000) {
      autoCountdown_ = false;
      autoElapsedMs_ = 0;
      game_.click();
    }
  }
}

bool PmailTextBox::onEvent(Kotonoha::EventRef event) {
  if (presentationPaused_)
    return false;
  if (onVisibilityEvent(event)) {
    indicatorTick_ = SDL_GetTicks();
    return true;
  }
  if (!isVisible())
    return false;
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
    if (event.key.key == SDLK_A) {
      setAutoMode(!autoMode());
      return true;
    }
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
  // Original draw order at 0x40a89b: tinted window backing, border, buttons.
  for (const auto *name : {"wakuh", "waku", "bot"}) {
    auto *tex = LoadTextBoxGraphic(renderer, std::string(name) + ".CHAR");
    const auto placement = assets_.Sprite(name);
    if (!tex || !placement.valid)
      continue;
    const SDL_FRect dst{placement.x, placement.y, placement.w, placement.h};
    SDL_SetTextureBlendMode(tex, SDL_BLENDMODE_BLEND);
    SDL_RenderTexture(&renderer, tex, nullptr, &dst);
  }
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
  const auto frame =
      autoMode_ ? (indicatorMs_ / 170) % 4 : sequence[(indicatorMs_ / 170) % 6];
  const std::string name = autoMode_ ? "cursor02" : "cursor01";
  auto *texture =
      LoadTextBoxGraphic(renderer, name + ".anim#" + std::to_string(frame));
  if (!texture)
    return;
  const auto &placement = assets_.AnimationOf(name).frames.at(frame);
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
      if (file == "wakuh.CHAR") {
        surface.reset(
            SDL_ConvertSurface(surface.get(), SDL_PIXELFORMAT_RGBA32));
        if (!surface)
          return nullptr;
        // locpu.dll AddRgbChr (0x10002800) moves each channel towards
        // white by tint/256. MesWin_R/G/B defaults are 0,0,150; alpha
        // remains the original CHAR coverage. SDL color modulation would
        // multiply black and incorrectly leave this backing black.
        for (int y = 0; y < surface->h; ++y) {
          auto *pixel =
              static_cast<uint8_t *>(surface->pixels) + y * surface->pitch;
          for (int x = 0; x < surface->w; ++x, pixel += 4)
            pixel[2] += ((255 - pixel[2]) * 150) >> 8;
        }
      }
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
