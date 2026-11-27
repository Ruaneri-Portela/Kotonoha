#include "PmailTextBox.hpp"
#include "PmailWindowTint.hpp"
#include <Kotonoha/Filesystem/Filesystem.hpp>
#include <Kotonoha/core/Engine.hpp>
#include <Kotonoha/core/UI.hpp>
#include <Kotonoha/media/upscaling/Upscaler.hpp>
#include <Pmail/Port.hpp>
#include <algorithm>
#include <cstdlib>
#include <iostream>
#include <string_view>

namespace Kotonoha::PmailPort {

PmailTextBox::PmailTextBox(Kotonoha::Engine &engine, PmailGame &game,
                           Kotonoha::Providers::SnowAssets &assets)
    : Kotonoha::VNTextBox(engine, "", ""), game_(game), assets_(assets) {}

PmailTextBox::~PmailTextBox() = default;

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
    // bot.msk hit-test regions for the two on-screen message-window buttons:
    // value 253: [753, 548]..[764, 558] (Backlog / Up)
    // value 254: [753, 565]..[764, 575] (Auto / Menu)
    if (event.button.x >= 753.0f && event.button.x <= 764.0f &&
        event.button.y >= 548.0f && event.button.y <= 558.0f) {
      openBacklog();
      return true;
    }
    if (event.button.x >= 753.0f && event.button.x <= 764.0f &&
        event.button.y >= 565.0f && event.button.y <= 575.0f) {
      openMenu();
      return true;
    }
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
    Kotonoha::RenderUpscaledTexture(&renderer, tex, nullptr, &dst);
  }
}

void PmailTextBox::renderSpeaker(Kotonoha::RendererRef renderer) {
  if (getSpeaker().empty())
    return;

  const auto &settings = engine.GetSettings();
  engine.GetVNUI().text(renderer, getSpeaker(), 74, 463, 640,
                        settings.vnTextColorOverride
                            ? settings.vnSpeakerColor
                            : SDL_Color{122, 71, 46, 255});
}

void PmailTextBox::renderDialogue(Kotonoha::RendererRef renderer) {
  const auto &settings = engine.GetSettings();
  engine.GetVNUI().text(renderer, getVisibleText(), 74, 492, 650,
                        settings.vnTextColorOverride
                            ? settings.vnTextColor
                            : SDL_Color{48, 45, 62, 255});
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
  const auto &placement = assets_.AnimationOf(name).frames.at(static_cast<size_t>(frame));
  const SDL_FRect dst{placement.x, placement.y, placement.w, placement.h};
  Kotonoha::RenderUpscaledTexture(&renderer, texture, nullptr, &dst);
}

SDL_Texture *PmailTextBox::LoadTextBoxGraphic(Kotonoha::RendererRef renderer,
                                              const std::string &file) {
  auto it = textBoxCache_.find(file);
  if (it != textBoxCache_.end())
    return it->second.tex.get();

  TextBoxTex entry;
  try {
    auto surface = assets_.loadSurfaceAsset("graphic/" + file);
    if (surface) {
      if (file == "wakuh.CHAR") {
        surface.reset(
            SDL_ConvertSurface(surface.get(), SDL_PIXELFORMAT_RGBA32));
        if (!surface)
          return nullptr;
        // Honor OF_ADV2.INI's MesWin_R/G/B, not a hard-coded blue tint.
        // The original locpu.dll AddRgbChr adjusts RGB, preserving alpha.
        // VFS access also works when the game files come from HTTP/archives.
        PmailMessageTint tint;
        if (auto source = assets_.source()) {
          for (const char *name : {"OF_ADV2.INI", "data/OF_ADV2.INI"}) {
            try {
              if (!source->exists(name))
                continue;
              const auto bytes = Kotonoha::Filesystem::readAll(
                  *source->open(name), 64u * 1024u);
              const auto sourceText = std::string_view(
                  reinterpret_cast<const char *>(bytes.data()), bytes.size());
              tint = ParsePmailMessageTint(sourceText, tint);
              break;
            } catch (const std::exception &error) {
              SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                          "[PmailPort] Cannot read MesWin settings: %s",
                          error.what());
            }
          }
        }
        auto overrideChannel = [](const char *key, int &channel) {
          const char *value = SDL_getenv(key);
          if (!value || !*value)
            return;
          char *end = nullptr;
          const long chosen = std::strtol(value, &end, 10);
          if (end && !*end && chosen >= 0 && chosen <= 256)
            channel = int(chosen);
        };
        overrideChannel("PMAIL_MESWIN_R", tint.red);
        overrideChannel("PMAIL_MESWIN_G", tint.green);
        overrideChannel("PMAIL_MESWIN_B", tint.blue);
        for (int y = 0; y < surface->h; ++y) {
          auto *pixel =
              static_cast<uint8_t *>(surface->pixels) + y * surface->pitch;
          for (int x = 0; x < surface->w; ++x, pixel += 4) {
            pixel[0] = PmailAddRgbChr(pixel[0], tint.red);
            pixel[1] = PmailAddRgbChr(pixel[1], tint.green);
            pixel[2] = PmailAddRgbChr(pixel[2], tint.blue);
          }
        }
      }
      entry.tex.reset(SDL_CreateTextureFromSurface(&renderer, surface.get()));
      if (entry.tex) {
        SDL_GetTextureSize(entry.tex.get(), &entry.w, &entry.h);
      }
    }
  } catch (...) {
  }

  auto &cached = textBoxCache_[file];
  cached = std::move(entry);
  return cached.tex.get();
}

} // namespace Kotonoha::PmailPort
