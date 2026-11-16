#include "PmailStage.hpp"
#include <Pmail/Port.hpp>
#include <Kotonoha/core/Engine.hpp>
#include <Kotonoha/core/UI.hpp>
#include <Kotonoha/media/upscaling/Upscaler.hpp>
#include <algorithm>
#include <iostream>
#include <set>
#include <stdexcept>
#include <utility>

namespace Kotonoha::PmailPort {
namespace {
// The original 16-color text palette is not yet recovered. These values
// preserve readable contrast over the computer backgrounds until native
// palette entries can be verified.
SDL_Color computerColor(int index) {
  switch (index) {
  case 1:
    return {45, 45, 48, 255};
  case 8:
    return {34, 73, 143, 255};
  case 10:
    return {24, 38, 66, 255};
  default:
    return {35, 43, 59, 255};
  }
}
} // namespace

PmailStage::PmailStage(Kotonoha::Engine &engine, PmailGame &game,
                       Kotonoha::Providers::SnowAssets &assets)
    : Kotonoha::Scene(engine), game_(game), assets_(assets) {}

PmailStage::~PmailStage() = default;

void PmailStage::update() {
  // The stage doesn't really own game logic, it just reflects it.
}

void PmailStage::render(Kotonoha::RendererRef renderer, float scale) {
  // The original composes into an 800x600 framebuffer. Compose animation
  // layers at that resolution, then scale once; scaling every accumulated
  // OSBOOT frame separately makes software renderers progressively slower.
  if (!sceneTexture_) {
    sceneTexture_.reset(SDL_CreateTexture(
        &renderer, SDL_PIXELFORMAT_RGBA8888, SDL_TEXTUREACCESS_TARGET,
        800, 600));
    if (!sceneTexture_)
      throw std::runtime_error(SDL_GetError());
    SDL_SetTextureBlendMode(sceneTexture_.get(), SDL_BLENDMODE_NONE);
  }
  auto *previousTarget = SDL_GetRenderTarget(&renderer);
  if (!SDL_SetRenderTarget(&renderer, sceneTexture_.get()))
    throw std::runtime_error(SDL_GetError());
  struct RestoreTarget {
    SDL_Renderer *renderer;
    SDL_Texture *target;
    ~RestoreTarget() { SDL_SetRenderTarget(renderer, target); }
  } restore{&renderer, previousTarget};
  SDL_SetRenderDrawColor(&renderer, 0, 0, 0, 255);
  SDL_RenderClear(&renderer);
  usedAnimationTextures_.clear();
  // 1. Draw Background
  if (game_.filled()) {
    const uint32_t color = game_.fillColor();
    SDL_SetRenderDrawColor(&renderer, Uint8(color >> 16), Uint8(color >> 8),
                           Uint8(color), 255);
    SDL_RenderClear(&renderer);
  }
  updateBackground(renderer);
  if (backgroundTexture_) {
    const SDL_FRect stage{0, 0, 800, 600};
    SDL_RenderTexture(&renderer, backgroundTexture_.get(), nullptr, &stage);
  }

  renderLips(renderer, game_.background(), 0, 0);

  // 2. Draw Sprites
  renderSprites(renderer);
  renderAnimations(renderer);
  renderComputerUi(renderer);
  if (!SDL_SetRenderTarget(&renderer, previousTarget))
    throw std::runtime_error(SDL_GetError());
  const SDL_FRect stage{0, 0, 800, 600};
  // Apply the engine-selected upscaler to the 800x600 original game raster.
  // VN text and pause/save controls are rendered separately at full output
  // resolution; the original artwork is the part that benefits from scaling.
  if (!Kotonoha::RenderUpscaledTexture(
          &renderer, sceneTexture_.get(), nullptr, &stage,
          SDL_GetTicksNS()))
    throw std::runtime_error(SDL_GetError());
}

void PmailStage::updateBackground(Kotonoha::RendererRef renderer) {
  std::string bgName = game_.background();
  if (bgName.empty()) {
    renderedBackground_.clear();
    backgroundTexture_.reset();
    return;
  }

  if (renderedBackground_ != bgName) {
    backgroundTexture_.reset();

    try {
      auto surface = assets_.loadSurfaceAsset("graphic/" + bgName + ".CRGB");
      if (surface) {
        backgroundTexture_.reset(
            SDL_CreateTextureFromSurface(&renderer, surface.get()));
        renderedBackground_ = bgName;
      }
    } catch (...) {
      renderedBackground_ = bgName;
    }
  }
}

void PmailStage::renderSprites(Kotonoha::RendererRef renderer) {
  for (const auto &sp : game_.sprites()) {
    if (sp.name.empty())
      continue;

    auto it = spriteCache_.find(sp.name);
    if (it == spriteCache_.end()) {
      // Load sprite. We'll try common extensions.
      SpriteTex st;
      const char *exts[] = {".CHAR", ".chd", ".img", ".evd", ".CRGB"};
      for (const char *ext : exts) {
        std::string fullPath = sp.name + ext;
        try {
          auto surface = assets_.loadSurfaceAsset("graphic/" + fullPath);
          if (surface) {
            st.tex.reset(
                SDL_CreateTextureFromSurface(&renderer, surface.get()));
            if (st.tex) {
              SDL_GetTextureSize(st.tex.get(), &st.w, &st.h);
              break;
            }
          }
        } catch (...) {
        }
      }
      it = spriteCache_.emplace(sp.name, std::move(st)).first;
    }

    if (it->second.tex) {
      const auto placement = assets_.Sprite(sp.name);
      SDL_FRect dst{float(sp.x) + (placement.valid ? placement.x : 0.0f),
                    float(sp.y) + (placement.valid ? placement.y : 0.0f),
                    it->second.w, it->second.h};
      // Apply tint
      SDL_SetTextureColorMod(
          it->second.tex.get(), Uint8(std::clamp(sp.tint[0], 0, 0x100) * 255 / 0x100),
          Uint8(std::clamp(sp.tint[1], 0, 0x100) * 255 / 0x100),
          Uint8(std::clamp(sp.tint[2], 0, 0x100) * 255 / 0x100));
      SDL_SetTextureBlendMode(it->second.tex.get(), SDL_BLENDMODE_BLEND);
      SDL_RenderTexture(&renderer, it->second.tex.get(), nullptr, &dst);
      renderLips(renderer, sp.name, float(sp.x), float(sp.y), sp.tint);
    }
  }
}

void PmailStage::renderAnimationFrame(Kotonoha::RendererRef renderer,
                                      const std::string &name, uint32_t frame,
                                      float x, float y, const int *tint) {
  const std::string id = "graphic/" + name + ".anim#" + std::to_string(frame);
  usedAnimationTextures_.insert(id);
  auto found = animationCache_.find(id);
  if (found == animationCache_.end()) {
    const auto surface = assets_.loadSurfaceAsset(id);
    SpriteTex texture;
    texture.tex.reset(
        SDL_CreateTextureFromSurface(&renderer, surface.get()));
    if (!texture.tex)
      throw std::runtime_error(std::string("Cannot create ANIM texture: ") +
                               SDL_GetError());
    SDL_GetTextureSize(texture.tex.get(), &texture.w, &texture.h);
    SDL_SetTextureBlendMode(texture.tex.get(), SDL_BLENDMODE_BLEND);
    found = animationCache_.emplace(id, std::move(texture)).first;
  }
  const auto &placement = assets_.AnimationOf(name).frames.at(frame);
  const SDL_FRect dst{x + placement.x, y + placement.y, placement.w,
                      placement.h};
  SDL_SetTextureColorMod(
      found->second.tex.get(),
      tint ? Uint8(std::clamp(tint[0], 0, 256) * 255 / 256) : 255,
      tint ? Uint8(std::clamp(tint[1], 0, 256) * 255 / 256) : 255,
      tint ? Uint8(std::clamp(tint[2], 0, 256) * 255 / 256) : 255);
  SDL_RenderTexture(&renderer, found->second.tex.get(), nullptr, &dst);
}

void PmailStage::renderLips(Kotonoha::RendererRef renderer,
                            const std::string &name, float x, float y,
                            const int *tint) {
  if (name.empty() || !game_.voicePlaying || !game_.voicePlaying())
    return;
  const auto &animation = assets_.AnimationOf(name);
  const int frame = game_.voiceMouthFrame ? game_.voiceMouthFrame() : 0;
  for (const auto &region : animation.regions) {
    if (region.actor != uint32_t(game_.voiceActor()))
      continue;
    // Retain all mouth variants while this character is speaking.
    for (size_t i = 0; i < region.count; ++i)
      usedAnimationTextures_.insert("graphic/" + name + ".anim#" +
                                    std::to_string(region.first + i));
    renderAnimationFrame(renderer, name, uint32_t(region.first + size_t(frame)),
                         x, y, tint);
  }
}

void PmailStage::renderComputerUi(Kotonoha::RendererRef renderer) {
  // The previous implementation painted desktop/chat records onto unrelated
  // VN dialogue scenes. Native scene-change syscalls clear their lifetime,
  // and waiting story dialogue must never be covered by computer overlays.
  if (!game_.hasComputerOverlay() || game_.waitingForClick())
    return;
  // All positions are in native 800x600 scene units. The original game draws
  // chat text into its back buffer immediately; this renderer retains the
  // equivalent VM history and composites it over the background each frame.
  auto &ui = engine.GetVNUI();
  for (const auto &slot : game_.computerText()) {
    if (slot.width <= 0 || slot.height <= 0 || slot.text.empty())
      continue;
    ui.textSized(renderer, slot.text, float(slot.x), float(slot.y),
                 float(std::min(slot.width, 800)),
                 float(std::clamp(slot.size, 8, 48)),
                 computerColor(slot.color));
  }
  const auto clock = game_.computerClock();
  if (!clock.empty())
    ui.textSized(renderer, clock, 604, 32, 175, 18, computerColor(1));

  // The native chat pane's exact line offsets, font palette and wrapping
  // are under investigation; keep the window usable without claiming parity.
  const auto history = game_.chatHistory();
  float y = 174;
  for (const auto &line : history) {
    if (y >= 570)
      break;
    const SDL_Color color = line.mode == 2 ? computerColor(8)
                                          : computerColor(1);
    const float consumed = ui.textSized(renderer, line.text, 85, y, 624,
                                        17, color);
    y += std::clamp(consumed, 22.0f, 54.0f);
  }
  // The executable has separate chat and mail renderers (0x417d80 and
  // 0x411320). Keep their text streams distinct even when a scene contains
  // both. The final native coordinates/font sizes are still under study;
  // these are bounded fallback placements on the existing DVD backdrop.
  const auto chat = game_.chatPanel();
  const auto mail = game_.mailPanel();
  const bool split = chat.active && mail.active;
  auto drawPanel = [&](const PmailGame::ComputerPanel &panel, float x,
                       float top, float width) {
    if (!panel.active || panel.entries.empty())
      return;
    float lineY = top;
    // Keep only text that fits in the authored 800x600 frame.
    for (const auto &line : panel.entries) {
      if (lineY >= 565.0f)
        break;
      const auto color = line.mode == 2 ? computerColor(8)
                                        : computerColor(1);
      const auto height = ui.textSized(renderer, line.text, x, lineY, width,
                                       17.0f, color);
      lineY += std::clamp(height, 22.0f, 50.0f);
    }
  };
  // On original layouts the background provides the panel artwork; do not
  // invent a new mail/chat window frame or substitute unrelated sprites.
  if (split) {
    drawPanel(chat, 85.0f, 202.0f, 288.0f);
    drawPanel(mail, 422.0f, 202.0f, 292.0f);
  } else {
    drawPanel(chat, 85.0f, 202.0f, 620.0f);
    drawPanel(mail, 85.0f, 202.0f, 620.0f);
  }
}

void PmailStage::renderAnimations(Kotonoha::RendererRef renderer) {
  for (const auto &animation : game_.animations()) {
    const auto &preset = AnimationPresets[animation.preset];
    // Restore-mode frames redraw over the saved scene. OSBOOT accumulates
    // frames. Native 0x4075e8 retains the final image in the scene buffer.
    const uint32_t first = preset.restore ? animation.frame : 0;
    for (uint32_t frame = first; frame <= animation.frame; ++frame)
      renderAnimationFrame(renderer, preset.name, frame, float(animation.x),
                           float(animation.y));
  }
  for (auto it = animationCache_.begin(); it != animationCache_.end();) {
    if (!usedAnimationTextures_.count(it->first)) {
      it = animationCache_.erase(it);
    } else
      ++it;
  }
}

} // namespace Kotonoha::PmailPort
