#pragma once

#include <Kotonoha/core/Scene.hpp>
#include <Pmail/Port.hpp>
#include <SDL3/SDL.h>
#include <SnowRadish/SnowAssets.hpp>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>

namespace Kotonoha::PmailPort {

class PmailGame;

/**
 * PmailStage represents the main gameplay scene (the "stage").
 * It draws the background and all active sprites.
 */
class PmailStage final : public Kotonoha::Scene {
public:
  ~PmailStage() override;
  PmailStage(Kotonoha::Engine &engine, PmailGame &game,
             Kotonoha::Providers::SnowAssets &assets);

  void update() override;
  void render(Kotonoha::RendererRef renderer, float scale) override;

private:
  PmailGame &game_;
  Kotonoha::Providers::SnowAssets &assets_;

  // Background caching
  std::string renderedBackground_;
  SDL_Texture *backgroundTexture_ = nullptr;
  SDL_Texture *sceneTexture_ = nullptr;

  // Sprite caching
  struct SpriteTex {
    SDL_Texture *tex = nullptr;
    float w = 0, h = 0;
  };
  std::map<std::string, SpriteTex> spriteCache_;
  std::map<std::string, SpriteTex> animationCache_;
  std::set<std::string> usedAnimationTextures_;
  void renderAnimations(Kotonoha::RendererRef renderer);
  void renderLips(Kotonoha::RendererRef renderer, const std::string &name,
                  float x, float y, const int *tint = nullptr);
  void renderAnimationFrame(Kotonoha::RendererRef renderer,
                            const std::string &name, uint32_t frame, float x,
                            float y, const int *tint = nullptr);

  void updateBackground(Kotonoha::RendererRef renderer);
  void renderSprites(Kotonoha::RendererRef renderer);
};

} // namespace Kotonoha::PmailPort
