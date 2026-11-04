#pragma once

#include <Kotonoha/core/Scene.hpp>

#include <filesystem>
#include <memory>
#include <vector>

namespace Kotonoha {

class MediaPlayer;

// Each image fades in, then advances after a timeout or a click/touch.
// Videos play through once and include their embedded audio when present.
// Confirm advances; missing or undecodable files are skipped.
class SplashScreen : public Scene {
public:
  static constexpr double fadeSeconds = 0.8;
  static constexpr double displaySeconds = 3.0;

  SplashScreen(Engine &engine, std::vector<std::filesystem::path> media);
  ~SplashScreen() override;

  bool finished() const override { return current >= media.size(); }
  void restart() override;

  bool capturesInput() const override { return !finished(); }
  RenderSpace renderSpace() const override { return RenderSpace::Screen; }
  bool handleEvent(EventRef event) override;
  void update() override;
  void render(RendererRef renderer, float scale) override;

private:
  std::vector<std::filesystem::path> media;
  size_t current = 0;
  TexturePtr texture;
  std::unique_ptr<MediaPlayer> video;
  Uint64 startedNs = 0;
  bool loaded = false;

  void load(RendererRef renderer);
  void next();
  bool currentIsVideo() const;
  double elapsed() const;
};

} // namespace Kotonoha
