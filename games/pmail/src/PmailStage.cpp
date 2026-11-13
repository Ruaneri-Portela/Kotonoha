#include "PmailStage.hpp"
#include <Pmail/Port.hpp>
#include <algorithm>
#include <iostream>
#include <set>
#include <stdexcept>

namespace Kotonoha::PmailPort {

PmailStage::PmailStage(Kotonoha::Engine &engine, PmailGame &game,
                       Kotonoha::Providers::SnowAssets &assets)
    : Kotonoha::Scene(engine), game_(game), assets_(assets) {}

PmailStage::~PmailStage() {
  if (backgroundTexture_)
    SDL_DestroyTexture(backgroundTexture_);
  for (auto &[name, sprite] : spriteCache_)
    if (sprite.tex)
      SDL_DestroyTexture(sprite.tex);
  for (auto &[name, sprite] : animationCache_)
    if (sprite.tex)
      SDL_DestroyTexture(sprite.tex);
}

void PmailStage::update() {
  // The stage doesn't really own game logic, it just reflects it.
}

void PmailStage::render(Kotonoha::RendererRef renderer, float scale) {
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
    SDL_RenderTexture(&renderer, backgroundTexture_, nullptr, &stage);
  }

  renderLips(renderer, game_.background(), 0, 0);

  // 2. Draw Sprites
  renderSprites(renderer);
  renderAnimations(renderer);
}

void PmailStage::updateBackground(Kotonoha::RendererRef renderer) {
  std::string bgName = game_.background();
  if (bgName.empty()) {
    renderedBackground_.clear();
    if (backgroundTexture_) {
      SDL_DestroyTexture(backgroundTexture_);
      backgroundTexture_ = nullptr;
    }
    return;
  }

  if (renderedBackground_ != bgName) {
    if (backgroundTexture_) {
      SDL_DestroyTexture(backgroundTexture_);
      backgroundTexture_ = nullptr;
    }

    try {
      auto surface = assets_.loadSurfaceAsset("graphic/" + bgName + ".CRGB");
      if (surface) {
        backgroundTexture_ =
            SDL_CreateTextureFromSurface(&renderer, surface.get());
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
            st.tex = SDL_CreateTextureFromSurface(&renderer, surface.get());
            if (st.tex) {
              SDL_GetTextureSize(st.tex, &st.w, &st.h);
              break;
            }
          }
        } catch (...) {
        }
      }
      it = spriteCache_.emplace(sp.name, st).first;
    }

    if (it->second.tex) {
      const auto placement = assets_.Sprite(sp.name);
      SDL_FRect dst{float(sp.x) + (placement.valid ? placement.x : 0.0f),
                    float(sp.y) + (placement.valid ? placement.y : 0.0f),
                    it->second.w, it->second.h};
      // Apply tint
      SDL_SetTextureColorMod(
          it->second.tex, Uint8(std::clamp(sp.tint[0], 0, 0x100) * 255 / 0x100),
          Uint8(std::clamp(sp.tint[1], 0, 0x100) * 255 / 0x100),
          Uint8(std::clamp(sp.tint[2], 0, 0x100) * 255 / 0x100));
      SDL_SetTextureBlendMode(it->second.tex, SDL_BLENDMODE_BLEND);
      SDL_RenderTexture(&renderer, it->second.tex, nullptr, &dst);
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
    texture.tex = SDL_CreateTextureFromSurface(&renderer, surface.get());
    if (!texture.tex)
      throw std::runtime_error(std::string("Cannot create ANIM texture: ") +
                               SDL_GetError());
    SDL_GetTextureSize(texture.tex, &texture.w, &texture.h);
    SDL_SetTextureBlendMode(texture.tex, SDL_BLENDMODE_BLEND);
    found = animationCache_.emplace(id, texture).first;
  }
  const auto &placement = assets_.AnimationOf(name).frames.at(frame);
  const SDL_FRect dst{x + placement.x, y + placement.y, placement.w,
                      placement.h};
  SDL_SetTextureColorMod(
      found->second.tex,
      tint ? Uint8(std::clamp(tint[0], 0, 256) * 255 / 256) : 255,
      tint ? Uint8(std::clamp(tint[1], 0, 256) * 255 / 256) : 255,
      tint ? Uint8(std::clamp(tint[2], 0, 256) * 255 / 256) : 255);
  SDL_RenderTexture(&renderer, found->second.tex, nullptr, &dst);
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
      SDL_DestroyTexture(it->second.tex);
      it = animationCache_.erase(it);
    } else
      ++it;
  }
}

} // namespace Kotonoha::PmailPort
