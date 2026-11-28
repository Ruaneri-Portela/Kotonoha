#pragma once

#include <Kotonoha/components/SubtitleScene.hpp>
#include <Kotonoha/core/SDL.hpp>
#include <Kotonoha/localization/Translation.hpp>
#include <optional>
#include <Kotonoha/parsers/Ors.h>
#include <Kotonoha/routing/SchoolDaysChoiceTextPlan.hpp>
#include <Kotonoha/routing/SchoolDaysMenuBarPresenter.hpp>
#include <SDL3_ttf/SDL_ttf.h>
#include <string_view>

namespace SchoolDays {
class Assets;
class Runtime;

// Retail System/SYSTEM/LOGO shown after the engine splash, as the original
// School Days port did. The logo lives inside System.GPK, so it is decoded
// through the game's AssetProvider instead of Kotonoha::SplashScreen's files.
class LogoSplash final : public Kotonoha::Scene {
  Assets &assets;
  std::string logo;
  Uint64 started = 0;
  bool done = false, shown = false;
  double elapsed() const;

public:
  static constexpr double fadeSeconds = 0.8, displaySeconds = 3.0;
  LogoSplash(Kotonoha::Engine &engine, Assets &assets);
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
  std::vector<Kotonoha::SubtitleCue> originalCues_;
  std::vector<std::size_t> cueOrdinals_;
  int nodeId_ = -1;
  const Kotonoha::Translation *dlcDictionary_ = nullptr;
  std::optional<std::size_t> lxhFirstEvent_;
  std::string locale_ = "en";
  bool nativeGpkText_ = false;
  void refreshLocalizedCues();
protected:
  bool replacesPreviousCue() const override { return true; }
  std::string displayCueText(const std::string &text) const override;

public:
  using SubtitleScene::SubtitleScene;
  // Also used by headless color tests: the speaker remains the original ASS
  // style identifier even when text and names have JSON dictionary entries.
  static std::vector<Kotonoha::SubtitleCue> TranslateCues(
      const std::vector<Kotonoha::SubtitleCue> &original,
      const std::vector<std::size_t> &ordinals, int nodeId,
      const Kotonoha::Translation &dictionary,
      const Kotonoha::Translation *dlc = nullptr,
      std::optional<std::size_t> lxhFirstEvent = std::nullopt,
       std::string_view locale = "en", bool nativeGpkText = false);
  void bind(const Kotonoha_orsData &script, int nodeId,
            const Kotonoha::Translation *dlc = nullptr,
            std::optional<std::size_t> lxhFirstEvent = std::nullopt,
             std::string locale = "en", bool nativeGpkText = false);
  void translationChanged(const std::string &original,
                          const std::string &previous,
                          const std::string &translated) override;
};

class GameScene final : public Kotonoha::Scene {
  friend class RuntimeHandoffTestAccess;
  Runtime &runtime;
  Kotonoha::SchoolDaysMenuBarPresenter menuBar;
  Kotonoha::SchoolDaysSaveFeelModel::Variant menuBarVariant =
      Kotonoha::SchoolDaysSaveFeelModel::Variant::Standard;
  const Kotonoha_orsEvent *lastChoice = nullptr;
  bool menuBarAttempted = false;
  // The choice font must keep SDL_ttf initialized independently of Engine UI.
  // If Engine construction throws after Runtime::init, the two UI instances
  // are destroyed before Runtime::~Runtime releases GameScene. Declaring this
  // session first guarantees both fonts close before TTF_Quit.
  Kotonoha::TTFSession choiceFontTtfSession;
  Kotonoha::FontPtr choiceChineseFallback;
  Kotonoha::SDLHandle<TTF_Font, TTF_CloseFont> choiceFont;
  Kotonoha::TexturePtr choiceScratch;
  const Kotonoha_orsEvent *choiceScratchEvent = nullptr;
  std::vector<int> choiceScratchSlots;
  std::vector<std::string> choiceScratchTexts;
  std::vector<std::vector<Kotonoha::SchoolDaysChoiceTextPlan::Line>>
      choiceScratchLines;
  void syncChoice();
  bool ensureMenuBar();
  std::vector<int> choices() const;
  bool normalizedPointer(float windowX, float windowY, float *x, float *y) const;
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
  // Explicit NewGame/LOAD/RouteMap restoration changes KTRF session state
  // while this GameScene and its MenuBar remain alive in the same process.
  void restoreMenuBarFeelingState();
  // Retail keyboard accelerators from upstream use the presenter controls.
  void toggleMenuBarAutoHideLock() { menuBar.ToggleAutoHideLock(); }
  void toggleMenuBarTopMeter() { menuBar.ToggleTopMeter(); }
  bool pointerInsideMenuBar() const {
    return menuBar.IsOpen() && menuBar.PointerInsideBar();
  }
};
} // namespace SchoolDays
