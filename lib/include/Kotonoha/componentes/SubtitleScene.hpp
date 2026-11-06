#pragma once

#include <Kotonoha/core/Scene.hpp>
#include <Kotonoha/parsers/AssStyle.hpp>
#include <memory>

namespace Kotonoha {

struct SubtitleCue {
  double start = 0, end = 0;
  std::string text, speaker;
  // The source is already ASS event text (for example retail ORS \N breaks).
  bool assText = false;
};

class SubtitleScene : public Scene {
  struct State;
  std::unique_ptr<State> state;
  double position = 0;
  int width, height;
  std::vector<SubtitleCue> sourceCues;
  float cueFontSize = 32;

protected:
  void setLogicalSize(int width, int height);
  // Dialogue streams replace the previous line; ordinary ASS timelines may
  // overlap.
  virtual bool replacesPreviousCue() const { return false; }
  virtual std::string displayCueText(const std::string &text) const {
    return text;
  }

public:
  explicit SubtitleScene(Engine &engine, int width = 1280, int height = 720);
  ~SubtitleScene() override;
  void loadAss(const std::string &script);
  void setStyle(AssStyle style);
  void setCues(const std::vector<SubtitleCue> &cues, float fontSize = 32);
  void translationChanged(const std::string &, const std::string &,
                          const std::string &) override;
  void clear();
  void setPosition(double seconds);
  double getPosition() const { return position; }
  SDL_Point logicalSize() const { return {width, height}; }
  void render(RendererRef renderer, float scale) override;
  // The original (pre-translation) texts of the currently loaded cues, for
  // live translation lookup and active-key reporting.
  std::vector<std::string> currentCueTexts() const;
};

} // namespace Kotonoha
