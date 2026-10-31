#pragma once

#include <Kotonoha/componentes/SubtitleScene.hpp>
#include <Kotonoha/parsers/Ors.h>

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
  void restart();
  bool finished() const { return done; }
  bool available() const { return !logo.empty(); }
  bool capturesInput() const override { return !done; }
  bool handleEvent(Kotonoha::EventRef event) override;
  void update() override;
  void render(Kotonoha::RendererRef renderer, float scale) override;
};

class Subtitles final : public Kotonoha::SubtitleScene {
public:
  using SubtitleScene::SubtitleScene;
  void bind(const Kotonoha_orsData &script);
};

class GameScene final : public Kotonoha::Scene {
  Runtime &runtime;
  std::string bar, chips;
  int hovered = -1, selectedChoice = -1;
  const Kotonoha_orsEvent *lastChoice = nullptr;
  bool pointerInBar = false;
  float barAlpha = 0;
  Uint64 lastUpdate = 0;
  void syncChoice();
  std::vector<int> choices() const;
  SDL_FRect choiceArea(int index, int count) const;
  void renderBar(Kotonoha::RendererRef renderer);

public:
  GameScene(Kotonoha::Engine &engine, Runtime &runtime);
  void update() override;
  bool handleEvent(Kotonoha::EventRef event) override;
  void render(Kotonoha::RendererRef renderer, float scale) override;
};
} // namespace SchoolDays
