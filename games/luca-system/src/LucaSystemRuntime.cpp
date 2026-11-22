// Copyright 2026 Kotonoha contributors. SPDX-License-Identifier: MIT
// LucaSystem (Visual Art's / Key) runtime — 1:1 VM & presentation recovered via
// clean-room REA + Rizin analysis of AIR.exe (.rdata opcode/chapter tables) and
// exhaustive audit of all 31 .PAK archives.
#include <LucaSystem/AirGallery.hpp>
#include <LucaSystem/BitmapFont.hpp>
#include <LucaSystem/CzImage.hpp>
#include <LucaSystem/DebugCatalog.hpp>
#include <LucaSystem/DialogueFlow.hpp>
#include <LucaSystem/LucaSystemRuntime.hpp>
#include <LucaSystem/LucaSystemScript.hpp>
#include <LucaSystem/SaveState.hpp>
#include <LucaSystem/SceneLayers.hpp>
#include <LucaSystem/GpuSceneComposition.hpp>

#include <Kotonoha/Filesystem/AtomicFile.hpp>
#include <Kotonoha/Filesystem/Filesystem.hpp>
#include <Kotonoha/Filesystem/StoragePaths.hpp>
#include <Kotonoha/components/ChoiceQuestionScene.hpp>
#include <Kotonoha/components/InGameMenu.hpp>
#include <Kotonoha/components/SettingsMenu.hpp>
#include <Kotonoha/components/VNBacklog.hpp>
#include <Kotonoha/components/VNSaveMenu.hpp>
#include <Kotonoha/components/VNTextBox.hpp>
#include <Kotonoha/core/Engine.hpp>
#include <Kotonoha/core/SDL.hpp>
#include <Kotonoha/core/UI.hpp>
#include <Kotonoha/events/BackAction.hpp>
#include <Kotonoha/events/ControllerEvent.hpp>
#include <Kotonoha/events/VideoSkipGesture.hpp>
#include <Kotonoha/media/MediaPlayer.hpp>
#include <Kotonoha/media/PcmPlayer.hpp>
#include <Kotonoha/media/TextureAlpha.hpp>
#include <Kotonoha/media/upscaling/Upscaler.hpp>
#include <SDL3/SDL.h>

#include <algorithm>
#include <cctype>
#include <charconv>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <map>
#include <memory>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace LucaSystem {

namespace {

bool isSystemScript(std::string_view name) { return name.rfind("_", 0) == 0; }

struct AirTitleItem {
  int atlasX, width;
  float center;
};

// Native-size English labels, laid out separately rather than squeezing the
// entire atlas into the viewport. Page two slides into the same strip.
constexpr AirTitleItem titleItems[] = {
    {13, 212, 271},   {840, 114, 482},  {981, 177, 676}, {1185, 172, 896},
    {1383, 87, 1072}, {1497, 186, 509}, {1710, 146, 771}};
constexpr int titlePages[]{0, 0, 0, 0, 0, 1, 1};
constexpr AirTitleItem storyItems[] = {
    {60, 161, 640}, {325, 195, 640}, {662, 84, 640}, {858, 372, 640}};
constexpr const char *storyRoots[]{"seen170", "seen700", "seen163", "seen800"};
constexpr SDL_FRect storyClose{1140, 680, 134, 40};

bool drawAirArt(Kotonoha::Providers::LucaSystemAssets &assets,
                Kotonoha::RendererRef renderer, const char *id, SDL_FRect dst,
                const SDL_FRect *src = nullptr) {
  try {
    if (assets.contains(id)) {
      auto &texture = assets.texture(renderer, id);
      return Kotonoha::RenderUpscaledTexture(&renderer, &texture, src, &dst);
    }
  } catch (const std::exception &) {
  }
  return false;
}

uint16_t paramU16(const ScriptInstruction &ins, size_t offset) {
  if (offset + 2 > ins.paramBytes.size())
    return 0;
  return uint16_t(ins.paramBytes[offset]) |
         (uint16_t(ins.paramBytes[offset + 1]) << 8);
}

uint32_t paramU32(const ScriptInstruction &ins, size_t offset) {
  if (offset + 4 > ins.paramBytes.size())
    return 0;
  return uint32_t(ins.paramBytes[offset]) |
         (uint32_t(ins.paramBytes[offset + 1]) << 8) |
         (uint32_t(ins.paramBytes[offset + 2]) << 16) |
         (uint32_t(ins.paramBytes[offset + 3]) << 24);
}

float paramFloat(const ScriptInstruction &ins, size_t offset) {
  if (offset + 4 > ins.paramBytes.size())
    return 0.0f;
  const uint32_t bits = paramU32(ins, offset);
  float value = 0.0f;
  std::memcpy(&value, &bits, sizeof(float));
  return std::isfinite(value) ? value : 0.0f;
}

uint64_t framesToMs(int frames) {
  if (frames <= 0)
    return 0;
  return (static_cast<uint64_t>(std::min(frames, 36000)) * 1000u + 30u) / 60u;
}

// Exact 140-entry opcode table verified against AIR.exe .rdata
// (0x14045fd40..0x140460198; index 135 is NULL!).
enum Opcode : uint8_t {
  OP_EQU = 0,
  OP_EQUN = 1,
  OP_EQUV = 2,
  OP_ADD = 3,
  OP_SUB = 4,
  OP_MUL = 5,
  OP_DIV = 6,
  OP_MOD = 7,
  OP_AND = 8,
  OP_OR = 9,
  OP_RANDOM = 10,
  OP_VARSTR = 11,
  OP_VARSTR_ADD = 12,
  OP_SET = 13,
  OP_FLAGCLR = 14,
  OP_GOTO = 15,
  OP_ONGOTO = 16,
  OP_CALL = 17,
  OP_IFY = 18,
  OP_IFN = 19,
  OP_RETURN = 20,
  OP_JUMP = 21,
  OP_FARCALL = 22,
  OP_FARRETURN = 23,
  OP_JUMPPOINT = 24,
  OP_END = 25,
  OP_STARTUP_BEGIN = 26,
  OP_STARTUP_END = 27,
  OP_VARSTR_SET = 29,
  OP_ARFLAGSET = 31,
  OP_SHAKELIST_SET = 34,
  OP_MESSAGE = 36,
  OP_MESSAGE_CLEAR = 37,
  OP_MESSAGE_WAIT = 38,
  OP_SELECT = 40,
  OP_CLOSE_WINDOW = 41,
  OP_FADE_WINDOW = 42,
  OP_LOG_BEGIN = 43,
  OP_LOG_BREAK = 44,
  OP_LOG_END = 45,
  OP_VOICE = 46,
  OP_VOICE_STOP = 47,
  OP_WAIT_COUNT = 48,
  OP_WAIT_TIME = 49,
  OP_WAIT_TEXTFEED = 50,
  OP_FFSTOP = 52,
  OP_INIT = 53,
  OP_STOP = 54,
  OP_IMAGELOAD = 55,
  OP_IMAGEUPDATE = 56,
  OP_MOVE = 58,
  OP_MOVE_SKIP = 59,
  OP_ROT = 60,
  OP_FADE = 62,
  OP_SCALE = 63,
  OP_SHAKE = 64,
  OP_SHAKELIST = 65,
  OP_BASE = 66,
  OP_FADE_OBJ = 71,
  OP_WAIT = 72,
  OP_WAIT_BSKIP = 73,
  OP_DRAW = 74,
  OP_WIPE = 75,
  OP_DELAY = 80,
  OP_TONE = 82,
  OP_DISP = 87,
  OP_FACE = 89,
  OP_SEPIA = 90,
  OP_PRIORITY = 98,
  OP_QUAKE = 106,
  OP_BGM = 107,
  OP_BGM_WAIT_START = 108,
  OP_BGM_WAIT_FADE = 109,
  OP_BGM_PUSH = 110,
  OP_BGM_POP = 111,
  OP_BGM_ASYNC_FADE_STOP = 112,
  OP_SE = 113,
  OP_SE_STOP = 114,
  OP_SE_WAIT = 115,
  OP_SE_WAIT_COUNT = 116,
  OP_SE_WAIT_FADE = 117,
  OP_VOLUME = 118,
  OP_MOVIE = 119,
  OP_SETCGFLAG = 120,
  OP_EX = 121,
  OP_TROPHY = 122,
  OP_SETBGMFLAG = 123,
  OP_TASK = 124,
  OP_DIALOG = 126,
  OP_SCENE_REPLAY_END = 131,
  OP_MANPU = 136,
  OP_SCENARIO = 137,
  OP_HAIKEI_SET = 138,
};

std::vector<std::string> splitDelimiter(const std::string &str,
                                        std::string_view delim) {
  std::vector<std::string> result;
  if (str.empty())
    return result;
  size_t pos = 0;
  while (pos <= str.size()) {
    size_t found = str.find(delim, pos);
    std::string item = (found == std::string::npos)
                           ? str.substr(pos)
                           : str.substr(pos, found - pos);
    if (!item.empty())
      result.push_back(std::move(item));
    if (found == std::string::npos)
      break;
    pos = found + delim.size();
  }
  return result;
}

// Sanitize LucaSystem inline tags: @Speaker@, $n, $w, $[base$ruby$], $C(...),
// $S(...)
void parseSpeakerAndSanitizeText(const std::string &rawText,
                                 std::string &speaker, std::string &text) {
  speaker.clear();
  std::string body = rawText;
  if (body.empty()) {
    text.clear();
    return;
  }

  // 1. Extract @Speaker@ (canonical AIR format), 【Speaker】, or [Speaker].
  if (body.front() == '@') {
    const size_t closeAt = body.find('@', 1);
    if (closeAt != std::string::npos && closeAt > 1 && closeAt < 64) {
      speaker = body.substr(1, closeAt - 1);
      body.erase(0, closeAt + 1);
    }
  } else if (body.rfind("【", 0) == 0) {
    const size_t end = body.find("】");
    if (end != std::string::npos && end > 3) {
      speaker = body.substr(3, end - 3);
      body = (end + 3 < body.size()) ? body.substr(end + 3) : std::string{};
    }
  } else if (body.front() == '[') {
    const size_t end = body.find(']');
    if (end != std::string::npos && end > 1 && end < 48) {
      speaker = body.substr(1, end - 1);
      body = (end + 1 < body.size()) ? body.substr(end + 1) : std::string{};
    }
  }

  // Strip inline formatting tags in speaker too (e.g. @$S(20)Speaker@)
  auto cleanInlineTags = [](const std::string &src) {
    std::string out;
    out.reserve(src.size());
    for (size_t i = 0; i < src.size();) {
      if (src[i] == '$') {
        if (i + 1 < src.size()) {
          const char next = src[i + 1];
          if (next == 'n' || next == 'N') {
            out.push_back('\n');
            i += 2;
            continue;
          }
          if (next == 'w' || next == 'W' || next == 'k' || next == 'K' ||
              next == 'r' || next == 'p') {
            i += 2;
            continue;
          }
          if (next == '[') {
            // $[base$ruby$] or $[text$]
            const size_t endBracket = src.find("$]", i + 2);
            if (endBracket != std::string::npos) {
              const std::string inner = src.substr(i + 2, endBracket - (i + 2));
              const size_t sep = inner.find('$');
              out += (sep != std::string::npos) ? inner.substr(0, sep) : inner;
              i = endBracket + 2;
              continue;
            }
          }
          if ((next == 'S' || next == 'C' || next == 'F' || next == 'A') &&
              i + 2 < src.size() && (src[i + 2] == '(' || src[i + 2] == '[')) {
            const char closer = (src[i + 2] == '(') ? ')' : ']';
            const size_t closePos = src.find(closer, i + 3);
            if (closePos != std::string::npos) {
              i = closePos + 1;
              continue;
            }
          }
        }
      }
      out.push_back(src[i++]);
    }
    return out;
  };

  speaker = cleanInlineTags(speaker);
  text = cleanInlineTags(body);
}

class AirTextBox final : public Kotonoha::VNTextBox {
public:
  enum class Control { Menu, Skip, Auto, Voice };

private:
  Kotonoha::Providers::LucaSystemAssets &assets_;
  // AIR's PUA and decorative symbols are present in the retail bitmap
  // atlas, but are not guaranteed to exist in any system TTF.
  std::optional<BitmapFont> originalGlyphs_;
  bool originalAtlasAvailable_ = false;
  static constexpr const char *originalAtlasId_ = "ゴシック27";
  std::array<CzImage, 5> frames_;
  CzImage fallbackStrip_;
  CzImage cursor_;
  std::array<Kotonoha::TexturePtr, 5> frameTextures_;
  Kotonoha::TexturePtr fallbackStripTexture_;
  Kotonoha::TexturePtr cursorTexture_;
  int windowType_ = 0;
  std::function<void(Control)> onControl_;
  bool skipOn_ = false, autoOn_ = false, voiceAvailable_ = false;
  int hoverControl_ = -1;

  static SDL_FRect controlRect(int i) {
    return {1130, 545.0f + i * 40, 130, 38};
  }

  void renderControls(Kotonoha::RendererRef renderer) {
    static constexpr const char *labels[]{"Menu", "Skip", "Auto", "Voice"};
    static constexpr const char *icons[]{"⚙", "⏩", "⇄", "♪"};
    static constexpr const char *plainIcons[]{"M", "S", "A", "V"};
    static constexpr std::uint32_t iconCodepoints[]{0x2699, 0x23e9,
                                                      0x21c4, 0x266a};
    auto &ui = engine.GetVNUI();
    for (int i = 0; i < 4; ++i) {
      const auto box = controlRect(i);
      const SDL_Color ink =
          i == 3 && !voiceAvailable_ ? SDL_Color{133, 143, 150, 155}
          : i == hoverControl_ || (i == 1 && skipOn_) || (i == 2 && autoOn_)
              ? SDL_Color{48, 76, 159, 255}
              : SDL_Color{111, 128, 142, 255};
      // Built-in TTFs may not contain these four symbols either. Use the
      // game's bitmap glyphs when available; otherwise use a plain icon label
      // rather than a missing-glyph box.
      if (isBitmapOnlyGlyph(iconCodepoints[i], ui) ||
          ui.hasGlyph(iconCodepoints[i]))
        drawMixedText(renderer, icons[i], box.x + 3, box.y + 3, ink);
      else
        ui.text(renderer, plainIcons[i], box.x + 3, box.y + 3, 28, ink);
      ui.text(renderer, labels[i], box.x + 33, box.y + 4, box.w - 33, ink);
    }
  }

  bool isBitmapOnlyGlyph(std::uint32_t cp, const Kotonoha::UI &ui) const {
    // Private-use codepoints are the game's own glyph identifiers. Prefer
    // the retail atlas even if an unrelated font maps the same PUA position.
    const bool gameSymbol = cp >= 0xe000u && cp <= 0xf8ffu;
    return originalGlyphs_ && originalAtlasAvailable_ &&
           (gameSymbol || !ui.hasGlyph(cp)) &&
           originalGlyphs_->glyph(cp).has_value();
  }

  template <class Visit>
  void forEachFontRun(std::string_view text, Kotonoha::UI &ui,
                      Visit &&visit) const {
    const char *cursor = text.data();
    std::size_t remaining = text.size();
    std::string ttfRun;
    const auto flushTtf = [&]() {
      if (!ttfRun.empty()) {
        visit(std::string_view(ttfRun), false);
        ttfRun.clear();
      }
    };
    while (remaining > 0) {
      const char *start = cursor;
      const std::uint32_t cp = SDL_StepUTF8(&cursor, &remaining);
      const std::string_view glyphBytes(start,
                                        static_cast<std::size_t>(cursor - start));
      if (isBitmapOnlyGlyph(cp, ui)) {
        flushTtf();
        visit(glyphBytes, true);
      } else {
        ttfRun.append(glyphBytes.data(), glyphBytes.size());
      }
    }
    flushTtf();
  }

  float mixedTextWidth(std::string_view text, Kotonoha::UI &ui) const {
    float width = 0.0f;
    forEachFontRun(text, ui, [&](std::string_view run, bool bitmap) {
      if (bitmap) {
        const char *cursor = run.data();
        std::size_t count = run.size();
        const auto cp = SDL_StepUTF8(&cursor, &count);
        width += originalGlyphs_->glyph(cp)->advance;
      } else {
        width += ui.textWidth(std::string(run));
      }
    });
    return width;
  }

  void drawMixedText(Kotonoha::RendererRef renderer, std::string_view text,
                     float x, float y, SDL_Color color) {
    auto &ui = engine.GetVNUI();
    SDL_Texture *atlas = nullptr;
    if (originalGlyphs_ && originalAtlasAvailable_) {
      try {
        atlas = &assets_.texture(renderer, originalAtlasId_);
        if (!originalGlyphs_->fitsAtlas(float(atlas->w), float(atlas->h)))
          atlas = nullptr;
      } catch (const std::exception &) {
        atlas = nullptr;
      }
    }
    forEachFontRun(text, ui, [&](std::string_view run, bool bitmap) {
      if (bitmap && atlas) {
        // The bitmap glyph is copied at its native cell size, never enhanced
        // or upscaled. All glyphs covered by TTF stay HiDPI vector text.
        originalGlyphs_->render(renderer, *atlas, run, x, y, color);
        const char *cursor = run.data();
        std::size_t count = run.size();
        x += originalGlyphs_->glyph(SDL_StepUTF8(&cursor, &count))->advance;
      } else {
        const std::string ttf(run);
        ui.text(renderer, ttf, x, y, 4096.0f, color);
        x += ui.textWidth(ttf);
      }
    });
  }

  static CzImage tryReadCz(const Kotonoha::Providers::LucaSystemAssets &assets,
                           const char *name) {
    try {
      if (assets.contains(name))
        return DecodeCzImage(assets.read(name));
    } catch (...) {
    }
    return {};
  }

  SDL_Texture *texture(Kotonoha::RendererRef renderer,
                       Kotonoha::TexturePtr &target, const CzImage &image) {
    if (image.rgba.empty())
      return nullptr;
    if (!target) {
      target.reset(uploadAirStaticRgba(renderer, image.width, image.height,
                                       image.rgba.data(), image.width * 4,
                                       SDL_SCALEMODE_LINEAR));
      if (!target)
        return nullptr;
    }
    return target.get();
  }

public:
  void setWindowType(int wt) { windowType_ = std::clamp(wt, 0, 4); }
  void renderChoiceControls(Kotonoha::RendererRef renderer) {
    renderControls(renderer);
  }
  void setControlAction(std::function<void(Control)> action) {
    onControl_ = std::move(action);
  }
  void setControls(bool skip, bool automatic, bool voice) {
    skipOn_ = skip;
    autoOn_ = automatic;
    voiceAvailable_ = voice;
  }

  bool handleControlClick(Kotonoha::EventRef event, bool allowVoice = true) {
    for (int i = 0; i < 4; ++i) {
      if (Kotonoha::UI::clicked(event, controlRect(i))) {
        if (onControl_ && (allowVoice || i != int(Control::Voice)))
          onControl_(static_cast<Control>(i));
        return true;
      }
    }
    return false;
  }

  bool onEvent(Kotonoha::EventRef rawEvent) override {
    if (!visible)
      return false;
    const auto event = Kotonoha::MapControllerEvent(rawEvent);
    if (!touchActivation.accept(event) || onVisibilityEvent(event))
      return true;
    if (Kotonoha::BackAction(engine, event)) {
      openMenu();
      return true;
    }
    if (event.type == SDL_EVENT_KEY_DOWN && !event.key.repeat &&
        (event.key.key == SDLK_B || event.key.key == SDLK_PAGEUP)) {
      openBacklog();
      return true;
    }
    if (event.type == SDL_EVENT_MOUSE_MOTION) {
      hoverControl_ = -1;
      const SDL_FPoint point{event.motion.x, event.motion.y};
      for (int i = 0; i < 4; ++i) {
        const auto box = controlRect(i);
        if (SDL_PointInRectFloat(&point, &box))
          hoverControl_ = i;
      }
    }
    return handleControlClick(event);
  }

protected:
  // The VM records MESSAGE and explicit log opcodes (including voice IDs).
  // Rendering, loading saves and translations must not insert a second copy.
  void recordDialogue(const std::string &, const std::string &) override {}

  void renderFrame(Kotonoha::RendererRef renderer) override {
    const int idx = std::clamp(windowType_, 0, 4);
    const CzImage &chosen =
        !frames_[idx].rgba.empty() ? frames_[idx] : frames_[0];
    Kotonoha::TexturePtr &chosenTex =
        !frames_[idx].rgba.empty() ? frameTextures_[idx] : frameTextures_[0];
    auto *tex = texture(renderer, chosenTex, chosen);
    if (tex) {
      SDL_FRect destination{0, 480, 1280, 240};
      SDL_SetTextureAlphaMod(tex, 175);
      Kotonoha::RenderUpscaledTexture(&renderer, tex, nullptr, &destination);
      SDL_SetTextureAlphaMod(tex, 255);
    } else if (auto *strip =
                   texture(renderer, fallbackStripTexture_, fallbackStrip_)) {
      SDL_FRect destination{165, 543, 950, 162};
      Kotonoha::RenderUpscaledTexture(&renderer, strip, nullptr, &destination);
    } else {
      SDL_SetRenderDrawBlendMode(&renderer, SDL_BLENDMODE_BLEND);
      SDL_SetRenderDrawColor(&renderer, 40, 48, 72, 210);
      SDL_FRect box{165, 543, 950, 162};
      SDL_RenderFillRect(&renderer, &box);
      SDL_SetRenderDrawColor(&renderer, 100, 115, 150, 120);
      SDL_RenderRect(&renderer, &box);
    }
    renderControls(renderer);
  }

  void renderIndicator(Kotonoha::RendererRef renderer) override {
    auto *tex = texture(renderer, cursorTexture_, cursor_);
    if (tex) {
      const int frame = int((SDL_GetTicks() / 120) % 4);
      SDL_FRect source{0, float(frame * 38), 48, 38};
      SDL_FRect destination{1056, 660, 48, 38};
      Kotonoha::RenderUpscaledTexture(&renderer, tex, &source, &destination);
    }
  }

  void renderSpeaker(Kotonoha::RendererRef renderer) override {
    if (speakerName.empty())
      return;
    const auto &settings = engine.GetSettings();
    const SDL_Color color = settings.vnTextColorOverride
                                ? settings.vnSpeakerColor
                                : SDL_Color{255, 238, 175, 255};
    // SDL_ttf draws directly at the active pixel density, not through the
    // image upscaler or the original 27px bitmap atlas.
    drawMixedText(renderer, speakerName, 188, 516, color);
  }

  void renderDialogue(Kotonoha::RendererRef renderer) override {
    SDL_Rect clip{182, 548, 904, 148};
    SDL_Rect previousClip{};
    const bool clipped = SDL_RenderClipEnabled(&renderer) &&
                         SDL_GetRenderClipRect(&renderer, &previousClip);
    if (clipped) {
      SDL_Rect intersection{};
      if (!SDL_GetRectIntersection(&clip, &previousClip, &intersection))
        return;
      clip = intersection;
    }
    SDL_SetRenderClipRect(&renderer, &clip);
    auto &ui = engine.GetVNUI();
    if (!layoutValid_ || layoutSource_ != fullDialogue ||
        layoutDensity_ != ui.pixelDensity() || layoutUiId_ != ui.instanceId()) {
      const auto measure = [this, &ui](const std::string &value) {
        return mixedTextWidth(value, ui);
      };
      dialogueLayout_ =
          Kotonoha::TextWrapping::layout(fullDialogue, measure, 898);
      layoutSource_ = fullDialogue;
      layoutDensity_ = ui.pixelDensity();
      layoutUiId_ = ui.instanceId();
      layoutValid_ = true;
    }
    const auto lines = Kotonoha::TextWrapping::visibleLines(
        dialogueLayout_, visibleDialogue.size());
    float y = 564;
    const auto &settings = engine.GetSettings();
    const SDL_Color color = settings.vnTextColorOverride
                                ? settings.vnTextColor
                                : SDL_Color{255, 255, 255, 255};
    for (const auto &line : lines) {
      const SDL_Color shadow{15, 21, 32, 220};
      drawMixedText(renderer, line, 190, y + 2, shadow);
      drawMixedText(renderer, line, 188, y, color);
      y += 32;
    }
    SDL_SetRenderClipRect(&renderer, clipped ? &previousClip : nullptr);
  }

public:
  AirTextBox(Kotonoha::Engine &engine,
             Kotonoha::Providers::LucaSystemAssets &assets)
      : VNTextBox(engine, "", "", 30), assets_(assets),
        frames_{{tryReadCz(assets, "MWIN0"), tryReadCz(assets, "MWIN1"),
                 tryReadCz(assets, "MWIN2"), tryReadCz(assets, "MWIN3"),
                 tryReadCz(assets, "MWIN4")}},
        fallbackStrip_(tryReadCz(assets, "MWIN1280_0")),
        cursor_(tryReadCz(assets, "MWIN_CURSOR")) {
    try {
      originalAtlasAvailable_ = assets.contains(originalAtlasId_);
      if (originalAtlasAvailable_ && assets.contains("info27"))
        originalGlyphs_.emplace(assets.read("info27"));
    } catch (const std::exception &error) {
      SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                  "LucaSystem: original glyph fallback unavailable: %s",
                  error.what());
    }
  }
};

class AirBacklog final : public Kotonoha::VNBacklog {
  Kotonoha::Providers::LucaSystemAssets &assets_;

  void renderFrame(Kotonoha::RendererRef renderer) override {
    try {
      if (assets_.contains("backlog_texture")) {
        const SDL_FRect screen{0, 0, 1280, 720};
        auto &background = assets_.texture(renderer, "backlog_texture");
        Kotonoha::RenderUpscaledTexture(&renderer, &background, nullptr,
                                        &screen);
      }
    } catch (const std::exception &) {
    }
    VNBacklog::renderFrame(renderer);
  }

public:
  AirBacklog(Kotonoha::Engine &engine,
             Kotonoha::Providers::LucaSystemAssets &assets)
      : VNBacklog(engine), assets_(assets) {}
};

class AirSettingsSkin final : public Kotonoha::SettingsMenu::Skin {
  using Page = Kotonoha::SettingsMenu::Page;
  std::shared_ptr<Kotonoha::Providers::LucaSystemAssets> assets_;
  static constexpr SDL_Color ink_{22, 53, 108, 255};

  bool draw(Kotonoha::RendererRef renderer, const char *id, SDL_FRect dst,
            const SDL_FRect *source = nullptr) {
    try {
      if (assets_->contains(id)) {
        auto &texture = assets_->texture(renderer, id);
        return Kotonoha::RenderUpscaledTexture(&renderer, &texture, source,
                                               &dst);
      }
    } catch (const std::exception &) {
    }
    return false;
  }

public:
  explicit AirSettingsSkin(
      std::shared_ptr<Kotonoha::Providers::LucaSystemAssets> assets)
      : assets_(std::move(assets)) {}

  const std::vector<Page> &pages() const override {
    static const std::vector<Page> result{
        Page::Basic, Page::TextPacing, Page::TextColors,
        Page::Sound, Page::Voice,      Page::Keyboard,
        Page::Mouse, Page::Touch,      Page::System};
    return result;
  }

  std::vector<std::string> audioChannels() const override {
    return {"BGM", "Se", "Voice"};
  }

  SDL_FRect tabArea(int index) const override {
    // Boundaries between the English Steam atlas labels.
    static constexpr float edges[]{251, 356, 445,  550,  663,
                                   766, 917, 1032, 1145, 1280};
    return {edges[index], 0, edges[index + 1] - edges[index], 60};
  }

  void renderFrame(Kotonoha::RendererRef renderer, Kotonoha::Engine &engine,
                   int tab, const Kotonoha::EngineSettings &draft) override {
    const SDL_FRect screen{0, 0, 1280, 720};
    auto &ui = engine.GetUI();
    if (!draw(renderer, "options_bg", screen)) {
      SDL_SetRenderDrawColor(&renderer, 246, 249, 253, 255);
      SDL_RenderFillRect(&renderer, &screen);
    }
    if (draw(renderer, "options_tab_en", {0, 0, 1280, 60})) {
      const auto active = tabArea(tab);
      if (!draw(renderer, "options_tab2_en", active, &active)) {
        const SDL_FRect underline{active.x + 8, 56, active.w - 16, 3};
        ui.panel(renderer, underline, ink_);
      }
    } else {
      static constexpr const char *names[]{"Basic", "Text1", "Text2",
                                           "Sound", "Voice", "Keyboard",
                                           "Mouse", "Touch", "System"};
      ui.text(renderer, "OPTIONS", 38, 10, 195, ink_);
      for (int i = 0; i < int(pages().size()); ++i) {
        auto rect = tabArea(i);
        rect.y = 4;
        rect.h = 50;
        ui.panel(renderer, rect,
                 i == tab ? SDL_Color{192, 222, 249, 255}
                          : SDL_Color{230, 240, 249, 255});
        ui.text(renderer, names[i], rect.x + 4, 18, rect.w - 8, ink_);
      }
    }
    const auto help = [&](std::initializer_list<const char *> lines) {
      float y = 105;
      for (auto *line : lines) {
        ui.text(renderer, line, 108, y, 1060, ink_);
        y += 52;
      }
    };
    const auto kind = pages()[tab];
    if (kind == Page::TextPacing || kind == Page::TextColors) {
      draw(renderer, "OPTIONS_PV_BG_win32_1280", {96, 505, 1088, 93});
      draw(renderer, "options_pv_win", {72, 490, 1136, 120});
      ui.text(renderer, "Dialogue preview", 146, 526, 985,
              draft.vnTextColorOverride ? draft.vnSpeakerColor
                                        : SDL_Color{255, 238, 175, 255});
      ui.text(renderer, "The breeze carries the sound of summer.", 146, 558,
              985,
              draft.vnTextColorOverride ? draft.vnTextColor
                                        : SDL_Color{255, 255, 255, 255});
    } else if (kind == Page::Keyboard) {
      help({"Enter / Space — reveal or advance dialogue",
            "Escape — open the system menu or go back",
            "B / Page Up — dialogue history",
            "F5 / F6 — quick save / quick load", "H — hide the dialogue box"});
    } else if (kind == Page::Mouse) {
      help({"Left click — select an option or advance dialogue",
            "Right click — open the system menu or go back",
            "Middle click — hide the dialogue box"});
    } else if (kind == Page::Touch) {
      help({"Tap to select or advance dialogue",
            "Use the on-screen menu button to open the system menu"});
    } else if (kind == Page::Sound || kind == Page::Voice) {
      ui.text(renderer, "Volume changes apply immediately", 104, 573, 1000,
              ink_);
    }
  }

  bool renderSlider(Kotonoha::RendererRef renderer, SDL_FRect bar,
                    float value) override {
    SDL_SetRenderDrawColor(&renderer, 197, 218, 237, 255);
    SDL_RenderFillRect(&renderer, &bar);
    const SDL_FRect fill{bar.x, bar.y, bar.w * value, bar.h};
    SDL_SetRenderDrawColor(&renderer, 95, 151, 211, 255);
    SDL_RenderFillRect(&renderer, &fill);
    const SDL_FRect source{78, 2, 72, 72};
    const SDL_FRect knob{bar.x + bar.w * value - 15, bar.y - 7, 30, 30};
    if (!draw(renderer, "options_parts", knob, &source)) {
      SDL_SetRenderDrawColor(&renderer, 52, 112, 187, 255);
      SDL_RenderFillRect(&renderer, &knob);
    }
    return true;
  }
};

class AirChoiceScene final : public Kotonoha::ChoiceQuestionScene {
  Kotonoha::Providers::LucaSystemAssets &assets_;
  bool highlighted_ = false;
  static constexpr float rowHeight = 74;
  static constexpr float rowStep = 100;

  bool storyChoice() const { return questionText.empty(); }

  SDL_FRect listArea() const override {
    return storyChoice() ? SDL_FRect{0, 0, 1280, 720}
                         : ChoiceQuestionScene::listArea();
  }

  void layout() override {
    if (!storyChoice()) {
      ChoiceQuestionScene::layout();
      return;
    }
    auto &ui = engine.GetVNUI();
    const float height = (options.size() - 1) * rowStep + rowHeight;
    float y = std::max(0.f, (720 - height) * .5f) - scroll;
    for (auto &option : options) {
      const float width = ui.textWidth(option.label, 26) <= 600 ? 652 : 1280;
      option.hitBox = {(1280 - width) * .5f, y, width, rowHeight};
      y += rowStep;
    }
  }

  void renderPanel(Kotonoha::RendererRef renderer) override {
    if (!storyChoice())
      ChoiceQuestionScene::renderPanel(renderer);
  }

  void renderOption(Kotonoha::RendererRef renderer, std::size_t index,
                    bool focused) override {
    if (!storyChoice()) {
      ChoiceQuestionScene::renderOption(renderer, index, focused);
      return;
    }
    const auto &option = options[index];
    const auto &rect = option.hitBox;
    const bool compact = rect.w < 1280;
    const SDL_FRect src{compact ? 314.f : 0.f,
                        focused && highlighted_ ? 148.f : 0.f, rect.w,
                        rowHeight};
    if (!drawAirArt(assets_, renderer, compact ? "SELWIN_s" : "SELWIN", rect,
                    &src)) {
      ChoiceQuestionScene::renderOption(renderer, index, focused);
      return;
    }
    auto &ui = engine.GetVNUI();
    const float width = std::min(rect.w - 48, ui.textWidth(option.label, 26));
    const float x = rect.x + (rect.w - width) * .5f;
    const float y = rect.y + 22;
    ui.textSized(renderer, option.label, x + 1, y + 2, rect.w - 48, 26,
                 {12, 20, 39, 255});
    ui.textSized(renderer, option.label, x, y, rect.w - 48, 26,
                 {220, 234, 250, 255});
  }

public:
  AirChoiceScene(Kotonoha::Engine &engine,
                 Kotonoha::Providers::LucaSystemAssets &assets)
      : ChoiceQuestionScene(engine, {}, {}, nullptr), assets_(assets) {}

  void
  setQuestion(const std::string &question,
              const std::vector<Kotonoha::ChoiceOption> &choices,
              std::function<void(int, const std::string &)> callback) override {
    highlighted_ = false;
    ChoiceQuestionScene::setQuestion(question, choices, std::move(callback));
  }

  bool onEvent(Kotonoha::EventRef raw) override {
    if (active && storyChoice()) {
      const auto event = Kotonoha::MapControllerEvent(raw);
      if (event.type == SDL_EVENT_MOUSE_MOTION) {
        layout();
        highlighted_ = false;
        const SDL_FPoint point{event.motion.x, event.motion.y};
        for (std::size_t i = 0; i < options.size(); ++i)
          if (SDL_PointInRectFloat(&point, &options[i].hitBox)) {
            selected = i;
            highlighted_ = true;
            break;
          }
      } else if (event.type == SDL_EVENT_KEY_DOWN &&
                 (event.key.key == SDLK_UP || event.key.key == SDLK_DOWN)) {
        highlighted_ = true;
      }
    }
    return ChoiceQuestionScene::onEvent(raw);
  }
};

class AirSaveMenu final : public Kotonoha::VNSaveMenu {
  Kotonoha::Providers::LucaSystemAssets &assets_;
  std::map<std::string, Kotonoha::TexturePtr> thumbnails_;

  const SlotGrid *slotGrid() const override {
    static constexpr SlotGrid grid{5,
                                   4,
                                   {64, 82, 224, 127},
                                   232,
                                   134,
                                   {48, 650, 154, 46},
                                   {470, 650, 62, 46},
                                   {748, 650, 62, 46},
                                   {872, 650, 166, 46},
                                   {1050, 650, 182, 46}};
    return &grid;
  }

  bool draw(Kotonoha::RendererRef renderer, const char *id, SDL_FRect dst,
            const SDL_FRect *source = nullptr) {
    try {
      if (assets_.contains(id)) {
        auto &texture = assets_.texture(renderer, id);
        return Kotonoha::RenderUpscaledTexture(&renderer, &texture, source,
                                               &dst);
      }
    } catch (const std::exception &) {
    }
    return false;
  }

  bool drawThumbnail(Kotonoha::RendererRef renderer, std::size_t index,
                     const SDL_FRect &rect) {
    const auto path = slotPath(index);
    if (path.empty())
      return false;
    using Kotonoha::VNSave;
    const auto [it, inserted] = thumbnails_.try_emplace(path);
    if (inserted) {
      try {
        auto save = VNSave::read(path);
        if (!save.thumbnailRgb.empty()) {
          Kotonoha::SurfacePtr image{SDL_CreateSurfaceFrom(
              VNSave::thumbnailWidth, VNSave::thumbnailHeight,
              SDL_PIXELFORMAT_RGB24, save.thumbnailRgb.data(),
              VNSave::thumbnailWidth * 3)};
          if (image)
            it->second.reset(
                SDL_CreateTextureFromSurface(&renderer, image.get()));
        }
      } catch (const std::exception &) {
        // Older or unreadable saves retain the text-only card.
      }
    }
    const SDL_FRect preview{rect.x + 32, rect.y + 7, 160, 90};
    return it->second && Kotonoha::RenderUpscaledTexture(
                             &renderer, it->second.get(), nullptr, &preview);
  }

public:
  AirSaveMenu(Kotonoha::Engine &engine,
              Kotonoha::Providers::LucaSystemAssets &assets)
      : VNSaveMenu(engine), assets_(assets) {}

  void open(const std::string &path, bool load,
            const std::string &suggestedName) {
    thumbnails_.clear();
    VNSaveMenu::open(path, load, suggestedName);
  }

  void render(Kotonoha::RendererRef renderer, float) override {
    if (!isOpen())
      return;
    auto &ui = engine.GetUI();
    const auto &grid = *slotGrid();
    const bool load = loadingSlots();
    const SDL_FRect screen{0, 0, 1280, 720};
    const SDL_Color ink{16, 54, 89, 255};
    if (!draw(renderer, load ? "load_bg" : "save_bg", screen)) {
      SDL_SetRenderDrawColor(&renderer, 211, 229, 245, 255);
      SDL_RenderFillRect(&renderer, &screen);
    }
    draw(renderer, load ? "load_bg_text_1280" : "save_bg_text_1280", screen);
    ui.text(renderer, load ? "Load game" : "Save game", 64, 34, 900, ink);
    SDL_Rect previous{};
    const bool hadClip = SDL_RenderClipEnabled(&renderer) &&
                         SDL_GetRenderClipRect(&renderer, &previous);
    for (std::size_t i = 0; i < grid.capacity(); ++i) {
      const auto rect = grid.card(i);
      const auto index = firstSlot() + i;
      const bool present = index < count();
      const bool focused = present && index == focusedSlot();
      if (!draw(renderer,
                load ? (focused ? "load_blank_ov" : "load_blank")
                     : (focused ? "save_blank_ov" : "save_blank"),
                rect))
        ui.panel(renderer, rect,
                 focused ? SDL_Color{248, 252, 255, 255}
                         : SDL_Color{187, 211, 233, 255});
      if (load && focused)
        draw(renderer, "LOAD_CURSOR_1280", {rect.x - 8, rect.y - 5, 240, 137});
      if (!present)
        continue;
      SDL_Rect clip{int(rect.x + 8), int(rect.y + 6), int(rect.w - 16),
                    int(rect.h - 12)};
      if (hadClip) {
        SDL_Rect intersection{};
        if (!SDL_GetRectIntersection(&clip, &previous, &intersection))
          continue;
        clip = intersection;
      }
      SDL_SetRenderClipRect(&renderer, &clip);
      const bool newSave = !load && index == 0;
      const auto number = load ? index + 1 : index;
      const bool preview = drawThumbnail(renderer, index, rect);
      if (!newSave)
        ui.text(renderer, std::to_string(number), rect.x + 12, rect.y + 10,
                rect.w - 24, ink);
      ui.text(renderer, newSave ? "New save" : slotLabel(index), rect.x + 12,
              rect.y + (preview ? 98 : 50), rect.w - 24, ink);
      SDL_SetRenderClipRect(&renderer, hadClip ? &previous : nullptr);
    }

    ui.button(renderer, grid.back, "Back [ESC]");
    const auto arrow = [&](SDL_FRect button, bool right) {
      const SDL_FRect source{right ? 40.0f : 0.0f, 0, 40, 40};
      const SDL_FRect dst{button.x + (button.w - 40) * .5f, button.y + 3, 40,
                          40};
      if (!draw(renderer, load ? "load_pt" : "save_pt", dst, &source))
        ui.button(renderer, button, right ? "Next" : "Prev");
    };
    arrow(grid.previous, false);
    arrow(grid.next, true);
    const auto pages = std::max<std::size_t>(
        1, (count() + grid.capacity() - 1) / grid.capacity());
    ui.text(renderer,
            std::to_string(firstSlot() / grid.capacity() + 1) + " / " +
                std::to_string(pages),
            580, 661, 130, ink);
    if (currentConfirmation() != Confirmation::None) {
      ui.panel(renderer, {320, 275, 640, 155});
      ui.text(renderer, confirmationPrompt(), 347, 320, 578,
              {240, 242, 255, 255});
      ui.button(renderer, grid.primary,
                currentConfirmation() == Confirmation::Delete ? "Delete"
                                                              : "Overwrite");
      ui.button(renderer, grid.secondary, "Cancel");
    } else if (hasSelectedSave()) {
      ui.button(renderer, grid.primary, "Rename [F2]");
      ui.button(renderer, grid.secondary, "Delete");
    } else if (load && !count()) {
      ui.text(renderer, "No saves found.", 64, 625, 400, ink);
    }
    if (!errorMessage().empty())
      ui.text(renderer, errorMessage(), 64, 624, 1140, {148, 30, 30, 255});
  }
};

class AirGallery final : public Kotonoha::Scene {
  static constexpr int pageSize_ = 12;
  Kotonoha::Providers::LucaSystemAssets &assets_;
  const AirGalleryCatalog &catalog_;
  std::function<bool(uint32_t)> unlocked_;
  Kotonoha::TouchActivation touch_;
  bool open_ = false;
  bool grid_ = false;
  int category_ = 0;
  int slot_ = 0;
  std::vector<uint16_t> viewer_;
  size_t variant_ = 0;

  static SDL_FRect categoryRect(int i) {
    return {40.0f + i * 300, 200, 300, 305};
  }
  static SDL_FRect slotRect(int i) {
    const int index = i % pageSize_;
    return {64.0f + (index % 4) * 288, 106.0f + (index / 4) * 172, 280, 152};
  }
  static constexpr SDL_FRect back_{32, 665, 168, 42};
  static constexpr SDL_FRect previous_{450, 665, 170, 42};
  static constexpr SDL_FRect next_{660, 665, 170, 42};

  bool draw(Kotonoha::RendererRef renderer, const std::string &id,
            SDL_FRect dst, const SDL_FRect *source = nullptr,
            bool fit = false) {
    try {
      if (!assets_.contains(id))
        return false;
      auto &texture = assets_.texture(renderer, id);
      if (fit) {
        float w = 0, h = 0;
        if (!SDL_GetTextureSize(&texture, &w, &h) || w <= 0 || h <= 0)
          return false;
        const float ratio = std::min(dst.w / w, dst.h / h);
        dst.x += (dst.w - w * ratio) * .5f;
        dst.y += (dst.h - h * ratio) * .5f;
        dst.w = w * ratio;
        dst.h = h * ratio;
      }
      return Kotonoha::RenderUpscaledTexture(&renderer, &texture, source, &dst);
    } catch (const std::exception &) {
      return false;
    }
  }
  bool drawImage(Kotonoha::RendererRef renderer, uint16_t id, SDL_FRect dst) {
    return draw(renderer, "image:" + std::to_string(id), dst, nullptr, true);
  }
  const auto &slots() const { return catalog_[category_]; }
  void moveSelection(int delta) {
    if (!viewer_.empty())
      variant_ =
          size_t(std::clamp(int(variant_) + delta, 0, int(viewer_.size()) - 1));
    else if (grid_)
      slot_ = std::clamp(slot_ + delta, 0, int(slots().size()) - 1);
    else
      category_ = std::clamp(category_ + delta, 0, int(catalog_.size()) - 1);
  }
  void activate() {
    if (!viewer_.empty()) {
      moveSelection(1);
    } else if (!grid_) {
      grid_ = true;
      slot_ = 0;
    } else {
      for (const auto id : slots()[slot_].variants)
        if (unlocked_(id))
          viewer_.push_back(id);
      variant_ = 0;
    }
  }
  void back() {
    if (!viewer_.empty())
      viewer_.clear();
    else if (grid_)
      grid_ = false;
    else
      close();
  }

public:
  AirGallery(Kotonoha::Engine &engine,
             Kotonoha::Providers::LucaSystemAssets &assets,
             const AirGalleryCatalog &catalog,
             std::function<bool(uint32_t)> unlocked)
      : Scene(engine), assets_(assets), catalog_(catalog),
        unlocked_(std::move(unlocked)) {}

  void open() {
    touch_.reset();
    open_ = !catalog_.empty();
    grid_ = false;
    category_ = slot_ = 0;
    viewer_.clear();
  }
  void close() { open_ = false; }
  bool designSpace() const override { return true; }
  Kotonoha::RenderLayer renderLayer() const override {
    return Kotonoha::RenderLayer::Overlay;
  }
  bool capturesInput() const override { return open_; }
  bool pausesStory() const override { return open_; }
  uint16_t viewedImage() const {
    return viewer_.empty() ? 0 : viewer_[variant_];
  }

  bool onEvent(Kotonoha::EventRef raw) override {
    if (!open_)
      return false;
    const auto event = Kotonoha::MapControllerEvent(raw);
    if (!touch_.accept(event))
      return true;
    if (Kotonoha::BackAction(engine, event) ||
        Kotonoha::UI::clicked(event, back_)) {
      back();
      return true;
    }
    if (event.type == SDL_EVENT_KEY_DOWN && !event.key.repeat) {
      switch (event.key.key) {
      case SDLK_LEFT:
        moveSelection(-1);
        return true;
      case SDLK_RIGHT:
        moveSelection(1);
        return true;
      case SDLK_UP:
        moveSelection(grid_ && viewer_.empty() ? -4 : -1);
        return true;
      case SDLK_DOWN:
        moveSelection(grid_ && viewer_.empty() ? 4 : 1);
        return true;
      case SDLK_PAGEUP:
        moveSelection(-pageSize_);
        return true;
      case SDLK_PAGEDOWN:
        moveSelection(pageSize_);
        return true;
      case SDLK_RETURN:
      case SDLK_SPACE:
        activate();
        return true;
      }
    }
    if (grid_ && Kotonoha::UI::clicked(event, previous_)) {
      moveSelection(viewer_.empty() ? -pageSize_ : -1);
    } else if (grid_ && Kotonoha::UI::clicked(event, next_)) {
      moveSelection(viewer_.empty() ? pageSize_ : 1);
    } else if (!grid_) {
      for (int i = 0; i < int(catalog_.size()); ++i) {
        if (Kotonoha::UI::clicked(event, categoryRect(i))) {
          category_ = i;
          activate();
          break;
        }
      }
    } else if (viewer_.empty()) {
      const int first = slot_ / pageSize_ * pageSize_;
      for (int i = first; i < std::min(first + pageSize_, int(slots().size()));
           ++i) {
        if (Kotonoha::UI::clicked(event, slotRect(i))) {
          slot_ = i;
          activate();
          break;
        }
      }
    }
    return true;
  }

  void render(Kotonoha::RendererRef renderer, float) override {
    if (!open_)
      return;
    auto &ui = engine.GetUI();
    const SDL_FRect screen{0, 0, 1280, 720};
    const SDL_Color ink{18, 40, 89, 255};
    SDL_SetRenderDrawColor(&renderer, 225, 235, 249, 255);
    SDL_RenderFillRect(&renderer, &screen);
    if (!viewer_.empty()) {
      SDL_SetRenderDrawColor(&renderer, 8, 14, 28, 255);
      SDL_RenderFillRect(&renderer, &screen);
      drawImage(renderer, viewedImage(), {0, 0, 1280, 655});
      ui.button(renderer, previous_, "Previous CG");
      ui.button(renderer, next_, "Next CG");
    } else {
      draw(renderer, "CGM_BG" + std::to_string(grid_ ? category_ + 1 : 0),
           screen);
      static const char *names[]{"Misuzu Kamio", "Minagi Tohno",
                                 "Kano Kirishima", "Kanna (Past)"};
      if (!grid_) {
        ui.text(renderer, "CG GALLERY", 64, 54, 700, ink);
        for (int i = 0; i < int(catalog_.size()); ++i) {
          const SDL_FRect source{i * 300.0f, i == category_ ? 305.0f : 0.0f,
                                 300, 305};
          if (!draw(renderer, "CGM_PT0_EN", categoryRect(i), &source))
            ui.button(renderer, categoryRect(i), names[i], i == category_);
        }
      } else {
        const auto heading =
            std::string(names[category_]) + "  ·  " +
            std::to_string(slot_ / pageSize_ + 1) + " / " +
            std::to_string((slots().size() + pageSize_ - 1) / pageSize_);
        ui.text(renderer, heading, 64, 54, 700, ink);
        const int first = slot_ / pageSize_ * pageSize_;
        for (int i = first;
             i < std::min(first + pageSize_, int(slots().size())); ++i) {
          const auto dst = slotRect(i);
          const auto &variants = slots()[i].variants;
          const auto available =
              std::find_if(variants.begin(), variants.end(), unlocked_);
          if (available == variants.end()) {
            ui.panel(renderer, dst);
            ui.text(renderer, "Locked", dst.x + 20, dst.y + 60, 220,
                    {225, 230, 245, 255});
          } else {
            // Only use the original thumbnail once all its possible imagery
            // is unlocked. For partial groups, use a known unlocked CG.
            char name[32];
            std::snprintf(name, sizeof(name), "CGGALLERY_CH%02d_%03d",
                          category_ + 1, i + 1);
            if (!std::all_of(variants.begin(), variants.end(), unlocked_) ||
                !draw(renderer, name, dst))
              drawImage(renderer, *available, dst);
          }
          draw(renderer, i == slot_ ? "cg_cursor_on" : "cg_cursor",
               {dst.x - 4, dst.y - 5, 288, 162});
        }
        ui.button(renderer, previous_, "Previous Page");
        ui.button(renderer, next_, "Next Page");
      }
    }
    ui.button(renderer, back_, "Back [ESC]");
  }
};

class AirPauseMenu final : public Kotonoha::InGameMenu {
  Kotonoha::Providers::LucaSystemAssets &assets_;
  std::function<void(bool)> quick_;
  int manualPage_ = -1;
  bool manualFromTitle_ = false;
  std::vector<std::string> manualPages_;

  SDL_Texture *asset(Kotonoha::RendererRef renderer, const std::string &id) {
    try {
      if (assets_.contains(id))
        return &assets_.texture(renderer, id);
    } catch (const std::exception &) {
    }
    return nullptr;
  }

  void closeManual() {
    manualPage_ = -1;
    if (manualFromTitle_)
      close();
    manualFromTitle_ = false;
  }

protected:
  int itemCount() const override { return 8; }
  std::string itemLabel(int index) const override {
    static const char *labels[]{"Save",       "Load",       "Options", "Title",
                                "Quick Save", "Quick Load", "Manual",  "Exit"};
    return labels[index];
  }
  SDL_FRect itemRect(int index) const override {
    return {180.0f + (index % 4) * 230.0f, 160.0f + (index / 4) * 200.0f, 230,
            200};
  }
  void renderPanel(Kotonoha::RendererRef renderer) override {
    const SDL_FRect screen{0, 0, 1280, 720};
    if (auto *background = asset(renderer, "system_menu_bg"))
      Kotonoha::RenderUpscaledTexture(&renderer, background, nullptr, &screen);
    else
      engine.GetUI().backdrop(renderer, {16, 24, 48, 210});
  }
  void renderItem(Kotonoha::RendererRef renderer, int index,
                  bool focused) override {
    if (auto *atlas = asset(renderer, "systemmenu")) {
      const SDL_FRect src{index * 230.0f, focused ? 200.0f : 0.0f, 230, 200};
      const auto dst = itemRect(index);
      Kotonoha::RenderUpscaledTexture(&renderer, atlas, &src, &dst);
    } else {
      const auto area = itemRect(index);
      engine.GetUI().button(renderer,
                            {area.x + 12, area.y + 65, area.w - 24, 60},
                            itemLabel(index), focused);
    }
  }
  void activate(int index) override {
    switch (index) {
    case 0:
    case 1:
      InGameMenu::activate(index + 3); // shared save/load callbacks
      break;
    case 2:
      InGameMenu::activate(2); // native Kotonoha settings
      break;
    case 3:
      InGameMenu::activate(5); // title
      break;
    case 4:
    case 5:
      if (quick_)
        quick_(index == 5);
      break;
    case 6:
      openManual(false);
      break;
    case 7: {
      SDL_Event quit{};
      quit.type = SDL_EVENT_QUIT;
      SDL_PushEvent(&quit);
      break;
    }
    }
  }

public:
  AirPauseMenu(Kotonoha::Engine &engine,
               Kotonoha::Providers::LucaSystemAssets &assets)
      : InGameMenu(engine), assets_(assets) {}

  void setQuickAction(std::function<void(bool)> quick) {
    quick_ = std::move(quick);
  }
  void open() override {
    selected = 0;
    InGameMenu::open();
  }
  void close() override {
    manualPage_ = -1;
    manualFromTitle_ = false;
    InGameMenu::close();
  }
  void openManual(bool fromTitle) {
    if (!IsOpen())
      open();
    manualPages_ = assets_.list("en_help");
    std::sort(manualPages_.begin(), manualPages_.end());
    manualPage_ = 0;
    manualFromTitle_ = fromTitle;
  }
  bool onEvent(Kotonoha::EventRef rawEvent) override {
    if (!IsOpen())
      return false;
    const auto event = Kotonoha::MapControllerEvent(rawEvent);
    if (!touchActivation.accept(event))
      return true;
    if (Kotonoha::BackAction(engine, event)) {
      if (manualPage_ >= 0)
        closeManual();
      else
        close();
      return true;
    }
    const bool key = event.type == SDL_EVENT_KEY_DOWN && !event.key.repeat;
    if (manualPage_ >= 0) {
      if ((key && event.key.key == SDLK_LEFT) ||
          Kotonoha::UI::clicked(event, {30, 665, 150, 42}))
        manualPage_ = std::max(0, manualPage_ - 1);
      if ((key && event.key.key == SDLK_RIGHT) ||
          Kotonoha::UI::clicked(event, {1100, 665, 150, 42}))
        manualPage_ = std::min(std::max(0, int(manualPages_.size()) - 1),
                               manualPage_ + 1);
      if (Kotonoha::UI::clicked(event, {565, 665, 150, 42}))
        closeManual();
      return true;
    }
    if (key) {
      switch (event.key.key) {
      case SDLK_LEFT:
        selected = (selected + 7) % 8;
        return true;
      case SDLK_RIGHT:
        selected = (selected + 1) % 8;
        return true;
      case SDLK_UP:
      case SDLK_DOWN:
        selected = (selected + 4) % 8;
        return true;
      case SDLK_RETURN:
      case SDLK_SPACE:
        activate(selected);
        return true;
      case SDLK_B:
      case SDLK_PAGEUP:
        close();
        openBacklog();
        return true;
      }
    }
    for (int i = 0; i < itemCount(); ++i) {
      const auto rect = itemRect(i);
      if (event.type == SDL_EVENT_MOUSE_MOTION) {
        const SDL_FPoint cursor{event.motion.x, event.motion.y};
        if (SDL_PointInRectFloat(&cursor, &rect))
          selected = i;
      }
      if (Kotonoha::UI::clicked(event, rect)) {
        activate(i);
        return true;
      }
    }
    return true;
  }
  void render(Kotonoha::RendererRef renderer, float scale) override {
    if (manualPage_ < 0) {
      InGameMenu::render(renderer, scale);
      return;
    }
    auto &ui = engine.GetUI();
    ui.backdrop(renderer, {12, 21, 44, 244});
    if (manualPage_ < int(manualPages_.size())) {
      if (auto *page = asset(renderer, manualPages_[manualPage_])) {
        float w = 0, h = 0;
        if (SDL_GetTextureSize(page, &w, &h) && w > 0 && h > 0) {
          const float fit = std::min(1200.0f / w, 640.0f / h);
          const SDL_FRect dst{(1280 - w * fit) * .5f, 10, w * fit, h * fit};
          Kotonoha::RenderUpscaledTexture(&renderer, page, nullptr, &dst);
        }
      }
    } else {
      ui.text(renderer, "Manual pages are unavailable", 445, 315, 400,
              {255, 255, 255, 255});
    }
    ui.button(renderer, {30, 665, 150, 42}, "Previous");
    ui.button(renderer, {565, 665, 150, 42}, "Close");
    ui.button(renderer, {1100, 665, 150, 42}, "Next");
  }
};

// Recursive-descent C-style expression evaluator for IFN / IFY / EQU
class ExprParser {
  std::string_view src_;
  size_t pos_ = 0;
  const std::unordered_map<uint32_t, int32_t> &vars_;
  const std::unordered_map<uint32_t, bool> &flags_;

  void skipWs() {
    while (pos_ < src_.size() &&
           std::isspace(static_cast<unsigned char>(src_[pos_])))
      ++pos_;
  }
  bool match(std::string_view tok) {
    skipWs();
    if (src_.substr(pos_, tok.size()) == tok) {
      pos_ += tok.size();
      return true;
    }
    return false;
  }

  int32_t lookupVar(uint32_t id) const {
    auto it = vars_.find(id);
    if (it != vars_.end())
      return it->second;
    auto fit = flags_.find(id);
    if (fit != flags_.end())
      return fit->second ? 1 : 0;
    return 0;
  }

  int32_t parsePrimary() {
    skipWs();
    if (pos_ >= src_.size())
      return 0;
    if (match("(")) {
      int32_t v = parseOr();
      match(")");
      return v;
    }
    if (src_[pos_] == '!' &&
        (pos_ + 1 >= src_.size() || src_[pos_ + 1] != '=')) {
      ++pos_;
      return parsePrimary() == 0 ? 1 : 0;
    }
    if (src_[pos_] == '-' && (pos_ + 1 < src_.size())) {
      ++pos_;
      return -parsePrimary();
    }
    if (src_[pos_] == '+') {
      ++pos_;
      return parsePrimary();
    }
    if (src_[pos_] == '#') {
      ++pos_;
      uint32_t id = 0;
      while (pos_ < src_.size() &&
             std::isdigit(static_cast<unsigned char>(src_[pos_]))) {
        id = id * 10u + uint32_t(src_[pos_++] - '0');
      }
      return lookupVar(id);
    }
    if (std::isalpha(static_cast<unsigned char>(src_[pos_]))) {
      const size_t start = pos_;
      while (pos_ < src_.size() &&
             (std::isalnum(static_cast<unsigned char>(src_[pos_])) ||
              src_[pos_] == '_'))
        ++pos_;
      const std::string_view ident = src_.substr(start, pos_ - start);
      if (match("[")) {
        int32_t idx = parseOr();
        match("]");
        if (ident == "FLAG" || ident == "FLG" || ident == "ARFLAG") {
          auto fit = flags_.find(static_cast<uint32_t>(idx));
          return fit != flags_.end() ? (fit->second ? 1 : 0) : 0;
        }
        return lookupVar(static_cast<uint32_t>(idx));
      }
      return 0;
    }
    if (std::isdigit(static_cast<unsigned char>(src_[pos_]))) {
      int64_t val = 0;
      while (pos_ < src_.size() &&
             std::isdigit(static_cast<unsigned char>(src_[pos_]))) {
        val = val * 10 + (src_[pos_++] - '0');
      }
      return static_cast<int32_t>(val);
    }
    ++pos_;
    return 0;
  }

  int32_t parseMul() {
    int32_t left = parsePrimary();
    while (true) {
      skipWs();
      if (match("*"))
        left *= parsePrimary();
      else if (match("/")) {
        int32_t r = parsePrimary();
        left = (r != 0) ? (left / r) : 0;
      } else if (match("%")) {
        int32_t r = parsePrimary();
        left = (r != 0) ? (left % r) : 0;
      } else
        break;
    }
    return left;
  }

  int32_t parseAdd() {
    int32_t left = parseMul();
    while (true) {
      skipWs();
      if (match("+"))
        left += parseMul();
      else if (match("-"))
        left -= parseMul();
      else
        break;
    }
    return left;
  }

  int32_t parseRel() {
    int32_t left = parseAdd();
    while (true) {
      if (match("<="))
        left = (left <= parseAdd()) ? 1 : 0;
      else if (match(">="))
        left = (left >= parseAdd()) ? 1 : 0;
      else if (match("<"))
        left = (left < parseAdd()) ? 1 : 0;
      else if (match(">"))
        left = (left > parseAdd()) ? 1 : 0;
      else
        break;
    }
    return left;
  }

  int32_t parseEq() {
    int32_t left = parseRel();
    while (true) {
      if (match("=="))
        left = (left == parseRel()) ? 1 : 0;
      else if (match("!="))
        left = (left != parseRel()) ? 1 : 0;
      else
        break;
    }
    return left;
  }

  int32_t parseAnd() {
    int32_t left = parseEq();
    while (match("&&") || match("&")) {
      int32_t r = parseEq();
      left = (left != 0 && r != 0) ? 1 : 0;
    }
    return left;
  }

  int32_t parseOr() {
    int32_t left = parseAnd();
    while (match("||") || match("|")) {
      int32_t r = parseAnd();
      left = (left != 0 || r != 0) ? 1 : 0;
    }
    return left;
  }

public:
  ExprParser(std::string_view src,
             const std::unordered_map<uint32_t, int32_t> &vars,
             const std::unordered_map<uint32_t, bool> &flags)
      : src_(src), vars_(vars), flags_(flags) {}

  int32_t eval() { return parseOr(); }
};

struct SlottedLayer {
  uint8_t group = 0;
  uint8_t slot = 0;
  uint32_t assetId = 0;
  float posX = 0.0f;
  float posY = 0.0f;
  float startOffsetX = 0.0f;
  float startOffsetY = 0.0f;
  float targetOffsetX = 0.0f;
  float targetOffsetY = 0.0f;
  uint64_t moveStart = 0;
  uint64_t moveDuration = 0;
  float alpha = 1.0f;
  float scaleX = 1.0f;
  float scaleY = 1.0f;
  float rotation = 0.0f;
  uint8_t r = 255, g = 255, b = 255;
  bool visible = true;

  bool sameVisual(const SlottedLayer &o) const {
    return group == o.group && slot == o.slot && assetId == o.assetId &&
           std::fabs(posX - o.posX) < 0.5f && std::fabs(posY - o.posY) < 0.5f &&
           std::fabs(alpha - o.alpha) < 0.01f && visible == o.visible;
  }
};

struct ShakeKeyframe {
  int16_t frames = 0;
  int16_t dx = 0;
  int16_t dy = 0;
};

} // namespace

struct LucaSystemRuntime::Impl {
  LucaSystemRuntime &runtime;
  std::shared_ptr<Kotonoha::Providers::LucaSystemAssets> assets;
  Kotonoha::Engine *engine = nullptr;

  std::unique_ptr<AirTextBox> textbox;
  Kotonoha::ChoiceQuestionScene *choiceScene = nullptr;
  Kotonoha::VNBacklog *backlog = nullptr;
  AirPauseMenu *menu = nullptr;
  std::unique_ptr<AirSaveMenu> saveMenu;
  AirGalleryCatalog galleryCatalog;
  std::unordered_set<uint32_t> galleryIds;
  std::unique_ptr<AirGallery> gallery;

  bool running = false;
  bool titleShown = false;
  bool originalTitle = true;
  CzImage titleBackground;
  Kotonoha::TexturePtr titleBackgroundTexture;
  int titleSelection = 0;
  int titlePage = 0;
  float titleSlideFrom = 0;
  std::uint64_t titleSlideStart = 0;
  bool titleHighlight = false;
  bool storyOpen = false;
  bool storyHighlight = false;
  int storySelection = 0;
  Kotonoha::TouchActivation titleTouch;

  float titleOffset() const {
    const float target = titlePage * 1280.0f;
    const float t = std::clamp(float(SDL_GetTicks() - titleSlideStart) / 240.0f,
                               0.0f, 1.0f);
    const float rest = 1.0f - t;
    return target + (titleSlideFrom - target) * rest * rest * rest;
  }

  void selectTitle(int index) {
    if (titlePages[index] != titlePage) {
      titleSlideFrom = titleOffset();
      titleSlideStart = SDL_GetTicks();
      titlePage = titlePages[index];
    }
    titleSelection = index;
    titleHighlight = true;
    playSysSe("CURSOR");
  }

  SDL_FRect titleRect(int index, float offset) const {
    const auto &item = titleItems[index];
    return {item.center - item.width * .5f + titlePages[index] * 1280 - offset,
            540, float(item.width), 70};
  }

  int storyCount() const {
    if (getVar(12072))
      return 4;
    if (getVar(12071))
      return 3;
    return getVar(12070) ? 2 : 1;
  }

  SDL_FRect storyRect(int index) const {
    const auto &item = storyItems[index];
    return {item.center - item.width * .5f, 210.0f + index * 69,
            float(item.width), 72};
  }

  // VM state
  std::vector<ScriptInstruction> instructions;
  size_t instructionIndex = 0;
  std::unordered_map<uint32_t, CzImage> objectImages;
  std::unordered_map<uint32_t, bool> objectHasTransparency;
  std::unordered_map<uint32_t, Kotonoha::TexturePtr> objectTextures;
  std::unordered_map<uint32_t, uint64_t> failedImageRetryAt;
  CzImage background;
  Kotonoha::TexturePtr backgroundTexture;
  std::unordered_map<uint32_t, int32_t> numericVars;
  std::unordered_map<uint32_t, std::string> stringVars;
  std::unordered_map<uint32_t, bool> flags;
  // Global persistent flags (#12001..#12089) + unlocked BGM/CG sets
  std::map<uint32_t, int32_t> globalVars;
  std::map<std::pair<std::string, uint16_t>, std::string> varStrTable;
  std::unordered_map<uint16_t, std::vector<ShakeKeyframe>> shakeLists;

  std::vector<CallStackFrame> callStack;
  std::uint64_t waitDeadline = 0;
  std::optional<std::uint64_t> storyPausedAt;
  std::uint64_t storyPausedTotal = 0;
  std::uint64_t lastDrawDeadline = 0;
  bool waiting = false;
  bool skipMode = false;
  bool autoMode = false;
  std::uint64_t autoAdvanceAt = 0;

  void dialogueControl(AirTextBox::Control control) {
    switch (control) {
    case AirTextBox::Control::Menu:
      if (menu)
        menu->open();
      break;
    case AirTextBox::Control::Skip:
      skipMode = !skipMode;
      autoMode = false;
      break;
    case AirTextBox::Control::Auto:
      autoMode = !autoMode;
      skipMode = false;
      break;
    case AirTextBox::Control::Voice:
      if (runtime.currentDialogueVoiceId_)
        playVoice(runtime.currentDialogueVoiceId_);
      break;
    }
    autoAdvanceAt = 0;
  }

  // Active choice state
  bool choiceActive = false;
  uint16_t choiceTargetVarId = 6001;
  std::vector<std::string> currentChoiceOptions;

  // Slotted (group, slot) scene composition for 1:1 AIR layer transitions
  using LayerKey = uint16_t; // (uint16_t(group) << 8) | slot
  static LayerKey makeKey(uint8_t group, uint8_t slot) {
    return (uint16_t(group) << 8) | uint16_t(slot);
  }
  std::map<LayerKey, SlottedLayer> stagedSlots;
  std::map<LayerKey, SlottedLayer> visibleSlots;
  std::map<LayerKey, SlottedLayer> outgoingSlots;
  AirStageResetTracker stageResets;
  // GPU-owned full-scene framebuffer: crossfade AFTER sprite composition,
  // never by modifying each layer's alpha independently.
  Kotonoha::TexturePtr incomingTransitionFrame;
  uint64_t drawStartTick = 0;
  uint64_t drawDurationMs = 0;
  bool drawWipeMode = false;

  // Active screen/layer shake & flash effects
  uint16_t activeShakeId = 0;
  uint8_t activeShakeTarget = 9;
  uint64_t shakeStartTick = 0;

  uint64_t flashStartTick = 0;
  uint64_t flashDurationMs = 0;
  SDL_Color flashColor{255, 255, 255, 255};

  std::string currentScenario;
  std::vector<std::string> dialogueLines;
  size_t dialogueIndex = 0;
  bool dialogueVisible = false;

  Kotonoha::PcmPlayer syssePlayer;
  Kotonoha::PcmPlayer bgm;
  Kotonoha::PcmPlayer voice;
  Kotonoha::PcmPlayer se;
  std::map<std::uint8_t, Kotonoha::PcmPlayer> scriptSe;
  uint32_t currentBgmId = 0;
  uint32_t lastPlayedVoiceId = 0;
  bool bgmFadingOut = false;
  uint64_t bgmFadeStart = 0;
  uint64_t bgmFadeDurationMs = 0;

  std::unique_ptr<Kotonoha::MediaPlayer> moviePlayer;
  bool movieFinished = false;
  Kotonoha::VideoSkipGesture videoSkip;

  struct SplashLogo {
    std::string assetName;
    CzImage image;
    Kotonoha::TexturePtr texture;
  };
  std::vector<SplashLogo> splashLogos;
  size_t currentSplashIndex = 0;
  uint64_t splashStartTime = 0;
  bool showingSplash = false;
  static constexpr uint64_t noticeDuration = 7500;
  static constexpr uint64_t logoDuration = 1800;

  SceneLayers sceneLayers;

  explicit Impl(LucaSystemRuntime &r,
                std::shared_ptr<Kotonoha::Providers::LucaSystemAssets> a)
      : runtime(r), assets(std::move(a)) {
    loadSystemTables();
    try {
      if (assets->contains("_cggallery")) {
        galleryCatalog = ParseAirGallery(assets->read("_cggallery"));
        for (const auto &category : galleryCatalog)
          for (const auto &slot : category)
            galleryIds.insert(slot.variants.begin(), slot.variants.end());
      }
    } catch (const std::exception &e) {
      SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "LucaSystem: CG catalog: %s",
                  e.what());
    }
    loadGlobalState();
  }

  // Gallery keys live beyond the 16-bit AIR script variable namespace.
  static constexpr uint32_t cgKey(uint32_t id) { return 0x10000u + id; }
  bool cgUnlocked(uint32_t id) const {
    const auto flag = globalVars.find(cgKey(id));
    return flag != globalVars.end() && flag->second != 0;
  }

  std::filesystem::path globalStatePath() const {
    try {
      return Kotonoha::saveDirectory() / "global_system.kns";
    } catch (...) {
      return {};
    }
  }

  void loadGlobalState() {
    const auto path = globalStatePath();
    if (path.empty())
      return;
    try {
      const auto bytes = Kotonoha::Filesystem::readLocalFile(path);
      std::string text(bytes.begin(), bytes.end());
      std::istringstream in(text);
      std::string magic;
      if (!std::getline(in, magic) || magic != "LUCA_GLOBAL_1")
        return;
      uint32_t key = 0;
      int32_t val = 0;
      while (in >> key >> val) {
        globalVars[key] = val;
        numericVars[key] = val;
      }
    } catch (...) {
    }
  }

  void saveGlobalState() {
    // Derive DREAM / SUMMER / AIR arc-clear flags from route-end flags
    if (getVar(12073) != 0 && getVar(12074) != 0 && getVar(12075) != 0)
      setVarInternal(12070, 1);
    if (getVar(12076) != 0)
      setVarInternal(12071, 1);
    if (getVar(12077) != 0)
      setVarInternal(12072, 1);

    const auto path = globalStatePath();
    if (path.empty())
      return;
    std::ostringstream out;
    out << "LUCA_GLOBAL_1\n";
    for (const auto &[k, v] : globalVars)
      out << k << ' ' << v << '\n';
    const std::string s = out.str();
    std::vector<uint8_t> bytes(s.begin(), s.end());
    std::string err;
    (void)Kotonoha::Filesystem::writeBinaryFileAtomically(path, bytes, &err);
  }

  void setVarInternal(uint32_t id, int32_t val) {
    numericVars[id] = val;
    if (id >= 12000 && id < 20000)
      globalVars[id] = val;
    if (id == 6002 && textbox)
      textbox->setWindowType(val);
  }

  void setVar(uint32_t id, int32_t val) {
    setVarInternal(id, val);
    if (id >= 12000 && id < 20000)
      saveGlobalState();
  }

  int32_t getVar(uint32_t id) const {
    auto it = numericVars.find(id);
    if (it != numericVars.end())
      return it->second;
    auto git = globalVars.find(id);
    if (git != globalVars.end())
      return git->second;
    auto fit = flags.find(id);
    if (fit != flags.end())
      return fit->second ? 1 : 0;
    return 0;
  }

  void syncGlobalVarsIntoSession() {
    for (const auto &[k, v] : globalVars)
      numericVars[k] = v;
  }

  void loadSystemTables() {
    if (!assets || !assets->scriptPak())
      return;
    auto *pak = assets->scriptPak();
    if (pak->findByName("_varstr")) {
      try {
        Script s("_varstr", assets->read("_varstr"));
        if (s.valid()) {
          for (const auto &ins : s.instructions()) {
            if (ins.opcode == OP_VARSTR_SET) {
              if (auto entry = ParseVarStrSet(ins))
                varStrTable[{entry->scenario, entry->id}] = entry->text;
            }
          }
        }
      } catch (...) {
      }
    }
    if (pak->findByName("_shakelist")) {
      try {
        Script s("_shakelist", assets->read("_shakelist"));
        if (s.valid()) {
          for (const auto &ins : s.instructions()) {
            if (ins.opcode == OP_SHAKELIST_SET && ins.paramBytes.size() >= 14) {
              uint16_t shakeId = paramU16(ins, 0);
              std::vector<ShakeKeyframe> keys;
              for (size_t off = 8; off + 6 <= ins.paramBytes.size(); off += 6) {
                int16_t fr = static_cast<int16_t>(paramU16(ins, off));
                int16_t dx = static_cast<int16_t>(paramU16(ins, off + 2));
                int16_t dy = static_cast<int16_t>(paramU16(ins, off + 4));
                if (fr > 0)
                  keys.push_back({fr, dx, dy});
              }
              if (!keys.empty())
                shakeLists[shakeId] = std::move(keys);
            }
          }
        }
      } catch (...) {
      }
    }
  }

  void setupSplashSequence() {
    // Steam boot order: white notice, Visual Arts, Key, then Prototype/LUCA.
    static const char *names[] = {"logo1", "logo2", "logo0"};
    splashLogos.clear();
    for (const char *n : names) {
      if (assets->contains(n)) {
        try {
          auto img = DecodeCzImage(assets->read(n));
          splashLogos.push_back({n, std::move(img), nullptr});
        } catch (...) {
        }
      }
    }
    showingSplash = !splashLogos.empty();
    currentSplashIndex = 0;
    splashStartTime = SDL_GetTicks();
  }

  std::vector<std::string> scenarioFiles() const {
    std::vector<std::string> result;
    if (auto *pak = assets->scriptPak()) {
      for (const auto &entry : pak->entries()) {
        if (!isSystemScript(entry.name))
          result.push_back(entry.name);
      }
    }
    std::sort(result.begin(), result.end());
    return result;
  }

  // Canonical AIR New Game root is seen170 (DREAM) at AIR.exe 0x140495978,
  // NOT seen163 (the final Sora/AIR spoiler chapter).
  std::string startupScenario() const {
    if (auto *pak = assets->scriptPak()) {
      if (pak->findByName("seen170"))
        return "seen170";
    }
    auto files = scenarioFiles();
    return files.empty() ? std::string() : files.front();
  }

  std::string titleBackgroundName() const {
    if (getVar(12072) != 0 && assets->contains("title4a"))
      return "title4a";
    if (getVar(12071) != 0 && assets->contains("title3a"))
      return "title3a";
    if (getVar(12070) != 0 && assets->contains("title2a"))
      return "title2a";
    return "title1a";
  }

  void playTitleAmbience() {
    // AIR Steam loops nami from SE.PAK at the title. Reminiscences (trk22)
    // starts in DREAM's script, after the player leaves the story selector.
    constexpr uint32_t waves = 200014;
    if (assets->contains("se:" + std::to_string(waves)))
      playSe(waves, true);
  }

  void finishSplash() {
    showingSplash = false;
    if (originalTitle)
      playTitleAmbience();
  }

  void advanceSplash() {
    if (++currentSplashIndex > splashLogos.size())
      finishSplash();
    else
      splashStartTime = SDL_GetTicks();
  }

  void enterTitleScreen() {
    stopAudio();
    moviePlayer.reset();
    movieFinished = true;
    running = false;
    waiting = false;
    skipMode = autoMode = false;
    autoAdvanceAt = 0;
    choiceActive = false;
    dialogueVisible = false;
    if (textbox)
      textbox->hide();
    if (choiceScene)
      choiceScene->hide();
    if (menu)
      menu->close();
    if (gallery)
      gallery->close();
    if (backlog)
      backlog->close();
    if (engine) {
      runtime.SetState(Kotonoha::GameState::Gameplay);
      runtime.ClearScenes();
      runtime.AddScene(choiceScene);
      runtime.AddScene(menu);
      runtime.AddScene(gallery.get());
    }
    stagedSlots.clear();
    visibleSlots.clear();
    outgoingSlots.clear();
    stageResets.reset();
    drawDurationMs = 0;
    drawWipeMode = false;
    pruneObjectCaches();
    sceneLayers.reset();
    originalTitle = true;
    titleShown = true;
    titlePage = titleSelection = 0;
    titleSlideFrom = 0;
    titleSlideStart = 0;
    titleHighlight = storyOpen = false;
    titleTouch.reset();
    titleBackgroundTexture.reset();
    try {
      const auto bgName = titleBackgroundName();
      if (assets->contains(bgName))
        titleBackground = DecodeCzImage(assets->read(bgName));
    } catch (...) {
    }
    if (!showingSplash)
      playTitleAmbience();
  }

  void openSaveSlots(bool load) {
    if (!saveMenu || saveMenu->isOpen())
      return;
    if (menu)
      menu->close();
    saveMenu->open(Kotonoha::saveDirectory().string(), load, currentScenario);
    runtime.AddScene(saveMenu.get());
  }

  int32_t evaluateExpr(std::string_view expr) const {
    if (expr.empty())
      return 0;
    ExprParser parser(expr, numericVars, flags);
    return parser.eval();
  }

  bool evaluateCondition(std::string_view expr) const {
    if (expr.empty())
      return true;
    return evaluateExpr(expr) != 0;
  }

  bool loadScenarioScript(const std::string &name) {
    if (name.empty())
      return false;
    auto *pak = assets->scriptPak();
    if (!pak)
      return false;
    const PakEntry *entry = pak->findByName(name);
    if (!entry && name.rfind(".scr") == std::string::npos) {
      entry = pak->findByName(name + ".scr");
    }
    if (!entry && name.size() > 4 && name.substr(name.size() - 4) == ".scr") {
      entry = pak->findByName(name.substr(0, name.size() - 4));
    }
    if (!entry)
      return false;

    auto bytes = assets->read(entry->name);
    Script script(entry->name,
                  std::vector<std::uint8_t>(bytes.begin(), bytes.end()));
    if (!script.valid())
      return false;

    currentScenario = entry->name;
    instructions = script.instructions();
    instructionIndex = 0;
    dialogueLines = script.strings();
    dialogueIndex = 0;
    dialogueVisible = false;
    choiceActive = false;
    syncGlobalVarsIntoSession();
    runtime.scenarioTexts_.clear();
    const auto remember = [&](const std::string &value) {
      if (!value.empty() && std::find(runtime.scenarioTexts_.begin(),
                                      runtime.scenarioTexts_.end(),
                                      value) == runtime.scenarioTexts_.end())
        runtime.scenarioTexts_.push_back(value);
    };
    for (const auto &line : dialogueLines) {
      std::string speaker, dialogue;
      parseSpeakerAndSanitizeText(line, speaker, dialogue);
      remember(speaker);
      remember(dialogue);
      remember(line); // Preserve legacy script-string translation keys.
    }
    return true;
  }

  void stopAudio() {
    bgm.Stop();
    bgm.SetGain(1.0f);
    bgmFadingOut = false;
    bgmFadeStart = 0;
    bgmFadeDurationMs = 0;
    voice.Stop();
    se.Stop();
    scriptSe.clear();
    syssePlayer.Stop();
    currentBgmId = 0;
    lastPlayedVoiceId = 0;
  }

  void startBgmFadeOut(uint64_t durationMs) {
    if (currentBgmId == 0 || !bgm.Playing()) {
      playBgm(0, false);
      return;
    }
    if (durationMs == 0)
      durationMs = 2000;
    bgmFadingOut = true;
    bgmFadeStart = storyNow();
    bgmFadeDurationMs = durationMs;
  }

  void tickBgmFade() {
    if (!bgmFadingOut)
      return;
    const uint64_t now = storyNow();
    const uint64_t elapsed = now > bgmFadeStart ? (now - bgmFadeStart) : 0;
    if (bgmFadeDurationMs == 0 || elapsed >= bgmFadeDurationMs) {
      playBgm(0, false);
      return;
    }
    const float gain =
        1.0f - std::clamp(float(elapsed) / float(bgmFadeDurationMs), 0.0f, 1.0f);
    bgm.SetGain(gain);
  }

  void playDecodedAudio(Kotonoha::PcmPlayer &player, const char *channelName,
                        const std::string &logicalId, int outCh, int outFreq,
                        bool loop) {
    if (!engine || !assets || logicalId.empty())
      return;
    Kotonoha::AudioClip decoded = assets->loadAudio(logicalId);
    SDL_AudioSpec target{SDL_AUDIO_S16, outCh, outFreq};
    Uint8 *converted = nullptr;
    int size = 0;
    if (!SDL_ConvertAudioSamples(&decoded.spec, decoded.data.data(),
                                 int(decoded.data.size()), &target, &converted,
                                 &size))
      throw std::runtime_error(SDL_GetError());
    Kotonoha::SDLMemoryPtr<Uint8> convertedOwner(converted);
    Kotonoha::PcmPlayer::Clip clip;
    clip.channels = target.channels;
    clip.rate = target.freq;
    clip.loop = loop;
    clip.samples.resize(static_cast<size_t>(size) / 2);
    std::memcpy(clip.samples.data(), converted, static_cast<size_t>(size));
    player.Play(engine->GetAudio().channel(channelName), std::move(clip));
  }

  void playSysSe(const char *name) {
    if (!engine || !assets || !name || !*name)
      return;
    try {
      if (assets->contains(name))
        playDecodedAudio(syssePlayer, "Se", name, 2, 48000, false);
    } catch (...) {
    }
  }

  void playVoice(uint32_t id) {
    if (id == 0)
      return;
    if (id == lastPlayedVoiceId && voice.Playing())
      return;
    lastPlayedVoiceId = id;
    if (!engine || !assets)
      return;
    try {
      voice.Stop();
      playDecodedAudio(voice, "Voice", "voice:" + std::to_string(id), 1, 48000,
                       false);
    } catch (const std::exception &e) {
      SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "LucaSystem: voice %u: %s", id,
                  e.what());
    }
  }

  void playBgm(uint32_t id, bool loop) {
    bgmFadingOut = false;
    bgmFadeStart = 0;
    bgmFadeDurationMs = 0;
    bgm.SetGain(1.0f);
    if (id == 0) {
      bgm.Stop();
      currentBgmId = 0;
      return;
    }
    if (currentBgmId == id && bgm.Playing())
      return;
    currentBgmId = id;
    if (!engine || !assets)
      return;
    try {
      playDecodedAudio(bgm, "BGM", "bgm:" + std::to_string(id), 2, 48000, loop);
    } catch (const std::exception &e) {
      SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "LucaSystem: bgm %u: %s", id,
                  e.what());
    }
  }

  void playSe(Kotonoha::PcmPlayer &player, uint32_t id, bool loop) {
    if (id == 0) {
      player.Stop();
      return;
    }
    if (!engine || !assets)
      return;
    try {
      playDecodedAudio(player, "Se", "se:" + std::to_string(id), 2, 48000,
                       loop);
    } catch (const std::exception &e) {
      SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "LucaSystem: se %u: %s", id,
                  e.what());
    }
  }

  void playSe(uint32_t id, bool loop) { playSe(se, id, loop); }

  std::uint64_t storyNow() const {
    return storyPausedAt.value_or(SDL_GetTicks()) - storyPausedTotal;
  }

  void syncStoryPause(bool paused) {
    if (paused == storyPausedAt.has_value())
      return;
    const auto now = SDL_GetTicks();
    if (paused) {
      storyPausedAt = now;
    } else {
      storyPausedTotal += now - *storyPausedAt;
      storyPausedAt.reset();
    }
    if (moviePlayer)
      moviePlayer->setPaused(paused);
    voice.SetPaused(paused);
    for (auto &[channel, player] : scriptSe)
      player.SetPaused(paused);
  }

  void playMovie(const std::string &name) {
    if (!engine || !assets)
      return;
    const std::string base = name.empty() ? "AIR_OP_A" : name;
    const std::vector<std::string> candidates = {
        "movie/" + base + "_EN.webm",
        "movie/" + base + ".webm",
        base + "_EN.webm",
        base + ".webm",
        "movie/" + base + ".mp4",
        "movie/" + base + ".ogv",
        "movie/" + base + ".wmv",
        base + ".mp4",
        base + ".ogv",
        base + ".wmv",
        base,
    };
    for (const auto &cand : candidates) {
      try {
        if (assets->contains(cand)) {
          moviePlayer =
              std::make_unique<Kotonoha::MediaPlayer>(engine->GetAudio());
          moviePlayer->open(*assets, cand, false, "BGM");
          if (moviePlayer->isOpen()) {
            // The retail WebM supplies the opening soundtrack. Don't mix
            // queued dialogue, ambience or script BGM into the movie.
            playBgm(0, false);
            voice.Stop();
            scriptSe.clear();
            movieFinished = false;
            videoSkip.reset();
            waiting = true;
            SDL_Log("LucaSystem: playing movie %s", cand.c_str());
            return;
          }
          moviePlayer.reset();
        }
      } catch (const std::exception &e) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                    "LucaSystem: movie %s open error: %s", cand.c_str(),
                    e.what());
      }
    }
    SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                "LucaSystem: movie %s not found, continuing script",
                base.c_str());
  }

  void loadObjectImage(uint32_t id) {
    if (id == 0 || objectImages.count(id))
      return;
    const uint64_t now = SDL_GetTicks();
    if (const auto retry = failedImageRetryAt.find(id);
        retry != failedImageRetryAt.end() && now < retry->second)
      return;
    try {
      auto image = DecodeCzImage(assets->readImageAsset(id));
      // Fill RGB in fully transparent boundary texels. SDL samples straight
      // alpha linearly; without this, cropped character sprites can exhibit
      // dark/color-fringed edges despite the decoded visible pixels being OK.
      if (!image.rgba.empty())
        Kotonoha::prepareStraightAlphaForLinearFiltering(
            image.rgba.data(), image.width, image.height, image.width * 4);
      objectHasTransparency[id] = hasNonOpaquePixels(image.rgba);
      objectImages[id] = std::move(image);
      objectTextures[id].reset();
      failedImageRetryAt.erase(id);
    } catch (const std::exception &e) {
      // A missing/CZ-corrupt asset may be referenced by several slots.
      // Decode/log at most once per two seconds, not every frame. Retry
      // later so a newly mounted asset overlay can still fix the image.
      if (failedImageRetryAt.size() > 4096)
        failedImageRetryAt.clear();
      failedImageRetryAt[id] = now + 2000;
      SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "LucaSystem: image %u: %s", id,
                  e.what());
    }
  }

  // Script slot (2, n) holds a complete portrait. Small CZ4 expression
  // fragments replace one another above it in the private (3, n) layer.
  // Check stagedSlots first (body + expression staged before DRAW), then
  // fall back to visibleSlots (INIT may have cleared the staged portrait).
  void stageImage(uint8_t group, uint8_t slot, uint32_t assetId, float x,
                  float y, bool hasExplicitPos = true) {
    const LayerKey key = makeKey(group, slot);
    if (assetId == 0) {
      stagedSlots.erase(key);
      if (group == 2)
        stagedSlots.erase(makeKey(3, slot));
      stageResets.clearSlot(group, slot);
      return;
    }
    if (!std::isfinite(x) || !std::isfinite(y))
      return;
    loadObjectImage(assetId);
    // Failed CZ loads cannot cause the prior valid sprite to disappear
    // during the next DRAW. An explicit INIT clear still takes precedence.
    if (!objectImages.count(assetId)) {
      keepAirSpriteAfterDecodeFailure(stagedSlots, visibleSlots, stageResets,
                                      group, slot);
      return;
    }
    SlottedLayer layer;
    layer.group = group;
    layer.slot = slot;
    layer.assetId = assetId;
    layer.posX = x;
    layer.posY = y;
    if (group != 0 && group != 2) {
      const SlottedLayer *prior = nullptr;
      if (const auto it = stagedSlots.find(key); it != stagedSlots.end())
        prior = &it->second;
      else if (stageResets.allowsVisibleFallback(group, slot)) {
        if (const auto it = visibleSlots.find(key); it != visibleSlots.end())
          prior = &it->second;
      }
      if (prior)
        inheritAirPortraitVisuals(layer, *prior, !hasExplicitPos);
    }
    if (group == 2) {
      const auto key = makeKey(group, slot);
      const SlottedLayer *baseLayer = nullptr;
      if (auto stagedIt = stagedSlots.find(key); stagedIt != stagedSlots.end())
        baseLayer = &stagedIt->second;
      else if (stageResets.allowsVisibleFallback(group, slot)) {
        if (auto shownIt = visibleSlots.find(key);
            shownIt != visibleSlots.end())
          baseLayer = &shownIt->second;
      }
      // A pose change in the same slot changes pixels, not the object's
      // opacity, tint, scale, rotation or active MOVE.
      if (baseLayer)
        inheritAirPortraitVisuals(layer, *baseLayer, !hasExplicitPos);
      const auto nextImage = objectImages.find(assetId);
      if (baseLayer && nextImage != objectImages.end()) {
        const auto baseImage = objectImages.find(baseLayer->assetId);
        if (baseImage != objectImages.end() &&
            isCharacterFragment(
                baseImage->second.width, baseImage->second.height,
                baseImage->second.canvasWidth, baseImage->second.canvasHeight,
                nextImage->second.width, nextImage->second.height,
                nextImage->second.canvasWidth, nextImage->second.canvasHeight)) {
          const SlottedLayer preservedBase = *baseLayer;
          stagedSlots[key] = preservedBase;
          // Face crops must move/rotate/scale with their full portrait,
          // including while MOVE is still interpolating.
          inheritAirPortraitVisuals(layer, preservedBase, true);
          layer.group = 3;
          stagedSlots[makeKey(3, slot)] = layer;
          return;
        }
      }
      stagedSlots.erase(makeKey(3, slot));
    }
    stagedSlots[makeKey(group, slot)] = layer;
  }

  void pruneObjectCaches() {
    pruneAirObjectCaches(stagedSlots, visibleSlots, outgoingSlots,
                         objectImages, objectTextures);
  }

  void syncLegacySceneLayers() {
    std::vector<SceneObject> flat;
    flat.reserve(visibleSlots.size());
    for (const auto &[key, slot] : visibleSlots) {
      if (!slot.visible || slot.assetId == 0)
        continue;
      SceneObject obj;
      obj.id = slot.assetId;
      obj.x = static_cast<int>(std::lround(slot.posX + slot.targetOffsetX));
      obj.y = static_cast<int>(std::lround(slot.posY + slot.targetOffsetY));
      obj.alpha = slot.alpha;
      obj.scaleX = slot.scaleX;
      obj.scaleY = slot.scaleY;
      obj.rotation = slot.rotation;
      obj.priority = key == AirPersistentBackdropKey
                         ? -100
                         : int(slot.group) * 100 + int(slot.slot);
      obj.visible = slot.visible;
      obj.r = slot.r;
      obj.g = slot.g;
      obj.b = slot.b;
      flat.push_back(obj);
    }
    sceneLayers.reset(flat);
  }

  void commitStagedSlots(uint64_t now, uint64_t durationMs, bool wipe) {
    // A CZ overlay in slot (0,0) does not erase the last opaque outdoor
    // background. A full opaque scene image replaces it as usual.
    retainAirBackdrop(stagedSlots, visibleSlots, [&](const SlottedLayer &layer) {
      if (layer.assetId == 65000 || layer.assetId == 65001)
        return layer.group == 0 && layer.slot == 0 &&
               layer.alpha >= 0.99f && layer.visible;
      const auto found = objectImages.find(layer.assetId);
      return found != objectImages.end() &&
             isFullAirBackdrop(layer, found->second);
    });
    // A cleared portrait also clears its private expression fragment.
    for (auto it = stagedSlots.begin(); it != stagedSlots.end();) {
      if (it->second.group == 3 &&
          !stagedSlots.count(makeKey(2, it->second.slot)))
        it = stagedSlots.erase(it);
      else
        ++it;
    }
    if (durationMs > 0) {
      outgoingSlots = visibleSlots;
      drawStartTick = now;
      drawDurationMs = std::min<uint64_t>(durationMs, 60000);
      drawWipeMode = wipe;
    } else {
      outgoingSlots.clear();
      drawDurationMs = 0;
      drawWipeMode = false;
    }
    visibleSlots = stagedSlots;
    stageResets.reset();
    syncLegacySceneLayers();
    bool unlocked = false;
    for (const auto &[key, layer] : visibleSlots) {
      if (layer.visible && layer.alpha > 0 && galleryIds.count(layer.assetId) &&
          objectImages.count(layer.assetId) && !cgUnlocked(layer.assetId)) {
        globalVars[cgKey(layer.assetId)] = 1;
        unlocked = true;
      }
    }
    if (unlocked)
      saveGlobalState();
    pruneObjectCaches();
  }

  std::pair<float, float> computeShakeOffset(uint64_t now,
                                             uint8_t layerTarget) {
    if (activeShakeId == 0)
      return {0.0f, 0.0f};
    if (activeShakeTarget != 9 && activeShakeTarget != layerTarget)
      return {0.0f, 0.0f};
    auto it = shakeLists.find(activeShakeId);
    if (it == shakeLists.end() || it->second.empty()) {
      activeShakeId = 0;
      return {0.0f, 0.0f};
    }
    uint64_t elapsedMs = now > shakeStartTick ? (now - shakeStartTick) : 0;
    uint64_t accumMs = 0;
    for (const auto &kf : it->second) {
      const uint64_t kfMs = std::max<uint64_t>(16, framesToMs(kf.frames));
      if (elapsedMs < accumMs + kfMs)
        return {float(kf.dx), float(kf.dy)};
      accumMs += kfMs;
    }
    activeShakeId = 0;
    return {0.0f, 0.0f};
  }

  void renderSlottedLayer(Kotonoha::RendererRef renderer,
                          const SlottedLayer &slot, uint64_t now,
                          float opacityMod = 1.0f,
                          const SlottedLayer *portrait = nullptr) {
    if (!slot.visible || slot.assetId == 0 || !std::isfinite(slot.alpha) ||
        slot.alpha <= 0.0f || !std::isfinite(opacityMod) ||
        opacityMod <= 0.0f || !std::isfinite(slot.scaleX) ||
        !std::isfinite(slot.scaleY))
      return;
    loadObjectImage(slot.assetId);
    auto it = objectImages.find(slot.assetId);
    if (it == objectImages.end())
      return;
    const auto &img = it->second;
    auto &tex = objectTextures[slot.assetId];
    if (!tex) {
      tex.reset(uploadAirStaticRgba(
          renderer, img.width, img.height, img.rgba.data(), img.width * 4,
          (slot.assetId == 65000 || slot.assetId == 65001)
              ? SDL_SCALEMODE_NEAREST
              : SDL_SCALEMODE_LINEAR));
      if (!tex)
        return;
    }

    const float alpha = std::clamp(slot.alpha * opacityMod, 0.0f, 1.0f);
    SDL_SetTextureAlphaMod(tex.get(),
                           static_cast<Uint8>(std::lround(alpha * 255.0f)));
    SDL_SetTextureColorMod(tex.get(), slot.r, slot.g, slot.b);

    // For expression crops the base portrait defines the full character
    // canvas, movement and rotation pivot. A cropped face file's own
    // anchor may be different even when its canvas dimensions match.
    const CzImage *baseImage = &img;
    const SlottedLayer *pose = &slot;
    if (portrait && slot.group == 3) {
      const auto found = objectImages.find(portrait->assetId);
      if (found != objectImages.end() &&
          found->second.canvasWidth == img.canvasWidth &&
          found->second.canvasHeight == img.canvasHeight) {
        baseImage = &found->second;
        pose = portrait;
      }
    }
    const float offX = airMoveOffset(pose->startOffsetX, pose->targetOffsetX,
                                     pose->moveStart, pose->moveDuration, now);
    const float offY = airMoveOffset(pose->startOffsetY, pose->targetOffsetY,
                                     pose->moveStart, pose->moveDuration, now);

    const uint8_t shakeKey =
        (slot.group == 0) ? 0 : static_cast<uint8_t>(1 + slot.slot);
    const auto [shakeX, shakeY] = computeShakeOffset(now, shakeKey);

    const auto crop = airCharacterCropPlacement(
        *pose, *baseImage, img, offX, offY, shakeX, shakeY);
    float drawX = crop.x, drawY = crop.y;
    float drawW = crop.w, drawH = crop.h;

    if (slot.assetId == 65000 || slot.assetId == 65001) {
      drawX = 0.0f;
      drawY = 0.0f;
      drawW = 1280.0f;
      drawH = 720.0f;
    }

    if (!std::isfinite(drawX) || !std::isfinite(drawY) ||
        !std::isfinite(drawW) || !std::isfinite(drawH) ||
        drawW <= 0 || drawH <= 0)
      return;
    SDL_FRect dst{drawX, drawY, drawW, drawH};
    // WIPE uses a full-scene clip instead of cropping a rotated/scaled
    // sprite's source rectangle, which used to shift faces and stretch art.
    // Upscalers are intended for flat background art; running an image
    // enhancement filter on transparent CHARCG portraits may distort edges,
    // hair strands and the sprite's original alpha silhouette.
    const auto transparency = objectHasTransparency.find(slot.assetId);
    const bool nativeSprite = slot.group == 2 || slot.group == 3 ||
        (transparency != objectHasTransparency.end() && transparency->second);
    const SDL_FPoint sharedPivot{crop.pivotX, crop.pivotY};
    const double rotation = double(pose->rotation);
    if (rotation != 0.0) {
      if (nativeSprite)
        SDL_RenderTextureRotated(&renderer, tex.get(), nullptr, &dst,
                                 rotation, &sharedPivot, SDL_FLIP_NONE);
      else
        Kotonoha::RenderUpscaledTextureRotated(
            &renderer, tex.get(), nullptr, &dst, rotation,
            nullptr, SDL_FLIP_NONE);
    } else {
      if (nativeSprite)
        SDL_RenderTexture(&renderer, tex.get(), nullptr, &dst);
      else
        Kotonoha::RenderUpscaledTexture(&renderer, tex.get(), nullptr, &dst);
    }

  }

  // Always compose each portrait and its expression as an adjacent pair.
  // This is the same ordering for ordinary frames, outgoing and incoming
  // transition frames, so transitions cannot reorder character faces.
  void renderSlottedScene(Kotonoha::RendererRef renderer,
                          const std::map<LayerKey, SlottedLayer> &slots,
                          uint64_t now, float opacity = 1.0f) {
    for (const auto key : orderedAirLayerKeys(slots)) {
      const auto it = slots.find(key);
      if (it == slots.end())
        continue;
      const SlottedLayer *portrait = nullptr;
      if ((key >> 8) == 3) {
        const auto base = slots.find(makeKey(2, std::uint8_t(key)));
        if (base != slots.end())
          portrait = &base->second;
      }
      renderSlottedLayer(renderer, it->second, now, opacity, portrait);
    }
  }

  // SDL3 keeps viewport, clip and logical presentation separately for each
  // render target. Restore the caller's target before rendering the old
  // frame or UI; the UI remains fully GPU-accelerated at native resolution.
  bool renderCrossfade(Kotonoha::RendererRef renderer, uint64_t now, float t) {
    if (t <= 0.0f) {
      renderSlottedScene(renderer, outgoingSlots, now);
      return true;
    }
    if (!incomingTransitionFrame) {
      incomingTransitionFrame.reset(SDL_CreateTexture(
          &renderer, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_TARGET,
          1280, 720));
      if (!incomingTransitionFrame ||
          !SDL_SetTextureBlendMode(incomingTransitionFrame.get(),
                                   SDL_BLENDMODE_BLEND) ||
          !SDL_SetTextureScaleMode(incomingTransitionFrame.get(),
                                   SDL_SCALEMODE_LINEAR)) {
        incomingTransitionFrame.reset();
        return false;
      }
    }
    if (!renderAirSceneTarget(renderer, *incomingTransitionFrame, [&] {
          renderSlottedScene(renderer, visibleSlots, now);
        }))
      return false;

    // Blend two fully composed frames, not each sprite in isolation.
    renderSlottedScene(renderer, outgoingSlots, now);
    const SDL_FRect frame{0, 0, 1280, 720};
    return blendAirSceneTarget(renderer, *incomingTransitionFrame, t, frame);
  }

  bool renderWipe(Kotonoha::RendererRef renderer, uint64_t now, float t) {
    return renderAirHorizontalWipe(
        renderer, 1280, 720, t,
        [&] { renderSlottedScene(renderer, outgoingSlots, now); },
        [&] { renderSlottedScene(renderer, visibleSlots, now); });
  }

  bool hasCalendarBackground(const std::map<uint16_t, SlottedLayer> &slots) const {
    const auto background = slots.find(makeKey(0, 0));
    if (background == slots.end())
      return false;
    if (!background->second.visible || background->second.alpha <= 0.0f)
      return false;
    if (background->second.assetId == 0 ||
        background->second.assetId == 65000 ||
        background->second.assetId == 65001)
      return false;
    for (const auto &[key, slot] : slots) {
      if (slot.visible && slot.alpha >= 0.99f &&
          (slot.assetId == 65000 || slot.assetId == 65001))
        return false;
    }
    // Scene slot (0,0) may be only a cropped/transparent fragment. Do not
    // float the calendar over empty black pixels as in seen170 transitions.
    const auto image = objectImages.find(background->second.assetId);
    if (image == objectImages.end() || image->second.rgba.empty())
      return false;
    const auto &slot = background->second;
    const auto &art = image->second;
    const float x = croppedSpriteOrigin(
        slot.posX, float(art.anchorX), float(art.cropX),
        slot.scaleX, slot.targetOffsetX, 0.0f);
    const float y = croppedSpriteOrigin(
        slot.posY, float(art.anchorY), float(art.cropY),
        slot.scaleY, slot.targetOffsetY, 0.0f);
    // Centre of the native 128x133 calendar badge, not window pixels.
    return rasterCoversPointWithOpacity(
        art.rgba, art.width, art.height, x, y, slot.scaleX, slot.scaleY,
        69.33f, 66.67f, slot.alpha);
  }

  void renderCalendarBadge(Kotonoha::RendererRef renderer, uint64_t now,
                           bool inTransition) {
    const std::string badge = airCalendarBadgeName(getVar(6003));
    if (badge.empty())
      return;
    if (!dialogueVisible && !choiceActive)
      return;
    if (textbox && textbox->isUserHidden())
      return;
    if (getVar(6002) == 2)
      return;
    const bool visOk = hasCalendarBackground(visibleSlots);
    const bool outOk = inTransition && hasCalendarBackground(outgoingSlots);
    if (!visOk && !outOk)
      return;
    float badgeAlpha = 1.0f;
    if (inTransition) {
      const float t = std::clamp(
          float(now > drawStartTick ? (now - drawStartTick) : 0) /
              float(drawDurationMs),
          0.0f, 1.0f);
      if (visOk && outOk)
        badgeAlpha = 1.0f;
      else if (visOk)
        badgeAlpha = t;
      else
        badgeAlpha = 1.0f - t;
    }
    if (badgeAlpha <= 0.001f)
      return;
    try {
      if (!assets->contains(badge))
        return;
      auto &calendar = assets->texture(renderer, badge);
      Uint8 originalAlpha = 255;
      SDL_GetTextureAlphaMod(&calendar, &originalAlpha);
      SDL_SetTextureAlphaMod(
          &calendar,
          static_cast<Uint8>(std::lround(badgeAlpha * float(originalAlpha))));
      // CZ4 stores a 192x200 crop at x=8 in its 200x200 canvas.
      const SDL_FRect position{5.33f, 0.0f, 128.0f, 133.33f};
      // The badge is native transparent UI art; never run an image upscaler.
      SDL_RenderTexture(&renderer, &calendar, nullptr, &position);
      SDL_SetTextureAlphaMod(&calendar, originalAlpha);
    } catch (const std::exception &error) {
      SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                  "LucaSystem: calendar composition: %s", error.what());
    }
  }

  void runUntilPause() {
    std::size_t budget = 16384;
    while (instructionIndex < instructions.size() && budget-- > 0) {
      if (waiting || choiceActive)
        return;
      const auto &ins = instructions[instructionIndex++];

      switch (ins.opcode) {
      case OP_MESSAGE: {
        const uint16_t voiceId = paramU16(ins, 0);
        if (voiceId != 0)
          playVoice(voiceId);
        if (!ins.text.empty()) {
          std::string speaker, text;
          parseSpeakerAndSanitizeText(ins.text, speaker, text);
          dialogueVisible = true;
          runtime.originalActiveKeys_.clear();
          if (!speaker.empty())
            runtime.originalActiveKeys_.push_back(speaker);
          runtime.originalActiveKeys_.push_back(text);
          runtime.currentDialogueVoiceId_ = voiceId;
          const auto localize = [&](const std::string &original) {
            return engine ? engine->GetTranslation().text(0, original)
                          : original;
          };
          runtime.dialogueText_ = localize(text);
          runtime.dialogueSpeaker_ = localize(speaker);
          if (textbox) {
            textbox->setWindowType(getVar(6002));
            textbox->setDialogue(runtime.dialogueSpeaker_,
                                 runtime.dialogueText_);
            textbox->show();
          }
          if (backlog && !text.empty()) {
            backlog->addEntry(runtime.dialogueSpeaker_, runtime.dialogueText_);
            if (voiceId != 0)
              backlog->setLastVoice("voice:" + std::to_string(voiceId));
          }
          return;
        }
        break;
      }
      case OP_LOG_BEGIN: {
        if (ins.paramBytes.size() >= 5) {
          const uint16_t voiceId = paramU16(ins, 0);
          if (voiceId != 0)
            playVoice(voiceId);
          size_t cursor = 3;
          std::string jp = ReadLsUtf16(ins.paramBytes, cursor);
          std::string en = ReadLsUtf16(ins.paramBytes, cursor);
          std::string zh = ReadLsUtf16(ins.paramBytes, cursor);
          const std::string &chosen =
              !en.empty() ? en : (!jp.empty() ? jp : zh);
          if (!chosen.empty() && backlog) {
            std::string spk, clean;
            parseSpeakerAndSanitizeText(chosen, spk, clean);
            if (!clean.empty()) {
              const auto translatedSpeaker =
                  engine ? engine->GetTranslation().text(0, spk) : spk;
              const auto translatedLine =
                  engine ? engine->GetTranslation().text(0, clean) : clean;
              backlog->addEntry(translatedSpeaker, translatedLine);
              if (voiceId != 0)
                backlog->setLastVoice("voice:" + std::to_string(voiceId));
            }
          }
        }
        break;
      }
      case OP_LOG_BREAK:
      case OP_LOG_END:
        break;

      case OP_MOVIE: {
        std::string movieName = ins.text;
        if (movieName.empty() && ins.paramBytes.size() >= 2) {
          size_t cursor = 0;
          movieName = ReadLsUtf8(ins.paramBytes, cursor);
        }
        // Apply optional post-movie background ID if encoded after movie string
        playMovie(movieName);
        if (moviePlayer && moviePlayer->isOpen())
          return;
        break;
      }

      case OP_MESSAGE_CLEAR:
      case OP_CLOSE_WINDOW:
      case OP_FADE_WINDOW: {
        dialogueVisible = false;
        if (textbox)
          textbox->hide();
        break;
      }
      case OP_MESSAGE_WAIT: {
        waiting = true;
        waitDeadline = storyNow() + 250;
        return;
      }

      case OP_WAIT:
      case OP_WAIT_BSKIP: {
        const uint64_t now = storyNow();
        if (ins.paramBytes.empty()) {
          if (now < lastDrawDeadline && !skipMode) {
            waiting = true;
            waitDeadline = lastDrawDeadline;
            return;
          }
          break;
        }
        const int frames = int(paramU16(ins, 0));
        const uint64_t ms = skipMode ? 0 : framesToMs(frames);
        if (ms > 0) {
          waiting = true;
          waitDeadline = now + ms;
          return;
        }
        break;
      }
      case OP_WAIT_COUNT:
      case OP_WAIT_TIME:
      case OP_WAIT_TEXTFEED:
      case OP_DELAY: {
        int frames = ins.paramBytes.size() >= 2 ? int(paramU16(ins, 0)) : 0;
        const uint64_t ms = skipMode ? 0 : framesToMs(frames);
        if (ms > 0) {
          waiting = true;
          waitDeadline = storyNow() + ms;
          return;
        }
        break;
      }

      case OP_FFSTOP:
        skipMode = false;
        break;

      case OP_INIT: {
        if (ins.paramBytes.size() >= 2) {
          const uint8_t group = ins.paramBytes[0];
          const uint8_t slot = ins.paramBytes[1];
          if ((group == 0xff && slot == 0x00) ||
              (group == 0x00 && slot == 0xff)) {
            stageResets.clearGroup(0);
            for (auto it = stagedSlots.begin(); it != stagedSlots.end();) {
              if (it->second.group == 0)
                it = stagedSlots.erase(it);
              else
                ++it;
            }
          } else if (group == 0xff && slot == 0xff) {
            stageResets.clearAll();
            stagedSlots.clear();
          } else if (slot == 0xff) {
            stageResets.clearGroup(group);
            for (auto it = stagedSlots.begin(); it != stagedSlots.end();) {
              if (it->second.group == group)
                it = stagedSlots.erase(it);
              else
                ++it;
            }
          } else {
            stageResets.clearSlot(group, slot);
            stagedSlots.erase(makeKey(group, slot));
            if (group == 2)
              stagedSlots.erase(makeKey(3, slot));
          }
        }
        break;
      }

      case OP_STOP: {
        activeShakeId = 0;
        break;
      }

      case OP_IMAGELOAD: {
        if (ins.paramBytes.size() == 4) {
          const uint8_t group = ins.paramBytes[0];
          const uint8_t slot = ins.paramBytes[1];
          const uint16_t assetId = paramU16(ins, 2);
          stageImage(group, slot, assetId, 0.0f, 0.0f, false);
        } else if (ins.paramBytes.size() >= 14) {
          const uint8_t group = ins.paramBytes[0];
          const uint8_t slot = ins.paramBytes[1];
          const uint16_t assetId = paramU16(ins, 2);
          const float posX = paramFloat(ins, 6);
          const float posY = paramFloat(ins, 10);
          stageImage(group, slot, assetId, posX, posY);
        } else if (ins.paramBytes.size() >= 6) {
          const uint32_t id = paramU16(ins, 0);
          const float x = float(int16_t(paramU16(ins, 2)));
          const float y = float(int16_t(paramU16(ins, 4)));
          stageImage(0, 0, id, x, y);
        }
        break;
      }

      case OP_FADE: {
        // Layer opacity setter: paramBytes = [group, targetSlot, alphaByte]
        if (ins.paramBytes.size() >= 3) {
          const uint8_t group = ins.paramBytes[0];
          const uint8_t slot = ins.paramBytes[1];
          const float alpha = float(ins.paramBytes[2]) / 255.0f;
          auto it = stagedSlots.find(makeKey(group, slot));
          if (it != stagedSlots.end()) {
            it->second.alpha = alpha;
            if (group == 2) {
              auto fragment = stagedSlots.find(makeKey(3, slot));
              if (fragment != stagedSlots.end())
                fragment->second.alpha = alpha;
            }
          } else if (group == 0 && slot >= 1) {
            auto charIt = stagedSlots.find(makeKey(2, slot - 1));
            if (charIt != stagedSlots.end())
              charIt->second.alpha = alpha;
            auto fragment = stagedSlots.find(makeKey(3, slot - 1));
            if (fragment != stagedSlots.end())
              fragment->second.alpha = alpha;
          }
        }
        break;
      }

      case OP_DRAW: {
        int frames = 0;
        uint16_t mode = 0;
        if (ins.paramBytes.size() >= 2)
          frames = int(paramU16(ins, 0));
        if (ins.paramBytes.size() >= 4)
          mode = paramU16(ins, 2);
        const uint64_t now = storyNow();
        const uint64_t duration = skipMode ? 0 : framesToMs(frames);
        commitStagedSlots(now, duration, mode == 3);
        lastDrawDeadline = now + duration;
        if (duration > 0) {
          waiting = true;
          waitDeadline = lastDrawDeadline;
          return;
        }
        break;
      }

      case OP_WIPE: {
        const int frames =
            ins.paramBytes.size() >= 2 ? int(paramU16(ins, 0)) : 18;
        const uint64_t now = storyNow();
        const uint64_t ms = skipMode ? 0 : framesToMs(frames);
        commitStagedSlots(now, ms, true);
        lastDrawDeadline = now + ms;
        if (ms > 0) {
          waiting = true;
          waitDeadline = lastDrawDeadline;
          return;
        }
        break;
      }

      case OP_MOVE: {
        if (ins.paramBytes.size() >= 14) {
          const uint8_t group = ins.paramBytes[0];
          const uint8_t slot = ins.paramBytes[1];
          const int frames = int(paramU16(ins, 4));
          const float dx = paramFloat(ins, 6);
          const float dy = paramFloat(ins, 10);
          const uint64_t now = storyNow();
          const uint64_t dur = skipMode ? 0 : framesToMs(frames);
          const auto applyMove = [&](SlottedLayer &l) {
            // A new MOVE starts at the *current displayed position*, not at
            // the previous animation's future target.
            const float x = airMoveOffset(l.startOffsetX, l.targetOffsetX,
                                          l.moveStart, l.moveDuration, now);
            const float y = airMoveOffset(l.startOffsetY, l.targetOffsetY,
                                          l.moveStart, l.moveDuration, now);
            l.startOffsetX = x;
            l.startOffsetY = y;
            l.targetOffsetX = x + dx;
            l.targetOffsetY = y + dy;
            l.moveStart = now;
            l.moveDuration = dur;
          };
          auto sit = stagedSlots.find(makeKey(group, slot));
          if (sit != stagedSlots.end())
            applyMove(sit->second);
          auto vit = visibleSlots.find(makeKey(group, slot));
          if (vit != visibleSlots.end())
            applyMove(vit->second);
          if (group == 2) {
            auto fragment = stagedSlots.find(makeKey(3, slot));
            if (fragment != stagedSlots.end())
              applyMove(fragment->second);
            fragment = visibleSlots.find(makeKey(3, slot));
            if (fragment != visibleSlots.end())
              applyMove(fragment->second);
          }
          if (dur > 0)
            lastDrawDeadline = std::max(lastDrawDeadline, now + dur);
        }
        break;
      }
      case OP_MOVE_SKIP:
        break;

      case OP_SHAKELIST: {
        if (ins.paramBytes.size() >= 6) {
          activeShakeTarget = ins.paramBytes[1];
          activeShakeId = paramU16(ins, 4);
          shakeStartTick = storyNow();
        }
        break;
      }

      case OP_HAIKEI_SET: {
        const uint32_t id =
            ins.paramBytes.size() >= 2 ? paramU16(ins, 0) : 0;
        if (id > 0) {
          // Use exactly the same validated scene staging path as IMAGELOAD.
          // A missing or corrupt background must not erase the last scene.
          stageImage(0, 0, id, 0.0f, 0.0f);
        }
        break;
      }

      case OP_EQUN: {
        if (ins.paramBytes.size() == 4) {
          const uint16_t varId = paramU16(ins, 0);
          const uint16_t rawU16 = paramU16(ins, 2);
          const int32_t val =
              (varId == 8001 || varId == 8002)
                  ? static_cast<int32_t>(rawU16)
                  : static_cast<int32_t>(static_cast<int16_t>(rawU16));
          setVar(varId, val);
        } else if (ins.paramBytes.size() >= 8) {
          const uint32_t varId = paramU32(ins, 0);
          const int32_t val = static_cast<int32_t>(paramU32(ins, 4));
          setVar(varId, val);
        }
        break;
      }

      case OP_EQU: {
        if (ins.paramBytes.size() >= 4) {
          const uint16_t dstVarId = paramU16(ins, 0);
          size_t cursor = 2;
          const std::string rhs = ReadLsUtf8(ins.paramBytes, cursor);
          if (!rhs.empty())
            setVar(dstVarId, evaluateExpr(rhs));
        }
        break;
      }

      case OP_EQUV: {
        if (ins.paramBytes.size() >= 4) {
          const uint16_t dst = paramU16(ins, 0);
          const uint16_t src = paramU16(ins, 2);
          setVar(dst, getVar(src));
        }
        break;
      }

      case OP_ADD: {
        if (ins.paramBytes.size() == 4) {
          const uint16_t id = paramU16(ins, 0);
          setVar(id, getVar(id) + int16_t(paramU16(ins, 2)));
        } else if (ins.paramBytes.size() >= 8) {
          const uint32_t id = paramU32(ins, 0);
          setVar(id, getVar(id) + int32_t(paramU32(ins, 4)));
        }
        break;
      }
      case OP_SUB: {
        if (ins.paramBytes.size() == 4) {
          const uint16_t id = paramU16(ins, 0);
          setVar(id, getVar(id) - int16_t(paramU16(ins, 2)));
        } else if (ins.paramBytes.size() >= 8) {
          const uint32_t id = paramU32(ins, 0);
          setVar(id, getVar(id) - int32_t(paramU32(ins, 4)));
        }
        break;
      }
      case OP_MUL: {
        if (ins.paramBytes.size() >= 4) {
          const uint16_t id = paramU16(ins, 0);
          setVar(id, getVar(id) * int16_t(paramU16(ins, 2)));
        }
        break;
      }
      case OP_DIV: {
        if (ins.paramBytes.size() >= 4) {
          const uint16_t id = paramU16(ins, 0);
          const int32_t v = int16_t(paramU16(ins, 2));
          if (v != 0)
            setVar(id, getVar(id) / v);
        }
        break;
      }
      case OP_MOD: {
        if (ins.paramBytes.size() >= 4) {
          const uint16_t id = paramU16(ins, 0);
          const int32_t v = int16_t(paramU16(ins, 2));
          if (v != 0)
            setVar(id, getVar(id) % v);
        }
        break;
      }
      case OP_RANDOM: {
        if (ins.paramBytes.size() >= 4) {
          const uint16_t id = paramU16(ins, 0);
          const int32_t mod = int16_t(paramU16(ins, 2));
          setVar(id, std::rand() % (mod > 0 ? mod : 100));
        }
        break;
      }
      case OP_SET: {
        if (ins.paramBytes.size() >= 4) {
          setVar(paramU16(ins, 0), int16_t(paramU16(ins, 2)));
        }
        break;
      }
      case OP_FLAGCLR: {
        if (ins.paramBytes.size() >= 2)
          flags[paramU16(ins, 0)] = false;
        break;
      }
      case OP_ARFLAGSET: {
        if (ins.paramBytes.size() >= 2)
          flags[paramU16(ins, 0)] = true;
        break;
      }
      case OP_VARSTR: {
        // AIR layout: destination string variable in the fixed parameter,
        // table id as the final uint16 of the six-byte payload.
        if (ins.paramBytes.size() >= 6) {
          const uint16_t dstStrVar =
              ins.fixedParam.size() >= 2
                  ? static_cast<uint16_t>(ins.fixedParam[0]) |
                        (static_cast<uint16_t>(ins.fixedParam[1]) << 8)
                  : paramU16(ins, 0);
          const uint16_t tableId = paramU16(ins, 4);
          auto it = varStrTable.find({currentScenario, tableId});
          if (it != varStrTable.end())
            stringVars[dstStrVar] = it->second;
        }
        break;
      }
      case OP_VARSTR_SET: {
        // Same table layout as _varstr: register the entry for later
        // OP_VARSTR lookups instead of silently ignoring the opcode.
        if (ins.paramBytes.size() >= 6) {
          size_t cursor = 2;
          const std::string scen = ReadLsCString(ins.paramBytes, cursor);
          if (cursor + 2 <= ins.paramBytes.size()) {
            const uint16_t strId = paramU16(ins, cursor);
            cursor += 2;
            const std::string jp = ReadLsUtf16(ins.paramBytes, cursor);
            const std::string en = ReadLsUtf16(ins.paramBytes, cursor);
            const std::string zh = ReadLsUtf16(ins.paramBytes, cursor);
            const std::string &chosen =
                !en.empty() ? en : (!jp.empty() ? jp : zh);
            if (!scen.empty() && !chosen.empty())
              varStrTable[{scen, strId}] = chosen;
          }
        }
        break;
      }

      case OP_SELECT: {
        uint16_t targetVarId = 6001;
        std::vector<std::string> options;
        if (ins.paramBytes.size() >= 10) {
          targetVarId = paramU16(ins, 0);
          if (targetVarId == 0)
            targetVarId = 6001;
          size_t cursor = 8;
          std::string jp = ReadLsUtf16(ins.paramBytes, cursor);
          std::string en = ReadLsUtf16(ins.paramBytes, cursor);
          std::string zh = ReadLsUtf16(ins.paramBytes, cursor);
          const std::string &chosen =
              !en.empty() ? en : (!jp.empty() ? jp : zh);
          for (auto &opt : splitDelimiter(chosen, "$d")) {
            std::string spk, clean;
            parseSpeakerAndSanitizeText(opt, spk, clean);
            if (!clean.empty())
              options.push_back(std::move(clean));
          }
        }
        if (options.empty()) {
          for (auto &st : ExtractShiftJisStrings(std::string_view(
                   reinterpret_cast<const char *>(ins.paramBytes.data()),
                   ins.paramBytes.size()))) {
            if (st.size() > 1)
              options.push_back(std::move(st));
          }
        }
        if (options.empty())
          options = {"Option 1", "Option 2"};

        skipMode = false;
        choiceActive = true;
        choiceTargetVarId = targetVarId;
        currentChoiceOptions = options;
        dialogueVisible = false;
        if (textbox)
          textbox->hide();
        if (choiceScene) {
          std::vector<Kotonoha::ChoiceOption> choices;
          choices.reserve(options.size());
          for (size_t i = 0; i < options.size(); ++i)
            choices.push_back({options[i], int(i)});
          choiceScene->setQuestion(
              "", choices,
              [this, targetVarId](int optionId, const std::string &label) {
                playSysSe("ENTER");
                if (backlog && !label.empty())
                  backlog->addEntry(">", label);
                choiceActive = false;
                currentChoiceOptions.clear();
                setVar(targetVarId, optionId);
                if (choiceScene)
                  choiceScene->hide();
                runUntilPause();
              });
        }
        return;
      }

      case OP_BGM: {
        if (ins.paramBytes.empty()) {
          playBgm(0, false);
        } else if (ins.paramBytes.size() >= 4) {
          const uint32_t id = paramU32(ins, 0);
          const bool loop =
              ins.paramBytes.size() >= 7 ? (ins.paramBytes[6] == 0) : true;
          playBgm(id, loop);
        } else if (ins.paramBytes.size() >= 2) {
          playBgm(paramU16(ins, 0), true);
        }
        break;
      }
      case OP_BGM_ASYNC_FADE_STOP: {
        const uint64_t fadeMs = ins.paramBytes.size() >= 2
                                    ? framesToMs(int(paramU16(ins, 0)))
                                    : 2000;
        startBgmFadeOut(fadeMs);
        break;
      }
      case OP_BGM_WAIT_START:
      case OP_BGM_WAIT_FADE:
      case OP_BGM_PUSH:
      case OP_BGM_POP:
        break;

      case OP_VOICE: {
        if (ins.paramBytes.size() >= 2)
          playVoice(paramU16(ins, 0));
        break;
      }
      case OP_VOICE_STOP:
        voice.Stop();
        break;

      case OP_SE: {
        if (ins.paramBytes.size() >= 5) {
          const uint32_t seId = paramU32(ins, 1);
          const bool loop =
              (ins.paramBytes.size() >= 6 && ins.paramBytes[5] == 2);
          // Byte zero selects the script mixer channel. Ambient loops on
          // channel zero and transient effects on other slots can coexist.
          playSe(scriptSe[ins.paramBytes[0]], seId, loop);
        } else if (ins.paramBytes.size() >= 2) {
          playSe(scriptSe[0], paramU16(ins, 0), false);
        } else {
          scriptSe.clear();
        }
        break;
      }
      case OP_SE_STOP:
      case OP_SE_WAIT:
      case OP_SE_WAIT_COUNT:
      case OP_SE_WAIT_FADE:
        if (ins.opcode == OP_SE_STOP) {
          if (ins.paramBytes.size() >= 1 && ins.paramBytes[0] != 0xFF)
            scriptSe.erase(ins.paramBytes[0]);
          else
            scriptSe.clear();
        }
        break;

      case OP_SETBGMFLAG: {
        if (ins.paramBytes.size() >= 4) {
          const uint32_t bgmId = paramU32(ins, 0);
          if (bgmId >= 100001 && bgmId <= 100100)
            globalVars[15000 + (bgmId - 100000)] = 1;
        }
        break;
      }
      case OP_SETCGFLAG:
      case OP_TROPHY:
        break;

      case OP_TASK: {
        if (ins.paramBytes.size() >= 2) {
          const uint16_t taskId = paramU16(ins, 0);
          if (taskId == 6 || taskId == 8) {
            // task::Flash / task::Flash_CHAR ("siro" = white, "aka" = red)
            size_t cursor = 2;
            const std::string colorName = ReadLsUtf8(ins.paramBytes, cursor);
            const uint16_t frames = cursor + 2 <= ins.paramBytes.size()
                                        ? paramU16(ins, cursor)
                                        : 12;
            flashColor = (colorName == "aka") ? SDL_Color{255, 32, 32, 255}
                                              : SDL_Color{255, 255, 255, 255};
            flashStartTick = storyNow();
            flashDurationMs = std::max<uint64_t>(120, framesToMs(frames));
          } else if (taskId == 3 || taskId == 4) {
            // task::EndRoll995 / task::EndRoll900 ending staff roll
            skipMode = false;
            if (!skipMode) {
              waiting = true;
              waitDeadline = storyNow() + 1500;
              return;
            }
          }
        }
        break;
      }

      case OP_GOTO:
        jumpFrom(ins);
        break;

      case OP_ONGOTO: {
        size_t cursor = 0;
        std::string expr = ins.text;
        if (expr.empty() && ins.paramBytes.size() >= 4)
          expr = ReadLsUtf8(ins.paramBytes, cursor);
        const int idx = evaluateExpr(expr);
        if (cursor + 2 <= ins.paramBytes.size()) {
          const uint16_t count = paramU16(ins, cursor);
          cursor += 2;
          if (count > 0 && idx >= 0 && idx < int(count) &&
              cursor + size_t(count) * 4 <= ins.paramBytes.size()) {
            const uint32_t target = paramU32(ins, cursor + size_t(idx) * 4);
            if (const auto targetIdx =
                    FindInstructionAtOffset(instructions, target)) {
              instructionIndex = *targetIdx;
            }
          }
        }
        break;
      }

      case OP_CALL: {
        if (callStack.size() >= 1024)
          throw std::runtime_error("LucaSystem: call stack limit exceeded");
        const auto next = instructionIndex;
        if (jumpFrom(ins))
          callStack.push_back({currentScenario, next});
        break;
      }

      case OP_FARCALL: {
        if (callStack.size() >= 1024)
          throw std::runtime_error("LucaSystem: call stack limit exceeded");
        const auto prevScenario = currentScenario;
        const auto next = instructionIndex;
        size_t cursor = 0;
        std::string targetScenario =
            !ins.text.empty() ? ins.text : ReadLsUtf8(ins.paramBytes, cursor);
        if (!targetScenario.empty() && loadScenarioScript(targetScenario)) {
          callStack.push_back({prevScenario, next});
          if (cursor + 4 <= ins.paramBytes.size()) {
            if (const auto idx = FindInstructionAtOffset(instructions,
                                                         paramU32(ins, cursor)))
              instructionIndex = *idx;
          }
        } else if (jumpFrom(ins)) {
          callStack.push_back({prevScenario, next});
        }
        break;
      }

      case OP_JUMP: {
        size_t cursor = 0;
        std::string targetScenario =
            !ins.text.empty() ? ins.text : ReadLsUtf8(ins.paramBytes, cursor);
        if (!targetScenario.empty() && loadScenarioScript(targetScenario)) {
          if (cursor + 4 <= ins.paramBytes.size()) {
            if (const auto idx = FindInstructionAtOffset(instructions,
                                                         paramU32(ins, cursor)))
              instructionIndex = *idx;
          }
        } else {
          jumpFrom(ins);
        }
        break;
      }

      case OP_RETURN:
      case OP_FARRETURN: {
        if (!callStack.empty()) {
          auto frame = callStack.back();
          callStack.pop_back();
          if (frame.scenario != currentScenario)
            loadScenarioScript(frame.scenario);
          instructionIndex = frame.instructionIndex;
        }
        break;
      }

      case OP_IFY:
      case OP_IFN: {
        size_t cursor = 0;
        std::string expr = ins.text;
        if (expr.empty() && ins.paramBytes.size() >= 2)
          expr = ReadLsUtf8(ins.paramBytes, cursor);
        const bool cond = evaluateCondition(expr);
        const bool shouldJump = (ins.opcode == OP_IFY) ? cond : !cond;
        if (shouldJump) {
          if (cursor + 4 <= ins.paramBytes.size()) {
            const uint32_t target = paramU32(ins, cursor);
            if (const auto idx = FindInstructionAtOffset(instructions, target))
              instructionIndex = *idx;
          } else {
            jumpFrom(ins);
          }
        }
        break;
      }

      case OP_STARTUP_BEGIN:
      case OP_STARTUP_END:
      case OP_JUMPPOINT:
      case OP_SCENARIO:
      case OP_MANPU:
      case OP_SHAKE:
      case OP_QUAKE:
      case OP_BASE:
      case OP_VOLUME:
      case OP_SEPIA:
      case OP_EX:
      case OP_IMAGEUPDATE:
      case OP_SCENE_REPLAY_END:
        break;

      case OP_END: {
        commitStagedSlots(storyNow(), 0, false);
        saveGlobalState();
        if (!callStack.empty()) {
          auto frame = callStack.back();
          callStack.pop_back();
          if (frame.scenario != currentScenario)
            loadScenarioScript(frame.scenario);
          instructionIndex = frame.instructionIndex;
        } else {
          running = false;
          if (engine)
            enterTitleScreen();
          return;
        }
        break;
      }

      default:
        break;
      }
    }
    if (instructionIndex >= instructions.size()) {
      commitStagedSlots(storyNow(), 0, false);
      saveGlobalState();
      running = false;
    }
  }

  bool jumpFrom(const ScriptInstruction &ins) {
    if (ins.paramBytes.size() >= 4) {
      if (const auto index =
              FindInstructionAtOffset(instructions, paramU32(ins, 0))) {
        instructionIndex = *index;
        return true;
      }
    }
    const auto end = std::find(ins.paramBytes.begin(), ins.paramBytes.end(), 0);
    const std::string_view label(
        reinterpret_cast<const char *>(ins.paramBytes.data()),
        static_cast<std::size_t>(end - ins.paramBytes.begin()));
    if (const auto index = FindJumpPoint(instructions, label)) {
      instructionIndex = *index;
      return true;
    }
    SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                "LucaSystem: unresolved jump opcode=%u at offset=%llu",
                unsigned(ins.opcode), static_cast<unsigned long long>(ins.pos));
    return false;
  }

  void advanceDialogue() {
    if (!textbox) {
      if (dialogueVisible) {
        dialogueVisible = false;
        ++dialogueIndex;
        runtime.dialogueText_.clear();
        runtime.dialogueSpeaker_.clear();
        runUntilPause();
      }
      return;
    }
    switch (
        nextDialogueAction(dialogueVisible, textbox->isDialogueComplete())) {
    case DialogueAdvance::None:
      return;
    case DialogueAdvance::Reveal:
      textbox->completeDialogue();
      return;
    case DialogueAdvance::ResumeScript:
      dialogueVisible = false;
      textbox->hide();
      ++dialogueIndex;
      runtime.dialogueText_.clear();
      runtime.dialogueSpeaker_.clear();
      runUntilPause();
      return;
    }
  }

  void attachGameplayScenes() {
    runtime.SetState(Kotonoha::GameState::Gameplay);
    runtime.ClearScenes();
    runtime.AddScene(textbox.get(), Kotonoha::GameState::Gameplay);
    runtime.AddScene(choiceScene, Kotonoha::GameState::Gameplay);
    runtime.AddScene(backlog, Kotonoha::GameState::Gameplay);
    runtime.AddScene(menu, Kotonoha::GameState::Gameplay);
  }

  std::vector<std::uint8_t> captureSaveThumbnail() {
    using Kotonoha::VNSave;
    if (!engine || originalTitle)
      return {};
    auto &renderer = engine->GetRenderer();
    Kotonoha::TexturePtr canvas{SDL_CreateTexture(
        &renderer, SDL_PIXELFORMAT_RGBA8888, SDL_TEXTUREACCESS_TARGET,
        VNSave::thumbnailWidth, VNSave::thumbnailHeight)};
    if (!canvas)
      return {};
    auto *previous = SDL_GetRenderTarget(&renderer);
    if (!SDL_SetRenderTarget(&renderer, canvas.get()))
      return {};
    struct RestoreTarget {
      SDL_Renderer *renderer;
      SDL_Texture *previous;
      SDL_Color color{};
      SDL_BlendMode blend = SDL_BLENDMODE_NONE;
      RestoreTarget(SDL_Renderer *renderer, SDL_Texture *previous)
          : renderer(renderer), previous(previous) {
        SDL_GetRenderDrawColor(renderer, &color.r, &color.g, &color.b,
                               &color.a);
        SDL_GetRenderDrawBlendMode(renderer, &blend);
      }
      ~RestoreTarget() {
        SDL_SetRenderTarget(renderer, previous);
        SDL_SetRenderDrawColor(renderer, color.r, color.g, color.b, color.a);
        SDL_SetRenderDrawBlendMode(renderer, blend);
      }
    } restore(&renderer, previous);
    if (!SDL_SetRenderLogicalPresentation(&renderer, 1280, 720,
                                          SDL_LOGICAL_PRESENTATION_LETTERBOX))
      return {};
    SDL_SetRenderDrawColor(&renderer, 0, 0, 0, 255);
    SDL_RenderClear(&renderer);
    // Draw only the story, not the Save, pause or name-entry overlays.
    runtime.renderGameplay(renderer, 1);
    Kotonoha::SurfacePtr pixels{SDL_RenderReadPixels(&renderer, nullptr)};
    Kotonoha::SurfacePtr image{
        pixels ? SDL_ConvertSurface(pixels.get(), SDL_PIXELFORMAT_RGB24)
               : nullptr};
    if (!image || image->w != VNSave::thumbnailWidth ||
        image->h != VNSave::thumbnailHeight)
      return {};
    std::vector<std::uint8_t> rgb(VNSave::thumbnailBytes);
    constexpr int rowBytes = VNSave::thumbnailWidth * 3;
    for (int row = 0; row < image->h; ++row)
      std::memcpy(rgb.data() + row * rowBytes,
                  static_cast<const std::uint8_t *>(image->pixels) +
                      row * image->pitch,
                  rowBytes);
    return rgb;
  }

  void launchChapterFromTitle(const std::string &scenarioName) {
    if (scenarioName.empty())
      return;
    storyOpen = false;
    originalTitle = false;
    titleShown = false;
    titleBackgroundTexture.reset();
    bgm.Stop();
    se.Stop();
    currentBgmId = 0;
    attachGameplayScenes();
    runtime.startScenario(scenarioName);
  }
};

LucaSystemRuntime::LucaSystemRuntime(
    std::shared_ptr<Kotonoha::Providers::LucaSystemAssets> assets)
    : Runtime("LucaSystem", "0.1.0", "me.hirameki.kotonoha.lucasystem"),
      impl_(std::make_unique<Impl>(*this, std::move(assets))),
      assets_(impl_->assets) {}

LucaSystemRuntime::~LucaSystemRuntime() = default;

void LucaSystemRuntime::shutdown() {
  if (!impl_)
    return;
  auto &impl = *impl_;
  impl.stopAudio();
  impl.moviePlayer.reset();
  ClearScenes();
  impl.textbox.reset();
  impl.saveMenu.reset();
  impl.gallery.reset();
  impl.choiceScene = nullptr;
  impl.backlog = nullptr;
  impl.menu = nullptr;
  impl.titleBackgroundTexture.reset();
  impl.objectTextures.clear();
  impl.objectImages.clear();
  impl.objectHasTransparency.clear();
  impl.failedImageRetryAt.clear();
  impl.backgroundTexture.reset();
  impl.splashLogos.clear();
  if (impl.assets)
    impl.assets->clearCache();
}

void LucaSystemRuntime::init(Kotonoha::Engine &engine) {
  auto &impl = *impl_;
  impl.engine = &engine;
  Runtime::init(engine);
}

void LucaSystemRuntime::initGlobal(Kotonoha::Engine &engine) {
  auto &impl = *impl_;
  engine.GetSettingsMenu().setSkin(
      std::make_unique<AirSettingsSkin>(impl.assets));
  impl.backlog = &engine.configureSharedScene<Kotonoha::VNBacklog, AirBacklog>(
      *impl.assets);
  impl.textbox = std::make_unique<AirTextBox>(engine, *impl.assets);
  impl.textbox->setControlAction(
      [this](AirTextBox::Control control) { impl_->dialogueControl(control); });
  impl.choiceScene = &engine.configureSharedScene<Kotonoha::ChoiceQuestionScene,
                                                  AirChoiceScene>(*impl.assets);
  impl.backlog->setVoiceReplay([this](const std::string &id) {
    try {
      impl_->voice.Stop();
      impl_->playDecodedAudio(impl_->voice, "Voice", id, 1, 48000, false);
    } catch (const std::exception &e) {
      SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "LucaSystem replay: %s",
                  e.what());
    }
  });
  impl.menu = &engine.configureSharedScene<Kotonoha::InGameMenu, AirPauseMenu>(
      *impl.assets);
  impl.saveMenu = std::make_unique<AirSaveMenu>(engine, *impl.assets);
  impl.gallery = std::make_unique<AirGallery>(
      engine, *impl.assets, impl.galleryCatalog,
      [this](uint32_t id) { return impl_->cgUnlocked(id); });

  auto &menu = *impl.menu;
  menu.SetOnReturnToMenu([this] { impl_->enterTitleScreen(); });
  menu.SetOnSave([this] { impl_->openSaveSlots(false); });
  menu.SetOnLoad([this] { impl_->openSaveSlots(true); });
  menu.setQuickAction([this](bool load) {
    const auto path = Kotonoha::saveDirectory() / "quicksave.kns";
    try {
      if (load) {
        if (!std::filesystem::exists(path))
          return;
        loadGame(path.string());
      } else {
        saveGame(path.string());
      }
      impl_->menu->close();
    } catch (const std::exception &e) {
      SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                  "LucaSystem quick save/load: %s", e.what());
    }
  });
}

void LucaSystemRuntime::initSplash(Kotonoha::Engine &engine) {
  auto &impl = *impl_;
  impl.setupSplashSequence();
  SetState(Kotonoha::GameState::Gameplay);
  initGameplay(engine);
}

void LucaSystemRuntime::initMenu(Kotonoha::Engine &engine) {
  (void)engine;
  auto &impl = *impl_;
  impl.menu->open();
  impl.menu->SetOnResume([this] { SetState(Kotonoha::GameState::Gameplay); });
  AddScene(impl.menu);
}

void LucaSystemRuntime::initGameplay(Kotonoha::Engine &engine) {
  (void)engine;
  auto &impl = *impl_;
  if (impl.backlog) {
    impl.backlog->clear();
    impl.backlog->close();
  }

  if (impl.originalTitle) {
    impl.enterTitleScreen();
    return;
  }

  impl.attachGameplayScenes();
  if (!impl.currentScenario.empty()) {
    startScenario(impl.currentScenario);
  } else {
    const auto startup = impl.startupScenario();
    if (!startup.empty())
      startScenario(startup);
  }
}

void LucaSystemRuntime::update() {
  Runtime::update();
  auto &impl = *impl_;
  const bool paused = isStoryPaused();
  impl.syncStoryPause(paused);
  const auto splashDuration =
      impl.currentSplashIndex == 0 ? Impl::noticeDuration : Impl::logoDuration;
  if (impl.showingSplash &&
      SDL_GetTicks() - impl.splashStartTime >= splashDuration)
    impl.advanceSplash();
  if (paused)
    return;
  if (impl.drawDurationMs > 0 &&
      impl.storyNow() >= impl.drawStartTick + impl.drawDurationMs) {
    // Keep both compositions until the transition's final frame has
    // elapsed. Once it is complete, the outgoing snapshot is disposable.
    impl.outgoingSlots.clear();
    impl.drawDurationMs = 0;
    impl.drawWipeMode = false;
    impl.pruneObjectCaches();
  }
  impl.tickBgmFade();
  if (impl.moviePlayer && !impl.movieFinished) {
    if (impl.engine)
      impl.moviePlayer->update(impl.engine->GetRenderer());
    if (impl.moviePlayer->finished()) {
      impl.movieFinished = true;
      impl.moviePlayer.reset();
      impl.waiting = false;
      impl.runUntilPause();
    }
  }
  if (impl.skipMode && impl.dialogueVisible && !impl.choiceActive) {
    if (impl.textbox)
      impl.textbox->completeDialogue();
    impl.advanceDialogue();
  }
  if (impl.autoMode && impl.dialogueVisible && !impl.choiceActive &&
      impl.textbox && impl.textbox->isVisible() &&
      impl.textbox->isDialogueComplete() && !impl.voice.Playing()) {
    const auto now = impl.storyNow();
    if (!impl.autoAdvanceAt)
      impl.autoAdvanceAt = now + 1000;
    else if (now >= impl.autoAdvanceAt) {
      impl.autoAdvanceAt = 0;
      impl.advanceDialogue();
    }
  } else {
    impl.autoAdvanceAt = 0;
  }
  if (impl.engine && impl.running && !impl.dialogueVisible &&
      !impl.choiceActive && !impl.waiting) {
    impl.runUntilPause();
  }
  if (impl.waiting && !impl.moviePlayer) {
    if (impl.storyNow() >= impl.waitDeadline) {
      impl.waiting = false;
      impl.runUntilPause();
    }
  }
}

void LucaSystemRuntime::renderGameplay(Kotonoha::RendererRef renderer,
                                       float scale) {
  auto &impl = *impl_;
  const uint64_t now = impl.storyNow();

  if (impl.showingSplash) {
    const uint64_t elapsed = SDL_GetTicks() - impl.splashStartTime;
    const SDL_FRect screen{0, 0, 1280, 720};
    SDL_SetRenderDrawColor(&renderer, 255, 255, 255, 255);
    SDL_RenderFillRect(&renderer, &screen);
    if (impl.currentSplashIndex == 0) {
      // The Windows release draws text here, not an image from SYSCG.PAK.
      // Recreate the notice in native UI without bundling its executable.
      auto &ui = impl.engine->GetUI();
      const SDL_Color ink{12, 12, 12, 255};
      static constexpr const char *paragraphs[] = {
          "This is a work of fiction.\n"
          "The characters, locations and organizations in this story are "
          "fictional.",
          "This product is owned by VISUAL ARTS Co., Ltd.\n"
          "Unauthorized lending, copying, modification and distribution, "
          "including via the Internet or peer-to-peer software, are "
          "prohibited without written permission.",
          "Do not upload music, images or recordings from this game without "
          "permission, except for authorized demos. Unauthorized uploads "
          "may be subject to local law.",
      };
      float y = 78;
      for (const char *paragraph : paragraphs) {
        const float height =
            ui.textSized(renderer, paragraph, 62, y, 1156, 26, ink);
        y += height + 26;
      }
    } else {
      auto &logo = impl.splashLogos[impl.currentSplashIndex - 1];
      if (!logo.texture && !logo.image.rgba.empty()) {
        logo.texture.reset(uploadAirStaticRgba(
            renderer, logo.image.width, logo.image.height,
            logo.image.rgba.data(), logo.image.width * 4,
            SDL_SCALEMODE_LINEAR));
      }
      const auto logoElapsed = std::min(elapsed, Impl::logoDuration);
      float alpha = 1.0f;
      if (logoElapsed < 350)
        alpha = float(logoElapsed) / 350.0f;
      else if (logoElapsed > Impl::logoDuration - 350)
        alpha = float(Impl::logoDuration - logoElapsed) / 350.0f;
      alpha = std::clamp(alpha, 0.0f, 1.0f);

      if (logo.texture) {
        SDL_SetTextureAlphaMod(logo.texture.get(),
                               static_cast<Uint8>(std::lround(alpha * 255.0f)));
        Kotonoha::RenderUpscaledTexture(&renderer, logo.texture.get(), nullptr,
                                        &screen);
      }
    }
    return;
  }

  if (impl.moviePlayer && !impl.movieFinished && impl.moviePlayer->isOpen()) {
    SDL_FRect screen{0, 0, 1280, 720};
    impl.moviePlayer->render(renderer, screen, nullptr, true);
    return;
  }

  if (impl.originalTitle) {
    auto &assets = *impl.assets;
    const SDL_FRect screen{0, 0, 1280, 720};
    if (impl.storyOpen) {
      if (!drawAirArt(assets, renderer, "scenario_bg", screen)) {
        SDL_SetRenderDrawColor(&renderer, 248, 249, 252, 255);
        SDL_RenderFillRect(&renderer, &screen);
      }
      drawAirArt(assets, renderer, "scenario_bg_hat", {452, 473, 376, 216});
      for (int i = 0; i < impl.storyCount(); ++i) {
        const auto &item = storyItems[i];
        const SDL_FRect src{
            float(item.atlasX),
            impl.storyHighlight && i == impl.storySelection ? 72.0f : 0.0f,
            float(item.width), 72};
        drawAirArt(assets, renderer, "scenario_menu_en", impl.storyRect(i),
                   &src);
      }
      auto &ui = impl.engine->GetUI();
      ui.panel(renderer, storyClose, {82, 105, 178, 255});
      ui.text(renderer, "[X] Close", storyClose.x + 10, storyClose.y + 5,
              storyClose.w - 20);
      return;
    }
    auto texture = [&renderer](Kotonoha::TexturePtr &target,
                               const CzImage &image) -> SDL_Texture * {
      if (image.rgba.empty())
        return nullptr;
      if (!target) {
        target.reset(uploadAirStaticRgba(
            renderer, image.width, image.height, image.rgba.data(),
            image.width * 4, SDL_SCALEMODE_LINEAR));
        if (!target)
          return nullptr;
      }
      return target.get();
    };
    if (auto *bgTex =
            texture(impl.titleBackgroundTexture, impl.titleBackground)) {
      Kotonoha::RenderUpscaledTexture(&renderer, bgTex, nullptr, &screen);
    }
    const float offset = impl.titleOffset();
    for (int i = 0; i < int(std::size(titleItems)); ++i) {
      const auto &item = titleItems[i];
      const auto dst = impl.titleRect(i, offset);
      if (dst.x + dst.w < 0 || dst.x >= 1280)
        continue;
      const SDL_FRect src{
          float(item.atlasX),
          impl.titleHighlight && i == impl.titleSelection ? 70.0f : 0.0f,
          float(item.width), 70};
      drawAirArt(assets, renderer, "title_menu", dst, &src);
    }
    const SDL_FRect arrowSrc{impl.titlePage ? 0.0f : 60.0f, 0, 60, 54};
    drawAirArt(assets, renderer, "title_arrow",
               {impl.titlePage ? 66.0f : 1150.0f, 548, 60, 54}, &arrowSrc);
    Runtime::renderGameplay(renderer, scale);
    return;
  }

  SDL_SetRenderDrawColor(&renderer, 0, 0, 0, 255);
  SDL_FRect screen{0, 0, 1280, 720};
  SDL_RenderFillRect(&renderer, &screen);

  const bool inTransition =
      impl.drawDurationMs > 0 && now < impl.drawStartTick + impl.drawDurationMs;
  if (inTransition) {
    const float t = std::clamp(
        float(now > impl.drawStartTick ? (now - impl.drawStartTick) : 0) /
            float(impl.drawDurationMs),
        0.0f, 1.0f);
    if (impl.drawWipeMode) {
      // Two disjoint GPU scissor regions: incoming pixels REPLACE outgoing
      // pixels; faces and transparent sprites cannot be drawn twice.
      if (!impl.renderWipe(renderer, now, t))
        impl.renderSlottedScene(renderer, impl.visibleSlots, now);
    } else if (!impl.renderCrossfade(renderer, now, t)) {
      // Rare render-target allocation/backend failure: stay playable and
      // restore the legacy fade while emitting a meaningful diagnostic.
      SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                  "LucaSystem: full-scene crossfade unavailable: %s",
                  SDL_GetError());
      SDL_SetRenderDrawColor(&renderer, 0, 0, 0, 255);
      SDL_RenderFillRect(&renderer, &screen);
      impl.renderSlottedScene(renderer, impl.outgoingSlots, now, 1.0f - t);
      impl.renderSlottedScene(renderer, impl.visibleSlots, now, t);
    }
  } else {
    impl.renderSlottedScene(renderer, impl.visibleSlots, now);
  }

  impl.renderCalendarBadge(renderer, now, inTransition);

  if (impl.flashDurationMs > 0 &&
      now < impl.flashStartTick + impl.flashDurationMs) {
    const float ft =
        1.0f - float(now - impl.flashStartTick) / float(impl.flashDurationMs);
    SDL_SetRenderDrawBlendMode(&renderer, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(
        &renderer, impl.flashColor.r, impl.flashColor.g, impl.flashColor.b,
        static_cast<Uint8>(std::lround(std::clamp(ft, 0.0f, 1.0f) * 255.0f)));
    SDL_RenderFillRect(&renderer, &screen);
  }

  if (impl.textbox) {
    impl.textbox->setControls(impl.skipMode, impl.autoMode,
                              !impl.choiceActive &&
                                  currentDialogueVoiceId_ != 0);
    if (impl.choiceActive)
      impl.textbox->renderChoiceControls(renderer);
  }
  Runtime::renderGameplay(renderer, scale);
}

void LucaSystemRuntime::advanceDialogue() { impl_->advanceDialogue(); }

bool LucaSystemRuntime::handleBackAction() {
  auto &impl = *impl_;
  if (impl.showingSplash) {
    impl.finishSplash();
    return true;
  }
  if (!impl.originalTitle)
    return false;
  if (impl.storyOpen) {
    impl.storyOpen = false;
    impl.playSysSe("CANCEL");
    impl.playTitleAmbience();
  } else if (impl.titlePage) {
    impl.selectTitle(0);
  }
  return true;
}

bool LucaSystemRuntime::onScreenEvent(Kotonoha::EventRef event) {
  if (impl_->originalTitle && event.type == SDL_EVENT_GAMEPAD_BUTTON_DOWN &&
      event.gbutton.button == SDL_GAMEPAD_BUTTON_START) {
    for (const auto *scene : GetActiveScenes())
      if (scene && scene->capturesInput())
        return false;
    SDL_Event confirm{};
    confirm.type = SDL_EVENT_KEY_DOWN;
    confirm.key.key = SDLK_RETURN;
    return onEvent(confirm);
  }
  return Runtime::onScreenEvent(event);
}

bool LucaSystemRuntime::handleAdvanceAction() {
  auto &impl = *impl_;
  if (!impl.dialogueVisible || isStoryPaused())
    return false;
  impl.autoAdvanceAt = 0;
  if (impl.textbox && impl.textbox->isUserHidden()) {
    impl.textbox->setUserHidden(false);
    return true;
  }
  impl.advanceDialogue();
  return true;
}

bool LucaSystemRuntime::onEvent(Kotonoha::EventRef event) {
  auto mapped = Kotonoha::MapControllerEvent(event);
  auto &impl = *impl_;
  if (mapped.type == SDL_EVENT_KEY_UP && mapped.key.key == SDLK_LCTRL)
    impl.skipMode = false;

  if (impl.showingSplash) {
    if (mapped.type == SDL_EVENT_KEY_DOWN && !mapped.key.repeat) {
      if (mapped.key.key == SDLK_ESCAPE || mapped.key.key == SDLK_AC_BACK ||
          mapped.key.key == SDLK_X)
        impl.finishSplash();
      else if (mapped.key.key == SDLK_RETURN || mapped.key.key == SDLK_SPACE)
        impl.advanceSplash();
    } else if (Kotonoha::UI::clicked(mapped, {0, 0, 1280, 720})) {
      impl.advanceSplash();
    }
    return true;
  }

  for (const auto *scene : GetActiveScenes()) {
    if (scene && scene->capturesInput()) {
      if (!impl.originalTitle && scene == impl.choiceScene && impl.textbox &&
          !isStoryPaused() && impl.textbox->handleControlClick(mapped, false))
        return true;
      return Runtime::onEvent(event);
    }
  }

  if (impl.moviePlayer && !impl.movieFinished) {
    const bool menuKey =
        (mapped.type == SDL_EVENT_KEY_DOWN && !mapped.key.repeat &&
         mapped.key.key == SDLK_ESCAPE) ||
        (mapped.type == SDL_EVENT_GAMEPAD_BUTTON_DOWN &&
         mapped.gbutton.button == SDL_GAMEPAD_BUTTON_START);
    if (menuKey && impl.menu) {
      impl.menu->open();
      return true;
    }
    if (!isStoryPaused() && impl.videoSkip.accept(event))
      skipMovie();
    return true;
  }

  if (impl.originalTitle) {
    if (!impl.titleTouch.accept(mapped))
      return true;
    const bool back =
        (impl.engine && Kotonoha::BackAction(*impl.engine, mapped)) ||
        (mapped.type == SDL_EVENT_KEY_DOWN && !mapped.key.repeat &&
         mapped.key.key == SDLK_X);
    if (back)
      return handleBackAction();
  }
  const bool hover = mapped.type == SDL_EVENT_MOUSE_MOTION;
  const SDL_FPoint pointer =
      hover ? SDL_FPoint{mapped.motion.x, mapped.motion.y} : SDL_FPoint{};
  bool confirm =
      mapped.type == SDL_EVENT_KEY_DOWN && !mapped.key.repeat &&
      (mapped.key.key == SDLK_RETURN || mapped.key.key == SDLK_SPACE);
  if (impl.originalTitle && impl.storyOpen) {
    if (Kotonoha::UI::clicked(mapped, storyClose))
      return handleBackAction();
    for (int i = 0; i < impl.storyCount(); ++i) {
      const auto rect = impl.storyRect(i);
      if (Kotonoha::UI::clicked(mapped, rect)) {
        impl.storySelection = i;
        impl.playSysSe("ENTER");
        impl.launchChapterFromTitle(i == 0 ? impl.startupScenario()
                                           : storyRoots[i]);
        return true;
      }
      if (hover && SDL_PointInRectFloat(&pointer, &rect)) {
        if (!impl.storyHighlight || impl.storySelection != i)
          impl.playSysSe("CURSOR");
        impl.storySelection = i;
        impl.storyHighlight = true;
      }
    }
    if (mapped.type == SDL_EVENT_KEY_DOWN && !mapped.key.repeat) {
      const auto key = mapped.key.key;
      if (key == SDLK_UP || key == SDLK_LEFT || key == SDLK_DOWN ||
          key == SDLK_RIGHT) {
        const int count = impl.storyCount();
        impl.storySelection = (impl.storySelection + count +
                               (key == SDLK_UP || key == SDLK_LEFT ? -1 : 1)) %
                              count;
        impl.storyHighlight = true;
        impl.playSysSe("CURSOR");
      }
    }
    if (confirm) {
      impl.playSysSe("ENTER");
      impl.launchChapterFromTitle(impl.storySelection == 0
                                      ? impl.startupScenario()
                                      : storyRoots[impl.storySelection]);
    }
    return true;
  }

  const auto titleHit = [&](float x, float y) {
    const SDL_FPoint point{x, y};
    const float offset = impl.titleOffset();
    for (int i = 0; i < int(std::size(titleItems)); ++i) {
      const auto rect = impl.titleRect(i, offset);
      if (SDL_PointInRectFloat(&point, &rect))
        return i;
    }
    return -1;
  };
  if (impl.originalTitle && hover) {
    const int hit = titleHit(mapped.motion.x, mapped.motion.y);
    if (hit >= 0 && titlePages[hit] == impl.titlePage &&
        (!impl.titleHighlight || hit != impl.titleSelection)) {
      impl.titleSelection = hit;
      impl.titleHighlight = true;
      impl.playSysSe("CURSOR");
    }
    return hit >= 0;
  }
  if (impl.originalTitle && Kotonoha::UI::clicked(mapped, {0, 540, 1280, 70})) {
    if (Kotonoha::UI::clicked(
            mapped, {impl.titlePage ? 66.0f : 1150.0f, 540, 60, 70})) {
      impl.selectTitle(impl.titlePage ? 0 : 5);
      return true;
    }
    const int hit = titleHit(mapped.button.x, mapped.button.y);
    if (hit < 0 || titlePages[hit] != impl.titlePage)
      return true;
    impl.titleSelection = hit;
    impl.titleHighlight = true;
    confirm = true;
  }
  if (impl.originalTitle && mapped.type == SDL_EVENT_KEY_DOWN &&
      !mapped.key.repeat &&
      (mapped.key.key == SDLK_LEFT || mapped.key.key == SDLK_RIGHT)) {
    constexpr int count = int(std::size(titleItems));
    impl.selectTitle(
        (impl.titleSelection + (mapped.key.key == SDLK_RIGHT ? 1 : count - 1)) %
        count);
    return true;
  }
  if (impl.originalTitle && confirm) {
    impl.playSysSe("ENTER");
    switch (impl.titleSelection) {
    case 0: // NEW GAME opens the retail story selector, even with only DREAM.
      impl.playBgm(0, false);
      impl.se.Stop();
      impl.storyOpen = true;
      impl.storySelection = 0;
      impl.storyHighlight = false;
      return true;
    case 1: // LOAD
      impl.openSaveSlots(true);
      return true;
    case 2: // OPTIONS
      if (impl.engine)
        impl.engine->openSettings();
      return true;
    case 3: // MANUAL
      if (impl.menu)
        impl.menu->openManual(true);
      return true;
    case 4: { // EXIT
      SDL_Event quit{};
      quit.type = SDL_EVENT_QUIT;
      SDL_PushEvent(&quit);
      return true;
    }
    case 5: // GALLERY
      if (impl.gallery)
        impl.gallery->open();
      return true;
    case 6: { // MUSIC -> cycle/listen to MUSIC.PAK BGM tracks
      if (impl.choiceScene) {
        impl.choiceActive = true;
        std::vector<Kotonoha::ChoiceOption> tracks;
        for (uint32_t tid = 100001; tid <= 100028; ++tid) {
          if (impl.assets->hasBgmAsset(tid))
            tracks.push_back(
                {"Track " + std::to_string(tid - 100000), int(tid)});
        }
        tracks.push_back({"Close Music Box", 0});
        impl.choiceScene->setQuestion(
            "MUSIC GALLERY", tracks, [this](int tid, const std::string &) {
              impl_->choiceActive = false;
              if (impl_->choiceScene)
                impl_->choiceScene->hide();
              if (tid > 0) {
                impl_->se.Stop();
                impl_->playBgm(static_cast<uint32_t>(tid), true);
              }
            });
      }
      return true;
    }
    default:
      return true;
    }
  }

  if (impl.originalTitle)
    return false;

  if (!impl.originalTitle && mapped.type == SDL_EVENT_KEY_DOWN &&
      !mapped.key.repeat && mapped.key.key == SDLK_F5) {
    auto path = Kotonoha::saveDirectory() / "quicksave.kns";
    saveGame(path.string());
    return true;
  }
  if (!impl.originalTitle && mapped.type == SDL_EVENT_KEY_DOWN &&
      !mapped.key.repeat && mapped.key.key == SDLK_F6) {
    auto path = Kotonoha::saveDirectory() / "quicksave.kns";
    if (std::filesystem::exists(path))
      loadGame(path.string());
    return true;
  }
  if (!impl.originalTitle && mapped.type == SDL_EVENT_KEY_DOWN &&
      mapped.key.key == SDLK_LCTRL) {
    impl.skipMode = true;
    impl.autoMode = false;
  }
  if (impl.dialogueVisible && mapped.type == SDL_EVENT_KEY_DOWN &&
      !mapped.key.repeat &&
      (mapped.key.key == SDLK_A || mapped.key.key == SDLK_S)) {
    impl.dialogueControl(mapped.key.key == SDLK_A ? AirTextBox::Control::Auto
                                                  : AirTextBox::Control::Skip);
    return true;
  }

  if (Runtime::onEvent(event))
    return true;
  if (impl.menu && impl.engine && Kotonoha::BackAction(*impl.engine, mapped)) {
    impl.menu->open();
    return true;
  }
  const bool advance =
      (mapped.type == SDL_EVENT_KEY_DOWN && !mapped.key.repeat &&
       (mapped.key.key == SDLK_RETURN || mapped.key.key == SDLK_SPACE)) ||
      Kotonoha::UI::clicked(mapped, {0, 0, 1280, 720});
  return advance && handleAdvanceAction();
}

void LucaSystemRuntime::saveGame(const std::string &path) {
  auto &impl = *impl_;
  SaveState state;
  state.scenario = impl.currentScenario;
  state.instructionIndex = (impl.choiceActive && impl.instructionIndex > 0)
                               ? (impl.instructionIndex - 1)
                               : impl.instructionIndex;
  state.dialogueIndex = impl.dialogueIndex;
  state.dialogueVisible = impl.dialogueVisible;
  state.dialogueText = dialogueSpeaker_.empty()
                           ? dialogueText_
                           : ("@" + dialogueSpeaker_ + "@" + dialogueText_);
  state.waiting = impl.waiting;
  if (impl.waiting) {
    const auto now = impl.storyNow();
    const auto remaining =
        impl.waitDeadline > now ? impl.waitDeadline - now : 0;
    state.waitRemainingMs = static_cast<std::uint32_t>(
        std::min<std::uint64_t>(remaining, SaveCodec::MaxWaitMs));
  }
  state.numbers.insert(impl.numericVars.begin(), impl.numericVars.end());
  // Persist active BGM in reserved internal save var so load restores BGM
  if (impl.currentBgmId != 0)
    state.numbers[99001] = static_cast<int32_t>(impl.currentBgmId);
  state.strings.insert(impl.stringVars.begin(), impl.stringVars.end());
  state.flags.insert(impl.flags.begin(), impl.flags.end());
  state.callFrames = impl.callStack;
  for (const auto &frame : impl.callStack)
    state.callStack.push_back(frame.instructionIndex);
  const auto saveTick = impl.storyNow();
  for (const auto &[key, slot] : impl.visibleSlots) {
    if (slot.assetId == 0)
      continue;
    // Keep legacy image metadata as a fallback for old save readers.
    if (slot.visible)
      state.images.push_back({slot.assetId,
                              static_cast<int32_t>(std::lround(slot.posX)),
                              static_cast<int32_t>(std::lround(slot.posY))});
    SavedVisualLayer saved;
    saved.group = slot.group;
    saved.slot = slot.slot;
    saved.assetId = slot.assetId;
    saved.posX = slot.posX;
    saved.posY = slot.posY;
    const uint64_t endTick = slot.moveStart + slot.moveDuration;
    const bool moving = slot.moveDuration > 0 && saveTick < endTick;
    saved.startOffsetX =
        airMoveOffset(slot.startOffsetX, slot.targetOffsetX,
                      slot.moveStart, slot.moveDuration, saveTick);
    saved.startOffsetY =
        airMoveOffset(slot.startOffsetY, slot.targetOffsetY,
                      slot.moveStart, slot.moveDuration, saveTick);
    saved.targetOffsetX = slot.targetOffsetX;
    saved.targetOffsetY = slot.targetOffsetY;
    saved.moveRemainingMs = moving
        ? static_cast<uint32_t>(std::min<uint64_t>(endTick - saveTick, 60000))
        : 0;
    saved.alpha = slot.alpha;
    saved.scaleX = slot.scaleX;
    saved.scaleY = slot.scaleY;
    saved.rotation = slot.rotation;
    saved.r = slot.r;
    saved.g = slot.g;
    saved.b = slot.b;
    saved.visible = slot.visible;
    state.visualLayers.push_back(saved);
  }
  if (impl.engine) {
    // LucaSystem owns its script counter and variables, not VNManager's
    // timeline node. Use the native save envelope with the VM's own payload.
    Kotonoha::VNSave save;
    save.nodeId = 0;
    save.gameData = encodeSaveState(state);
    try {
      save.thumbnailRgb = impl.captureSaveThumbnail();
    } catch (const std::exception &error) {
      SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "LucaSystem save preview: %s",
                  error.what());
    }
    save.write(path);
  } else {
    const auto payload = encodeSaveState(state);
    std::vector<uint8_t> raw(payload.begin(), payload.end());
    std::string err;
    if (!Kotonoha::Filesystem::writeBinaryFileAtomically(path, raw, &err))
      throw std::runtime_error("LucaSystem save failed: " + err);
  }
}

void LucaSystemRuntime::loadGame(const std::string &path) {
  auto &impl = *impl_;
  const bool fromTitle = impl.originalTitle;
  std::string payload;
  if (impl.engine) {
    auto saved = Kotonoha::VNSave::read(path);
    payload = std::move(saved.gameData);
  } else {
    auto raw = Kotonoha::Filesystem::readLocalFile(path);
    payload.assign(raw.begin(), raw.end());
  }
  const SaveState state = decodeSaveState(payload);

  auto *pak = impl.assets->scriptPak();
  if (!pak || !pak->findByName(state.scenario))
    throw std::runtime_error("LucaSystem save scenario is unavailable");
  auto bytes = impl.assets->read(state.scenario);
  Script candidate(state.scenario,
                   std::vector<std::uint8_t>(bytes.begin(), bytes.end()));
  if (!candidate.valid() ||
      state.instructionIndex > candidate.instructionCount() ||
      state.dialogueIndex > 1000000 || state.callFrames.size() > 1024)
    throw std::runtime_error("Invalid LucaSystem save script position");
  for (const auto &frame : state.callFrames)
    if (frame.scenario == state.scenario &&
        frame.instructionIndex > candidate.instructionCount())
      throw std::runtime_error("Invalid LucaSystem save return frame");

  if (!startScenario(state.scenario, state.instructionIndex, true))
    throw std::runtime_error("Cannot resume LucaSystem scenario");
  if (fromTitle && impl.engine) {
    impl.stopAudio();
    impl.attachGameplayScenes();
  }
  impl.numericVars.clear();
  impl.numericVars.insert(state.numbers.begin(), state.numbers.end());
  impl.syncGlobalVarsIntoSession();
  impl.stringVars.clear();
  impl.stringVars.insert(state.strings.begin(), state.strings.end());
  impl.flags.clear();
  impl.flags.insert(state.flags.begin(), state.flags.end());
  impl.callStack = state.callFrames;

  impl.stagedSlots.clear();
  impl.visibleSlots.clear();
  impl.outgoingSlots.clear();
  impl.stageResets.reset();
  if (!state.visualLayers.empty()) {
    const auto now = impl.storyNow();
    for (const auto &saved : state.visualLayers) {
      impl.loadObjectImage(saved.assetId);
      SlottedLayer layer;
      layer.group = saved.group;
      layer.slot = saved.slot;
      layer.assetId = saved.assetId;
      layer.posX = saved.posX;
      layer.posY = saved.posY;
      layer.startOffsetX = saved.startOffsetX;
      layer.startOffsetY = saved.startOffsetY;
      layer.targetOffsetX = saved.targetOffsetX;
      layer.targetOffsetY = saved.targetOffsetY;
      layer.moveStart = now;
      layer.moveDuration = saved.moveRemainingMs;
      layer.alpha = saved.alpha;
      layer.scaleX = saved.scaleX;
      layer.scaleY = saved.scaleY;
      layer.rotation = saved.rotation;
      layer.r = saved.r;
      layer.g = saved.g;
      layer.b = saved.b;
      layer.visible = saved.visible;
      impl.visibleSlots[Impl::makeKey(layer.group, layer.slot)] = layer;
    }
  } else {
    // V2–V5: only asset IDs and approximate coordinates were persisted.
    uint8_t charSlotCounter = 0;
    uint8_t bgSlotCounter = 0;
    for (const auto &place : state.images) {
      impl.loadObjectImage(place.assetId);
      SlottedLayer layer;
      layer.assetId = place.assetId;
      layer.posX = float(place.x);
      layer.posY = float(place.y);
      if (place.assetId >= 10000 && place.assetId < 20000) {
        layer.group = 2;
        layer.slot = charSlotCounter++;
      } else {
        layer.group = 0;
        layer.slot = bgSlotCounter++;
      }
      impl.visibleSlots[Impl::makeKey(layer.group, layer.slot)] = layer;
    }
  }
  impl.stagedSlots = impl.visibleSlots;
  impl.drawDurationMs = 0;
  impl.drawWipeMode = false;
  impl.pruneObjectCaches();
  impl.syncLegacySceneLayers();

  if (auto bgmIt = impl.numericVars.find(99001);
      bgmIt != impl.numericVars.end() && bgmIt->second > 0) {
    impl.playBgm(static_cast<uint32_t>(bgmIt->second), true);
  }

  impl.dialogueIndex = state.dialogueIndex;
  impl.waiting = state.waiting;
  impl.waitDeadline = impl.storyNow() + state.waitRemainingMs;

  std::string rawText = state.dialogueText;
  if (state.dialogueVisible && rawText.empty()) {
    for (std::size_t i = state.instructionIndex; i > 0; --i) {
      const auto &ins = impl.instructions[i - 1];
      if (ins.opcode == OP_MESSAGE && !ins.text.empty()) {
        rawText = ins.text;
        break;
      }
    }
  }
  std::string spk, cleanText;
  parseSpeakerAndSanitizeText(rawText, spk, cleanText);
  impl.dialogueVisible = state.dialogueVisible && !cleanText.empty();
  originalActiveKeys_.clear();
  if (impl.dialogueVisible) {
    if (!spk.empty())
      originalActiveKeys_.push_back(spk);
    originalActiveKeys_.push_back(cleanText);
  }
  currentDialogueVoiceId_ = 0;
  const auto translate = [&](const std::string &original) {
    return impl.engine ? impl.engine->GetTranslation().text(0, original)
                       : original;
  };
  dialogueText_ = impl.dialogueVisible ? translate(cleanText) : std::string{};
  dialogueSpeaker_ = impl.dialogueVisible ? translate(spk) : std::string{};
  if (impl.textbox) {
    impl.textbox->setWindowType(impl.getVar(6002));
    if (impl.dialogueVisible) {
      impl.textbox->setDialogue(dialogueSpeaker_, dialogueText_);
      impl.textbox->show();
    } else {
      impl.textbox->hide();
    }
  }
  // If saved at a SELECT instruction, re-execute SELECT to restore choices
  if (!impl.dialogueVisible &&
      impl.instructionIndex < impl.instructions.size() &&
      impl.instructions[impl.instructionIndex].opcode == OP_SELECT) {
    impl.runUntilPause();
  }
}

bool LucaSystemRuntime::startScenario(const std::string &name, size_t position,
                                      bool resume) {
  auto &impl = *impl_;
  if (!impl.loadScenarioScript(name)) {
    SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                "LucaSystem: failed to load scenario '%s'", name.c_str());
    return false;
  }
  if (position > impl.instructions.size())
    return false;

  impl.originalTitle = false;
  impl.showingSplash = false;
  if (impl.gallery)
    impl.gallery->close();
  impl.instructionIndex = position;
  impl.callStack.clear();
  impl.stagedSlots.clear();
  impl.visibleSlots.clear();
  impl.outgoingSlots.clear();
  impl.stageResets.reset();
  impl.sceneLayers.reset();
  impl.waiting = false;
  impl.choiceActive = false;
  impl.running = position < impl.instructions.size();
  dialogueText_.clear();
  dialogueSpeaker_.clear();
  originalActiveKeys_.clear();
  currentDialogueVoiceId_ = 0;
  if (impl.textbox)
    impl.textbox->hide();

  SDL_Log("LucaSystem: scenario '%s' parsed: %zu instructions, %zu dialogue "
          "lines",
          impl.currentScenario.c_str(), impl.instructions.size(),
          impl.dialogueLines.size());

  if (!resume)
    impl.runUntilPause();
  return true;
}

std::string LucaSystemRuntime::currentScenarioName() const {
  return impl_->currentScenario;
}
size_t LucaSystemRuntime::instructionPosition() const {
  return impl_->instructionIndex;
}
bool LucaSystemRuntime::isRunning() const { return impl_->running; }
bool LucaSystemRuntime::isDialogueVisible() const {
  return impl_->dialogueVisible;
}
bool LucaSystemRuntime::isChoiceActive() const { return impl_->choiceActive; }
std::vector<std::string> LucaSystemRuntime::activeChoices() const {
  return impl_->currentChoiceOptions;
}
bool LucaSystemRuntime::selectChoice(int optionIndex) {
  auto &impl = *impl_;
  if (!impl.choiceActive || optionIndex < 0 ||
      static_cast<size_t>(optionIndex) >= impl.currentChoiceOptions.size())
    return false;
  const uint16_t targetVar = impl.choiceTargetVarId;
  impl.choiceActive = false;
  impl.currentChoiceOptions.clear();
  if (impl.choiceScene)
    impl.choiceScene->hide();
  impl.setVar(targetVar, optionIndex);
  impl.runUntilPause();
  return true;
}
bool LucaSystemRuntime::isMoviePlaying() const {
  return impl_->moviePlayer && !impl_->movieFinished;
}
void LucaSystemRuntime::skipMovie() {
  auto &impl = *impl_;
  if (impl.moviePlayer) {
    impl.movieFinished = true;
    impl.moviePlayer->stop();
    impl.moviePlayer.reset();
    impl.waiting = false;
    impl.runUntilPause();
  }
}
void LucaSystemRuntime::skipWait() {
  auto &impl = *impl_;
  if (impl.waiting && !impl.moviePlayer) {
    impl.waiting = false;
    impl.waitDeadline = 0;
    impl.lastDrawDeadline = 0;
    impl.runUntilPause();
  }
}
int32_t LucaSystemRuntime::getVariable(uint32_t varId) const {
  return impl_->getVar(varId);
}
void LucaSystemRuntime::setVariable(uint32_t varId, int32_t value) {
  impl_->setVar(varId, value);
}
uint32_t LucaSystemRuntime::activeBgmId() const { return impl_->currentBgmId; }
uint32_t LucaSystemRuntime::lastVoiceId() const {
  return impl_->lastPlayedVoiceId;
}
size_t LucaSystemRuntime::visibleLayerCount() const {
  return impl_->visibleSlots.size();
}
bool LucaSystemRuntime::isGalleryOpen() const {
  return impl_->gallery && impl_->gallery->capturesInput();
}
uint16_t LucaSystemRuntime::galleryImageId() const {
  return isGalleryOpen() ? impl_->gallery->viewedImage() : 0;
}
bool LucaSystemRuntime::isCgUnlocked(uint32_t id) const {
  return impl_->galleryIds.count(id) != 0 && impl_->cgUnlocked(id);
}

bool LucaSystemRuntime::debugJumpToLabel(const std::string &label) {
  auto &impl = *impl_;
  if (const auto index = FindJumpPoint(impl.instructions, label)) {
    impl.instructionIndex = *index;
    impl.runUntilPause();
    return true;
  }
  return false;
}

std::string LucaSystemRuntime::debugNode() const {
  auto &impl = *impl_;
  return impl.currentScenario.empty()
             ? std::string()
             : ("luca_system | scenario " + impl.currentScenario + " @" +
                std::to_string(impl.instructionIndex));
}

std::string LucaSystemRuntime::debugNodeCatalogKey() const {
  return impl_->currentScenario;
}

std::vector<Kotonoha::DebugNode> LucaSystemRuntime::debugNodes() const {
  // Enumerating PAK metadata is cheap: only inspect decoded opcodes for the
  // currently loaded script, never decode all AIR scenarios in a UI frame.
  std::vector<Kotonoha::DebugNode> nodes;
  if (!assets_)
    return nodes;

  constexpr std::string_view prefix = "luca_system | scenario ";
  const auto nodeId = [&](const std::string &script) {
    return std::string(prefix) + script;
  };
  const auto &script = impl_->currentScenario;
  std::set<std::string> knownScripts;
  if (const auto *pak = assets_->scriptPak()) {
    for (const auto &entry : pak->entries()) {
      if (entry.name.empty() || !knownScripts.insert(entry.name).second)
        continue;
      nodes.push_back({nodeId(entry.name),
                       std::string(isSystemScript(entry.name) ? "System table "
                                                              : "Scenario ") +
                           entry.name,
                       {entry.name}});
    }
  }
  if (!script.empty() && knownScripts.insert(script).second)
    nodes.push_back({nodeId(script), "Scenario " + script, {script}});
  if (script.empty())
    return nodes;

  const auto events = buildDebugInstructions(script, impl_->instructions,
                                             impl_->instructionIndex);
  std::set<std::string> sceneAssets;
  sceneAssets.insert(script);
  const auto include = [&](std::vector<std::string> &target,
                           const std::string &id) {
    // Logical image:/voice:/bgm:/se: IDs and named PAK entries are all
    // resolved by the active provider, never by a raw filesystem lookup.
    if (!id.empty() && assets_->contains(id) &&
        std::find(target.begin(), target.end(), id) == target.end())
      target.push_back(id);
  };

  // The last executed MESSAGE/IMAGELOAD may already be past the interpreter's
  // next instruction PC. Show currently visible scene resources too.
  std::vector<std::string> liveAssets;
  include(liveAssets, script);
  const auto layers = [&](const auto &slots) {
    for (const auto &[key, layer] : slots)
      if (layer.visible && layer.assetId)
        include(liveAssets, "image:" + std::to_string(layer.assetId));
  };
  layers(impl_->visibleSlots);
  layers(impl_->stagedSlots);
  layers(impl_->outgoingSlots);
  if (impl_->currentBgmId)
    include(liveAssets, "bgm:" + std::to_string(impl_->currentBgmId));
  if (currentDialogueVoiceId_)
    include(liveAssets, "voice:" + std::to_string(currentDialogueVoiceId_));

  for (const auto &event : events) {
    std::vector<std::string> references;
    for (const auto &id : event.assets)
      include(references, id);
    for (const auto &id : references)
      sceneAssets.insert(id);
    if (event.pc == impl_->instructionIndex)
      for (const auto &id : liveAssets)
        include(references, id);
    nodes.push_back({nodeId(script) + " @" + std::to_string(event.pc),
                     event.label, std::move(references)});
  }
  // A scenario node gives the Asset Provider Viewer a complete index of
  // the current script's known resources, not just the current instruction.
  for (auto &node : nodes)
    if (node.id == nodeId(script)) {
      node.assets.assign(sceneAssets.begin(), sceneAssets.end());
      for (const auto &id : liveAssets)
        include(node.assets, id);
      break;
    }
  return nodes;
}

void LucaSystemRuntime::debugJumpToNode(const std::string &id) {
  constexpr std::string_view prefix = "luca_system | scenario ";
  if (id.compare(0, prefix.size(), prefix) != 0)
    throw std::invalid_argument("Invalid LucaSystem debug node ID");

  const auto pos = id.rfind(" @");
  const std::string script =
      id.substr(prefix.size(), pos == std::string::npos ? std::string::npos
                                                        : pos - prefix.size());
  if (script.empty() || !assets_ || !assets_->scriptPak() ||
      !assets_->scriptPak()->findByName(script))
    throw std::invalid_argument("Unknown LucaSystem scenario: " + script);
  if (isSystemScript(script))
    throw std::invalid_argument(
        "LucaSystem system tables are viewable assets, not executable scenes");

  std::size_t pc = 0;
  if (pos != std::string::npos) {
    const char *start = id.data() + pos + 2;
    const char *end = id.data() + id.size();
    const auto parsed = std::from_chars(start, end, pc);
    if (parsed.ec != std::errc{} || parsed.ptr != end)
      throw std::invalid_argument("Invalid LucaSystem instruction index");
  }

  // This is a native interpreter jump, not a VNManager timeline jump.
  // Starting at an arbitrary instruction cannot reconstruct earlier VM
  // variables/layers, matching the Debug UI's existing warning.
  // Validate the target before mutating the VM. An invalid PC must never
  // discard the active dialogue, staged layers, save state or call stack.
  if (script == impl_->currentScenario) {
    if (pc >= impl_->instructions.size())
      throw std::out_of_range("Cannot jump beyond the LucaSystem script");
  } else {
    const auto bytes = assets_->read(script);
    const Script target(script, {bytes.begin(), bytes.end()});
    if (!target.valid() || pc >= target.instructionCount())
      throw std::out_of_range("Cannot jump to an invalid LucaSystem PC");
  }
  if (!startScenario(script, pc, true))
    throw std::out_of_range("LucaSystem scenario or instruction not found");
  SetState(Kotonoha::GameState::Gameplay);
  impl_->runUntilPause();
}

void LucaSystemRuntime::debugRestartScene() {
  if (impl_->currentScenario.empty() || !startScenario(impl_->currentScenario))
    throw std::runtime_error("There is no active LucaSystem scene to restart");
}

std::vector<std::string> LucaSystemRuntime::activeTranslationKeys() const {
  return originalActiveKeys_;
}

std::vector<Kotonoha::DebugAudioAsset> LucaSystemRuntime::debugSceneAudio() {
  if (!currentDialogueVoiceId_)
    return {};
  return {{"voice:" + std::to_string(currentDialogueVoiceId_), dialogueSpeaker_,
           dialogueText_}};
}

Kotonoha::Translation::SourceTexts
LucaSystemRuntime::translationSources() const {
  return scenarioTexts_.empty()
             ? Kotonoha::Translation::SourceTexts{}
             : Kotonoha::Translation::SourceTexts{{0, scenarioTexts_}};
}

} // namespace LucaSystem
