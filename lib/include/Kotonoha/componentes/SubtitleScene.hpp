#pragma once

#include <Kotonoha/core/Scene.hpp>
#include <Kotonoha/parsers/AssStyle.hpp>
#include <memory>

namespace Kotonoha {

struct SubtitleCue {
  double start = 0, end = 0;
  std::string text, speaker;
};

class SubtitleScene : public Scene {
  struct State;
  std::unique_ptr<State> state;
  double position = 0;
  int width, height;

public:
  explicit SubtitleScene(Engine &engine, int width = 1280, int height = 720);
  ~SubtitleScene() override;
  void loadAss(const std::string &script);
  void setStyle(AssStyle style);
  void setCues(const std::vector<SubtitleCue> &cues, float fontSize = 32);
  void clear();
  void setPosition(double seconds);
  double getPosition() const { return position; }
  void render(RendererRef renderer, float scale) override;
};

} // namespace Kotonoha
