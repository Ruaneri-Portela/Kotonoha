#pragma once

#include <Kotonoha/componentes/SubtitleScene.hpp>
#include <Kotonoha/core/SDL.hpp>
#include <Kotonoha/parsers/Ors.h>
#include <Kotonoha/routing/SchoolDaysMenuBarPresenter.hpp>
#include <SDL3_ttf/SDL_ttf.h>

#include <SchoolDays/SchoolDaysAssets.hpp>

namespace SchoolDays {
class Runtime;

// Retail System/SYSTEM/LOGO shown after the engine splash, as the original
// School Days port did. The logo lives inside System.GPK, so it is decoded
// through the game's AssetProvider instead of Kotonoha::SplashScreen's files.
class LogoSplash final : public Kotonoha::Scene {
  Kotonoha::Providers::SchoolDaysAssets &assets;
  std::string logo;
  Uint64 started = 0;
  bool done = false, shown = false;
  double elapsed() const;

public:
  static constexpr double fadeSeconds = 0.8, displaySeconds = 3.0;
  LogoSplash(Kotonoha::Engine &engine, Kotonoha::Providers::SchoolDaysAssets &assets);
  void restart() override;
  bool finished() const override { return done; }
  bool available() const { return !logo.empty(); }
  bool capturesInput() const override { return !done; }
  Kotonoha::RenderSpace renderSpace() const override {
    return Kotonoha::RenderSpace::Screen;
  }
  bool handleEvent(Kotonoha::EventRef event) override;
  void update() override;
  void render(Kotonoha::RendererRef renderer, float scale) override;
};

class Subtitles final : public Kotonoha::SubtitleScene {
  std::vector<Kotonoha::SubtitleCue> originalCues;

protected:
  bool replacesPreviousCue() const override { return true; }
  std::string displayCueText(const std::string &text) const override;

public:
  using SubtitleScene::SubtitleScene;
  void bind(const Kotonoha_orsData &script, const std::string &section);
  std::vector<std::string> originalCueTexts() const;
  std::vector<std::string> currentOriginalCueTexts() const;
};

class GameScene final : public Kotonoha::Scene {
  Runtime &runtime;
  Kotonoha::SchoolDaysMenuBarPresenter menuBar;
  Kotonoha::SchoolDaysSaveFeelModel::Variant menuBarVariant =
      Kotonoha::SchoolDaysSaveFeelModel::Variant::Standard;
  const Kotonoha_orsEvent *lastChoice = nullptr;
  bool menuBarAttempted = false;
  // The choice font must keep SDL_ttf initialized independently of Engine UI.
  // If Engine construction throws after Runtime::init, the two UI instances
  // are destroyed before Runtime::~Runtime releases GameScene. Declaring this
  // session before choiceFont guarantees TTF_CloseFont runs before TTF_Quit.
  Kotonoha::TTFSession choiceFontTtfSession;
  Kotonoha::SDLHandle<TTF_Font, TTF_CloseFont> choiceFont;
  Kotonoha::TexturePtr choiceScratch;
  const Kotonoha_orsEvent *choiceScratchEvent = nullptr;
  void syncChoice();
  bool ensureMenuBar();
  std::vector<int> choices() const;
  bool normalizedPointer(float windowX, float windowY, float *x,
                         float *y) const;
  bool ensureChoiceScratch(Kotonoha::RendererRef renderer,
                           const std::vector<int> &visible);
  void renderChoices(Kotonoha::RendererRef renderer);

public:
  GameScene(Kotonoha::Engine &engine, Runtime &runtime);
  void update() override;
  bool handleEvent(Kotonoha::EventRef event) override;
  void render(Kotonoha::RendererRef renderer, float scale) override;
  bool handleMenuBarEvent(Kotonoha::EventRef event);
  void renderMenuBar(Kotonoha::RendererRef renderer);
};
} // namespace SchoolDays
