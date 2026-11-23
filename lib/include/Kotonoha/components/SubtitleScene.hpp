#pragma once

#include <Kotonoha/core/Scene.hpp>
#include <Kotonoha/parsers/AssStyle.hpp>
#include <Kotonoha/workers/SubtitleFontRegistry.hpp>
#include <Kotonoha/workers/SubtitleSourceDirector.hpp>
#include <memory>

namespace Kotonoha {

class AssetProvider;

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
  // Subtitle source slots. Priority: forced > sidecar > embedded > default.
  // Forced/default follow the scene clock; sidecar/embedded follow the video
  // playback clock supplied by openVideoSubtitles/updateVideoClock.
  void setForcedSubtitles(std::string script);
  void clearForcedSubtitles();
  void setEmbeddedSubtitles(std::string script, double videoSeconds);
  void clearEmbeddedSubtitles();
  // Opens all video-derived sources for `videoAssetId`: a same-basename .ass
  // sidecar from the AssetProvider and, when this FFmpeg build can decode it,
  // an embedded subtitle stream. Returns true when any source was found.
  bool openVideoSubtitles(const AssetProvider &assets,
                          const std::string &videoAssetId,
                          double videoSeconds);
  void updateVideoClock(double seconds);
  void closeVideoSubtitles();
  bool hasEmbeddedSubtitleDecoder() const;
  workers::SubtitleSourceKind activeSubtitleSource() const;
  // Font attachments from the open video. Registered on the ASS_Library before
  // sidecar/embedded tracks are rasterized; deduplicated and size-bounded.
  size_t setAttachedFonts(
      const std::vector<workers::SubtitleAttachedFont> &fonts);
  void clearAttachedFonts();
  const std::vector<workers::SubtitleAttachedFont> &attachedFonts() const;
  size_t attachedFontBytes() const;
  void setPosition(double seconds);
  double getPosition() const { return position; }
  SDL_Point logicalSize() const { return {width, height}; }
  void render(RendererRef renderer, float scale) override;
  // The original (pre-translation) texts of the currently loaded cues, for
  // live translation lookup and active-key reporting.
  std::vector<std::string> currentCueTexts() const;
  std::vector<std::string> sourceCueTexts() const;
};

} // namespace Kotonoha
