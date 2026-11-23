#pragma once
#include <Kotonoha/components/TouchActivation.hpp>
#include <Kotonoha/utils/TextWrapping.hpp>

#include <Kotonoha/core/Clock.hpp>
#include <Kotonoha/core/Engine.hpp>
#include <Kotonoha/core/Scene.hpp>

#include <algorithm>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace Kotonoha {

// Typewriter dialogue box. Games derive from it and override the protected
// hooks to change how the box looks, paces text or reaches the menus.
class VNTextBox : public Scene {
protected:
  TouchActivation touchActivation;
  Clock clock;

  std::string speakerName;
  std::string fullDialogue;
  std::string visibleDialogue;

  double cps = 30.0; // Caracteres por segundo
  double visibleChars = 0.0;
  bool isComplete = false;
  bool visible = true;
  bool userHidden = false;

  // Game-specific text styling. Defaults preserve the historical engine look,
  // so games that do not opt in are unaffected. Ports whose native UI draws
  // outlined white text (e.g. Artemis) set these in their derived text box.
  SDL_Color speakerColor{255, 215, 100, 255};
  SDL_Color dialogueColor{240, 240, 245, 255};
  SDL_Color textOutlineColor{0, 0, 0, 0}; // alpha 0 disables the outline
  int textOutlineWidth = 0;
  float speakerFontSize = 0.0f;  // 0 keeps the UI default
  float dialogueFontSize = 0.0f; // 0 keeps the UI default

  // Cached full-dialogue line breaks are stable during typewriter reveal.
  // Rebuild only when text, UI font instance or raster scale changes.
  TextWrapping::Layout dialogueLayout_;
  std::string layoutSource_;
  std::uint64_t layoutUiId_ = 0;
  float layoutDensity_ = 0.0f;
  bool layoutValid_ = false;

  virtual double charactersPerSecond() const { return cps; }
  // Draws one text run with the configured colour / optional outline. Keeps
  // the shared VNTextBox reusable while letting a port match native artwork.
  void drawStyledText(RendererRef renderer, UI &ui, const std::string &text,
                      float x, float y, float wrap, const SDL_Color &color,
                      float fontSize);
  virtual void recordDialogue(const std::string &speaker,
                              const std::string &dialogue);
  virtual void openMenu();
  virtual void openBacklog();
  virtual void renderFrame(RendererRef renderer);
  virtual void renderSpeaker(RendererRef renderer);
  virtual void renderDialogue(RendererRef renderer);
  virtual void renderIndicator(RendererRef renderer);

public:
  bool designSpace() const override { return true; }
  VNTextBox(Engine &engineRef, std::string speaker, std::string dialogue,
            double charsPerSecond = 30.0);

  virtual void setDialogue(const std::string &speaker,
                           const std::string &dialogue);
  void translationChanged(const std::string &original,
                          const std::string &previous,
                          const std::string &translated) override {
    replaceTranslation(original, previous, translated);
  }
  void replaceTranslation(const std::string &original,
                          const std::string &previous,
                          const std::string &translated);
  void setTextSpeed(double charsPerSecond);
  double getTextSpeed() const { return cps; }

  bool onEvent(EventRef event) override;
  bool handleEvent(EventRef event) override;
  void update() override;
  void render(RendererRef renderer, float scale) override;

  bool isDialogueComplete() const { return isComplete; }

  virtual void completeDialogue();

  void hide() { visible = false; }
  void show() { visible = true; }
  // User hiding is independent from script/content visibility. Calling show()
  // every frame must not reveal a box the player has hidden.
  void setUserHidden(bool hidden);
  void toggleUserHidden() { setUserHidden(!userHidden); }
  bool isUserHidden() const { return userHidden; }
  bool isVisible() const { return visible && !userHidden; }
  // Custom game handlers should call this before consuming dialogue input.
  // Restoring the box consumes the event without advancing the script.
  bool onVisibilityEvent(EventRef event);
  const std::string &getText() const { return fullDialogue; }
  const std::string &getSpeaker() const { return speakerName; }
  const std::string &getVisibleText() const { return visibleDialogue; }
};

VNTextBox &GetVNTextBoxRuntime(Engine &engineRef, const std::string &speaker,
                               const std::string &dialogue, double cps);

class VNTextBoxUtility {
public:
  static size_t CountUtf8Chars(const std::string &str) {
    size_t count = 0;
    size_t i = 0;
    while (i < str.size()) {
      i = NextUtf8Char(str, i);
      ++count;
    }
    return count;
  }

  static size_t NextUtf8Char(const std::string &str, size_t index) {
    return index >= str.size() ? str.size()
                               : TextWrapping::nextUtf8(str, index);
  }

  static std::string GetSubStringUtf8(const std::string &str,
                                      size_t charCount) {
    size_t index = 0;
    for (size_t i = 0; i < charCount && index < str.size(); ++i) {
      index = NextUtf8Char(str, index);
    }
    return str.substr(0, index);
  }

  // Text layout understands real newlines, escaped CR/LF/CRLF, literal
  // backslashes and UTF-8 character boundaries.
  static std::vector<std::string>
  RevealLines(const std::string &text, std::size_t visibleBytes,
              const std::function<float(const std::string &)> &measure,
              float width) {
    return TextWrapping::reveal(text, visibleBytes, measure, width);
  }

  static bool IsCJK(const std::string &ch) {
    return !ch.empty() && static_cast<unsigned char>(ch[0]) >= 0xe0;
  }

  static std::vector<std::string>
  WrapText(const std::string &text,
           const std::function<float(const std::string &)> &measureWidthFunc,
           float maxWidth) {
    return TextWrapping::wrap(text, measureWidthFunc, maxWidth);
  }
};

} // namespace Kotonoha
