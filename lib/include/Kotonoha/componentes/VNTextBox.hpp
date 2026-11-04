#pragma once
#include <Kotonoha/componentes/TouchActivation.hpp>

#include <Kotonoha/core/Clock.hpp>
#include <Kotonoha/core/Engine.hpp>
#include <Kotonoha/core/Scene.hpp>

#include <string>

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

  virtual double charactersPerSecond() const { return cps; }
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
  bool isVisible() const { return visible; }
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
    if (index >= str.size())
      return str.size();
    const unsigned char c = static_cast<unsigned char>(str[index]);
    const size_t length = (c < 0x80)           ? 1
                          : ((c >> 5) == 0x6)  ? 2
                          : ((c >> 4) == 0xE)  ? 3
                          : ((c >> 3) == 0x1E) ? 4
                                               : 1;
    return std::min(str.size(), index + length);
  }

  static std::string GetSubStringUtf8(const std::string &str,
                                      size_t charCount) {
    size_t index = 0;
    for (size_t i = 0; i < charCount && index < str.size(); ++i) {
      index = NextUtf8Char(str, index);
    }
    return str.substr(0, index);
  }

  static std::vector<std::string>
  RevealLines(const std::string &text, std::size_t visibleBytes,
              const std::function<float(const std::string &)> &measure,
              float width) {
    const auto lines = WrapText(text, measure, width);
    std::vector<std::string> visible;
    std::size_t offset = 0;
    for (const auto &line : lines) {
      if (line.empty()) {
        if (offset >= visibleBytes)
          break;
        visible.push_back("");
        while (offset < text.size()) {
          if (text.compare(offset, 4, "\\r\\n") == 0) {
            offset += 4;
          } else if (text.compare(offset, 2, "\\n") == 0 ||
                     text.compare(offset, 2, "\\r") == 0 ||
                     text.compare(offset, 2, "\r\n") == 0) {
            offset += 2;
          } else if (text[offset] == '\n' || text[offset] == '\r') {
            offset += 1;
          } else {
            break;
          }
        }
        continue;
      }
      auto start = text.find(line, offset);
      if (start == std::string::npos || start >= visibleBytes)
        break;
      visible.push_back(
          line.substr(0, std::min(line.size(), visibleBytes - start)));
      offset = start + line.size();
      while (offset < text.size()) {
        if (text.compare(offset, 4, "\\r\\n") == 0) {
          offset += 4;
        } else if (text.compare(offset, 2, "\\n") == 0 ||
                   text.compare(offset, 2, "\\r") == 0 ||
                   text.compare(offset, 2, "\r\n") == 0) {
          offset += 2;
        } else if (text[offset] == '\n' || text[offset] == '\r') {
          offset += 1;
        } else {
          break;
        }
      }
    }
    return visible;
  }

  static bool IsCJK(const std::string &ch) {
    if (ch.empty())
      return false;
    const unsigned char c = static_cast<unsigned char>(ch[0]);
    return (c >= 0xE0);
  }

  static std::vector<std::string>
  WrapText(const std::string &text,
           const std::function<float(const std::string &)> &measureWidthFunc,
           float maxWidth) {
    std::vector<std::string> lines;
    if (text.empty())
      return lines;

    std::string currentLine;
    std::string currentWord;

    for (size_t i = 0; i < text.size();) {
      size_t newlineSeqLen = 0;
      if (text.compare(i, 4, "\\r\\n") == 0) {
        newlineSeqLen = 4;
      } else if (text.compare(i, 2, "\\n") == 0 ||
                 text.compare(i, 2, "\\r") == 0) {
        newlineSeqLen = 2;
      } else if (text.compare(i, 2, "\r\n") == 0) {
        newlineSeqLen = 2;
      } else if (text[i] == '\n' || text[i] == '\r') {
        newlineSeqLen = 1;
      }

      if (newlineSeqLen > 0) {
        currentLine += currentWord;
        lines.push_back(std::move(currentLine));
        currentLine.clear();
        currentWord.clear();
        i += newlineSeqLen;
        continue;
      }

      const size_t next = NextUtf8Char(text, i);
      const std::string ch = text.substr(i, next - i);
      i = next;

      if (IsCJK(ch)) {
        currentLine += currentWord;
        currentWord.clear();

        if (!currentLine.empty() &&
            measureWidthFunc(currentLine + ch) > maxWidth) {
          lines.push_back(std::move(currentLine));
          currentLine = ch;
        } else {
          currentLine += ch;
        }
      } else if (ch == " ") {
        currentWord += ch;
        if (!currentLine.empty() &&
            measureWidthFunc(currentLine + currentWord) > maxWidth) {
          lines.push_back(std::move(currentLine));
          currentLine = currentWord;
        } else {
          currentLine += currentWord;
        }
        currentWord.clear();
      } else {
        currentWord += ch;
        if (measureWidthFunc(currentLine + currentWord) > maxWidth) {
          if (!currentLine.empty()) {
            lines.push_back(std::move(currentLine));
            currentLine.clear();
          } else {
            lines.push_back(std::move(currentWord));
            currentWord.clear();
          }
        }
      }
    }

    currentLine += currentWord;
    if (!currentLine.empty()) {
      lines.push_back(std::move(currentLine));
    }

    return lines;
  }
};

} // namespace Kotonoha
