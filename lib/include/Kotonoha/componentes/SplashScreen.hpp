#pragma once

#include <Kotonoha/core/Scene.hpp>

#include <filesystem>
#include <vector>

namespace Kotonoha {

// Each picture fades in, then advances after a timeout or a click/touch.
// Keys are ignored; missing or undecodable files are skipped.
class SplashScreen : public Scene {
public:
  static constexpr double fadeSeconds = 0.8;
  static constexpr double displaySeconds = 3.0;

  SplashScreen(Engine &engine, std::vector<std::filesystem::path> pictures);

  bool finished() const { return current >= pictures.size(); }
  void restart();

  bool capturesInput() const override { return !finished(); }
  RenderSpace renderSpace() const override { return RenderSpace::Screen; }
  bool handleEvent(EventRef event) override;
  void update() override;
  void render(RendererRef renderer, float scale) override;

private:
  std::vector<std::filesystem::path> pictures;
  size_t current = 0;
  TexturePtr texture;
  Uint64 startedNs = 0;
  bool loaded = false;

  void load(RendererRef renderer);
  void next();
  double elapsed() const;
};

} // namespace Kotonoha
