#pragma once

#include <Kotonoha/components/VNTextBox.hpp>
#include <Pmail/Port.hpp>
#include <SDL3/SDL.h>
#include <SnowRadish/SnowAssets.hpp>
#include <functional>
#include <map>

namespace Kotonoha::PmailPort {

class PmailGame;

/**
 * PmailTextBox is a specialized VNTextBox for PMAIL.
 * It uses the original message-window textures and ensures correct positioning.
 */
class PmailTextBox final : public Kotonoha::VNTextBox {
public:
  ~PmailTextBox() override;
  bool designSpace() const override { return false; }
  bool onEvent(Kotonoha::EventRef event) override;
  void update() override;
  void recordDialogue(const std::string &, const std::string &) override {}
  PmailTextBox(Kotonoha::Engine &engine, PmailGame &game,
               Kotonoha::Providers::SnowAssets &assets);

protected:
  // VNTextBox hooks
  void renderFrame(Kotonoha::RendererRef renderer) override;
  void renderSpeaker(Kotonoha::RendererRef renderer) override;
  void renderDialogue(Kotonoha::RendererRef renderer) override;
  void renderIndicator(Kotonoha::RendererRef renderer) override;

  // Overrides
  void setDialogue(const std::string &speaker,
                   const std::string &dialogue) override;
  void openMenu() override;
  void openBacklog() override;

private:
  PmailGame &game_;
  std::function<void()> menuAction_, backlogAction_;

public:
  void setMenuAction(std::function<void()> action) {
    menuAction_ = std::move(action);
  }
  void setBacklogAction(std::function<void()> action) {
    backlogAction_ = std::move(action);
  }

private:
  Kotonoha::Providers::SnowAssets &assets_;

  // Text box texture cache
  struct TextBoxTex {
    SDL_Texture *tex = nullptr;
    float w = 0, h = 0;
  };
  std::map<std::string, TextBoxTex> textBoxCache_;
  uint64_t indicatorTick_ = 0, indicatorMs_ = 0;

  SDL_Texture *LoadTextBoxGraphic(Kotonoha::RendererRef renderer,
                                  const std::string &file);
};

} // namespace Kotonoha::PmailPort
