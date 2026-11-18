// Copyright 2026 Kotonoha contributors. SPDX-License-Identifier: MIT
// LucaSystem (Visual Art's / Key) runtime — 1:1 VM & presentation recovered via
// clean-room REA + Rizin analysis of AIR.exe (.rdata opcode/chapter tables) and
// exhaustive audit of all 31 .PAK archives.
#include <LucaSystem/CzImage.hpp>
#include <LucaSystem/DialogueFlow.hpp>
#include <LucaSystem/LucaSystemRuntime.hpp>
#include <LucaSystem/LucaSystemScript.hpp>
#include <LucaSystem/SaveState.hpp>
#include <LucaSystem/SceneLayers.hpp>

#include <Kotonoha/Filesystem/AtomicFile.hpp>
#include <Kotonoha/Filesystem/Filesystem.hpp>
#include <Kotonoha/Filesystem/StoragePaths.hpp>
#include <Kotonoha/components/ChoiceQuestionScene.hpp>
#include <Kotonoha/components/InGameMenu.hpp>
#include <Kotonoha/components/VNBacklog.hpp>
#include <Kotonoha/components/VNManager.hpp>
#include <Kotonoha/components/VNSaveMenu.hpp>
#include <Kotonoha/components/VNTextBox.hpp>
#include <Kotonoha/core/Engine.hpp>
#include <Kotonoha/core/SDL.hpp>
#include <Kotonoha/core/UI.hpp>
#include <Kotonoha/events/ControllerEvent.hpp>
#include <Kotonoha/media/MediaPlayer.hpp>
#include <Kotonoha/media/PcmPlayer.hpp>
#include <SDL3/SDL.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <map>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <vector>

namespace LucaSystem {

namespace {

bool isSystemScript(std::string_view name) { return name.rfind("_", 0) == 0; }

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
  std::array<CzImage, 5> frames_;
  CzImage fallbackStrip_;
  CzImage cursor_;
  std::array<Kotonoha::TexturePtr, 5> frameTextures_;
  Kotonoha::TexturePtr fallbackStripTexture_;
  Kotonoha::TexturePtr cursorTexture_;
  int windowType_ = 0;

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
      target.reset(SDL_CreateTexture(&renderer, SDL_PIXELFORMAT_RGBA32,
                                     SDL_TEXTUREACCESS_STATIC, image.width,
                                     image.height));
      if (!target || !SDL_UpdateTexture(target.get(), nullptr,
                                        image.rgba.data(), image.width * 4))
        return nullptr;
      SDL_SetTextureScaleMode(target.get(), SDL_SCALEMODE_LINEAR);
      SDL_SetTextureBlendMode(target.get(), SDL_BLENDMODE_BLEND);
    }
    return target.get();
  }

public:
  void setWindowType(int wt) { windowType_ = std::clamp(wt, 0, 4); }

protected:
  void renderFrame(Kotonoha::RendererRef renderer) override {
    const int idx = std::clamp(windowType_, 0, 4);
    const CzImage &chosen =
        !frames_[idx].rgba.empty() ? frames_[idx] : frames_[0];
    Kotonoha::TexturePtr &chosenTex =
        !frames_[idx].rgba.empty() ? frameTextures_[idx] : frameTextures_[0];
    auto *tex = texture(renderer, chosenTex, chosen);
    if (tex) {
      SDL_FRect destination{0, 480, 1280, 240};
      SDL_RenderTexture(&renderer, tex, nullptr, &destination);
    } else if (auto *strip =
                   texture(renderer, fallbackStripTexture_, fallbackStrip_)) {
      SDL_FRect destination{160, 500, 950, 160};
      SDL_RenderTexture(&renderer, strip, nullptr, &destination);
    } else {
      SDL_SetRenderDrawBlendMode(&renderer, SDL_BLENDMODE_BLEND);
      SDL_SetRenderDrawColor(&renderer, 40, 48, 72, 210);
      SDL_FRect box{160, 500, 950, 160};
      SDL_RenderFillRect(&renderer, &box);
      SDL_SetRenderDrawColor(&renderer, 100, 115, 150, 120);
      SDL_RenderRect(&renderer, &box);
    }
  }

  void renderIndicator(Kotonoha::RendererRef renderer) override {
    auto *tex = texture(renderer, cursorTexture_, cursor_);
    if (tex) {
      const int frame = int((SDL_GetTicks() / 120) % 4);
      SDL_FRect source{0, float(frame * 38), 48, 38};
      SDL_FRect destination{1060, 640, 48, 38};
      SDL_RenderTexture(&renderer, tex, &source, &destination);
    }
  }

  void renderSpeaker(Kotonoha::RendererRef renderer) override {
    if (!speakerName.empty())
      engine.GetVNUI().text(renderer, speakerName, 210, 494, 850,
                            SDL_Color{255, 238, 175, 255});
  }

  void renderDialogue(Kotonoha::RendererRef renderer) override {
    SDL_Rect clip{210, 532, 850, 150};
    SDL_SetRenderClipRect(&renderer, &clip);
    auto measure = [this](const std::string &value) {
      return engine.GetVNUI().textWidth(value);
    };
    const auto lines = Kotonoha::VNTextBoxUtility::RevealLines(
        fullDialogue, visibleDialogue.size(), measure, 850);
    float y = speakerName.empty() ? 534 : 540;
    for (const auto &line : lines) {
      engine.GetVNUI().text(renderer, line, 210, y, 850,
                            SDL_Color{255, 255, 255, 255});
      y += 32;
    }
    SDL_SetRenderClipRect(&renderer, nullptr);
  }

public:
  AirTextBox(Kotonoha::Engine &engine,
             const Kotonoha::Providers::LucaSystemAssets &assets)
      : VNTextBox(engine, "", "", 30),
        frames_{{tryReadCz(assets, "MWIN0"), tryReadCz(assets, "MWIN1"),
                 tryReadCz(assets, "MWIN2"), tryReadCz(assets, "MWIN3"),
                 tryReadCz(assets, "MWIN4")}},
        fallbackStrip_(tryReadCz(assets, "MWIN1280_0")),
        cursor_(tryReadCz(assets, "MWIN_CURSOR")) {}
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
  Kotonoha::InGameMenu *menu = nullptr;
  std::unique_ptr<Kotonoha::VNSaveMenu> saveMenu;

  bool running = false;
  bool titleShown = false;
  bool originalTitle = true;
  CzImage titleBackground;
  CzImage titleMenu;
  Kotonoha::TexturePtr titleBackgroundTexture;
  Kotonoha::TexturePtr titleMenuTexture;
  int titleSelection = 0;

  // VM state
  std::vector<ScriptInstruction> instructions;
  size_t instructionIndex = 0;
  std::unordered_map<uint32_t, CzImage> objectImages;
  std::unordered_map<uint32_t, Kotonoha::TexturePtr> objectTextures;
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
  std::uint64_t lastDrawDeadline = 0;
  bool waiting = false;
  bool skipMode = false;
  bool autoMode = false;

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
  uint32_t currentBgmId = 0;
  uint32_t lastPlayedVoiceId = 0;

  std::unique_ptr<Kotonoha::MediaPlayer> moviePlayer;
  bool movieFinished = false;

  struct SplashLogo {
    std::string assetName;
    CzImage image;
    Kotonoha::TexturePtr texture;
  };
  std::vector<SplashLogo> splashLogos;
  size_t currentSplashIndex = 0;
  uint64_t splashStartTime = 0;
  bool showingSplash = false;

  SceneLayers sceneLayers;

  explicit Impl(LucaSystemRuntime &r,
                std::shared_ptr<Kotonoha::Providers::LucaSystemAssets> a)
      : runtime(r), assets(std::move(a)) {
    loadSystemTables();
    loadGlobalState();
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
            if (ins.opcode == OP_VARSTR_SET && ins.paramBytes.size() >= 6) {
              size_t cursor = 0;
              std::string scen = ReadLsUtf8(ins.paramBytes, cursor);
              if (cursor + 2 <= ins.paramBytes.size()) {
                uint16_t strId = paramU16(ins, cursor);
                cursor += 2;
                std::string jp = ReadLsUtf16(ins.paramBytes, cursor);
                std::string en = ReadLsUtf16(ins.paramBytes, cursor);
                std::string zh = ReadLsUtf16(ins.paramBytes, cursor);
                const std::string &chosen =
                    !en.empty() ? en : (!jp.empty() ? jp : zh);
                if (!scen.empty() && !chosen.empty())
                  varStrTable[{scen, strId}] = chosen;
              }
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
    static const char *names[] = {"logo0",    "logo1",   "logo2",
                                  "logo_key", "logo_va", "logo_prototype"};
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
    if (!splashLogos.empty()) {
      showingSplash = true;
      currentSplashIndex = 0;
      splashStartTime = SDL_GetTicks();
    }
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

  void enterTitleScreen() {
    stopAudio();
    moviePlayer.reset();
    movieFinished = true;
    running = false;
    waiting = false;
    choiceActive = false;
    dialogueVisible = false;
    if (textbox)
      textbox->hide();
    if (choiceScene)
      choiceScene->hide();
    stagedSlots.clear();
    visibleSlots.clear();
    outgoingSlots.clear();
    sceneLayers.reset();
    originalTitle = true;
    titleShown = true;
    titleBackgroundTexture.reset();
    try {
      const auto bgName = titleBackgroundName();
      if (assets->contains(bgName))
        titleBackground = DecodeCzImage(assets->read(bgName));
      if (titleMenu.rgba.empty() && assets->contains("title_menu"))
        titleMenu = DecodeCzImage(assets->read("title_menu"));
    } catch (...) {
    }
    // Play AIR Title BGM (trk22 / 100021) when available
    if (assets->hasBgmAsset(100021))
      playBgm(100021, true);
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
    for (const auto &line : dialogueLines)
      if (std::find(runtime.scenarioTexts_.begin(),
                    runtime.scenarioTexts_.end(),
                    line) == runtime.scenarioTexts_.end())
        runtime.scenarioTexts_.push_back(line);
    return true;
  }

  void stopAudio() {
    bgm.Stop();
    voice.Stop();
    se.Stop();
    syssePlayer.Stop();
    currentBgmId = 0;
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
    if (id == 0) {
      bgm.Stop();
      currentBgmId = 0;
      return;
    }
    if (currentBgmId == id)
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

  void playSe(uint32_t id, bool loop = false) {
    if (id == 0) {
      se.Stop();
      return;
    }
    if (!engine || !assets)
      return;
    try {
      playDecodedAudio(se, "Se", "se:" + std::to_string(id), 2, 48000, loop);
    } catch (const std::exception &e) {
      SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "LucaSystem: se %u: %s", id,
                  e.what());
    }
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
            movieFinished = false;
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
    try {
      objectImages[id] = DecodeCzImage(assets->readImageAsset(id));
      objectTextures[id].reset();
    } catch (const std::exception &e) {
      SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "LucaSystem: image %u: %s", id,
                  e.what());
    }
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
      obj.priority = int(slot.group) * 100 + int(slot.slot);
      obj.visible = slot.visible;
      obj.r = slot.r;
      obj.g = slot.g;
      obj.b = slot.b;
      flat.push_back(obj);
    }
    sceneLayers.reset(flat);
  }

  void commitStagedSlots(uint64_t now, uint64_t durationMs, bool wipe) {
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
    syncLegacySceneLayers();
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
                          float opacityMod = 1.0f, float rightClip = -1.0f) {
    if (!slot.visible || slot.assetId == 0 || slot.alpha <= 0.0f ||
        opacityMod <= 0.0f)
      return;
    loadObjectImage(slot.assetId);
    auto it = objectImages.find(slot.assetId);
    if (it == objectImages.end())
      return;
    const auto &img = it->second;
    auto &tex = objectTextures[slot.assetId];
    if (!tex) {
      tex.reset(SDL_CreateTexture(&renderer, SDL_PIXELFORMAT_RGBA32,
                                  SDL_TEXTUREACCESS_STATIC, img.width,
                                  img.height));
      if (!tex || !SDL_UpdateTexture(tex.get(), nullptr, img.rgba.data(),
                                     img.width * 4))
        return;
      SDL_SetTextureScaleMode(tex.get(),
                              (slot.assetId == 65000 || slot.assetId == 65001)
                                  ? SDL_SCALEMODE_NEAREST
                                  : SDL_SCALEMODE_LINEAR);
      SDL_SetTextureBlendMode(tex.get(), SDL_BLENDMODE_BLEND);
    }

    const float alpha = std::clamp(slot.alpha * opacityMod, 0.0f, 1.0f);
    SDL_SetTextureAlphaMod(tex.get(),
                           static_cast<Uint8>(std::lround(alpha * 255.0f)));
    SDL_SetTextureColorMod(tex.get(), slot.r, slot.g, slot.b);

    float offX = slot.targetOffsetX;
    float offY = slot.targetOffsetY;
    if (slot.moveDuration > 0 && now < slot.moveStart + slot.moveDuration) {
      const float t = float(now > slot.moveStart ? (now - slot.moveStart) : 0) /
                      float(slot.moveDuration);
      offX = slot.startOffsetX + (slot.targetOffsetX - slot.startOffsetX) * t;
      offY = slot.startOffsetY + (slot.targetOffsetY - slot.startOffsetY) * t;
    }

    const uint8_t shakeKey =
        (slot.group == 0) ? 0 : static_cast<uint8_t>(1 + slot.slot);
    const auto [shakeX, shakeY] = computeShakeOffset(now, shakeKey);

    float drawX = 0.0f;
    float drawY = 0.0f;
    float drawW = float(img.width) * slot.scaleX;
    float drawH = float(img.height) * slot.scaleY;

    if (slot.assetId == 65000 || slot.assetId == 65001) {
      drawX = 0.0f;
      drawY = 0.0f;
      drawW = 1280.0f;
      drawH = 720.0f;
    } else if (img.anchorX != 0 || img.anchorY != 0) {
      drawX = slot.posX - float(img.anchorX) + float(img.cropX) + offX + shakeX;
      drawY = slot.posY - float(img.anchorY) + float(img.cropY) + offY + shakeY;
    } else {
      drawX = slot.posX + float(img.cropX) + offX + shakeX;
      drawY = slot.posY + float(img.cropY) + offY + shakeY;
    }

    SDL_FRect dst{drawX, drawY, drawW, drawH};
    SDL_FRect source{};
    const SDL_FRect *sourcePtr = nullptr;
    if (rightClip >= 0.0f) {
      const float visibleWidth = wipeVisibleWidth(drawX, drawW, rightClip);
      if (visibleWidth <= 0.0f)
        return;
      const float scaleFactor = (slot.assetId == 65000 || slot.assetId == 65001)
                                    ? 1280.0f
                                    : slot.scaleX;
      source = SDL_FRect{0, 0, visibleWidth / scaleFactor, float(img.height)};
      sourcePtr = &source;
      dst.w = visibleWidth;
    }
    if (slot.rotation != 0.0f) {
      SDL_RenderTextureRotated(&renderer, tex.get(), sourcePtr, &dst,
                               double(slot.rotation), nullptr, SDL_FLIP_NONE);
    } else {
      SDL_RenderTexture(&renderer, tex.get(), sourcePtr, &dst);
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
        if (ins.paramBytes.size() >= 2) {
          const uint16_t voiceId = paramU16(ins, 0);
          if (voiceId != 0)
            playVoice(voiceId);
        }
        if (!ins.text.empty()) {
          std::string speaker, text;
          parseSpeakerAndSanitizeText(ins.text, speaker, text);
          dialogueVisible = true;
          runtime.dialogueText_ = text;
          runtime.dialogueSpeaker_ = speaker;
          if (textbox) {
            textbox->setWindowType(getVar(6002));
            textbox->setDialogue(speaker, text);
            textbox->show();
          }
          if (backlog)
            backlog->addEntry(speaker, text);
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
            backlog->addEntry(spk, clean);
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
        waitDeadline = SDL_GetTicks() + 250;
        return;
      }

      case OP_WAIT:
      case OP_WAIT_BSKIP: {
        const uint64_t now = SDL_GetTicks();
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
          waitDeadline = SDL_GetTicks() + ms;
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
            for (auto it = stagedSlots.begin(); it != stagedSlots.end();) {
              if (it->second.group == 0)
                it = stagedSlots.erase(it);
              else
                ++it;
            }
          } else if (group == 0xff && slot == 0xff) {
            stagedSlots.clear();
          } else if (slot == 0xff) {
            for (auto it = stagedSlots.begin(); it != stagedSlots.end();) {
              if (it->second.group == group)
                it = stagedSlots.erase(it);
              else
                ++it;
            }
          } else {
            stagedSlots.erase(makeKey(group, slot));
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
          loadObjectImage(assetId);
          SlottedLayer layer;
          layer.group = group;
          layer.slot = slot;
          layer.assetId = assetId;
          layer.posX = 0.0f;
          layer.posY = 0.0f;
          stagedSlots[makeKey(group, slot)] = layer;
        } else if (ins.paramBytes.size() >= 14) {
          const uint8_t group = ins.paramBytes[0];
          const uint8_t slot = ins.paramBytes[1];
          const uint16_t assetId = paramU16(ins, 2);
          const float posX = paramFloat(ins, 6);
          const float posY = paramFloat(ins, 10);
          loadObjectImage(assetId);
          SlottedLayer layer;
          layer.group = group;
          layer.slot = slot;
          layer.assetId = assetId;
          layer.posX = posX;
          layer.posY = posY;
          stagedSlots[makeKey(group, slot)] = layer;
        } else if (ins.paramBytes.size() >= 6) {
          const uint32_t id = paramU16(ins, 0);
          const float x = float(int16_t(paramU16(ins, 2)));
          const float y = float(int16_t(paramU16(ins, 4)));
          loadObjectImage(id);
          SlottedLayer layer;
          layer.group = 0;
          layer.slot = 0;
          layer.assetId = id;
          layer.posX = x;
          layer.posY = y;
          stagedSlots[makeKey(0, 0)] = layer;
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
          } else if (group == 0 && slot >= 1) {
            auto charIt = stagedSlots.find(makeKey(2, slot - 1));
            if (charIt != stagedSlots.end())
              charIt->second.alpha = alpha;
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
        const uint64_t now = SDL_GetTicks();
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
        const uint64_t now = SDL_GetTicks();
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
          const uint64_t now = SDL_GetTicks();
          const uint64_t dur = skipMode ? 0 : framesToMs(frames);
          const auto applyMove = [&](SlottedLayer &l) {
            l.startOffsetX = l.targetOffsetX;
            l.startOffsetY = l.targetOffsetY;
            l.targetOffsetX += dx;
            l.targetOffsetY += dy;
            l.moveStart = now;
            l.moveDuration = dur;
          };
          auto sit = stagedSlots.find(makeKey(group, slot));
          if (sit != stagedSlots.end())
            applyMove(sit->second);
          auto vit = visibleSlots.find(makeKey(group, slot));
          if (vit != visibleSlots.end())
            applyMove(vit->second);
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
          shakeStartTick = SDL_GetTicks();
        }
        break;
      }

      case OP_HAIKEI_SET: {
        uint32_t id = ins.paramBytes.size() >= 2 ? paramU16(ins, 0) : 0;
        if (id > 0) {
          loadObjectImage(id);
          SlottedLayer layer;
          layer.group = 0;
          layer.slot = 0;
          layer.assetId = id;
          stagedSlots[makeKey(0, 0)] = layer;
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
        if (ins.paramBytes.size() >= 6) {
          const uint16_t dstStrVar = paramU16(ins, 0);
          const uint16_t tableId = paramU16(ins, 4);
          auto it = varStrTable.find({currentScenario, tableId});
          if (it != varStrTable.end())
            stringVars[dstStrVar] = it->second;
        }
        break;
      }
      case OP_VARSTR_SET:
        break;

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
      case OP_BGM_ASYNC_FADE_STOP:
        playBgm(0, false);
        break;
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
          playSe(seId, loop);
        } else if (ins.paramBytes.size() >= 2) {
          playSe(paramU16(ins, 0), false);
        } else {
          se.Stop();
        }
        break;
      }
      case OP_SE_STOP:
      case OP_SE_WAIT:
      case OP_SE_WAIT_COUNT:
      case OP_SE_WAIT_FADE:
        if (ins.opcode == OP_SE_STOP)
          se.Stop();
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
            flashStartTick = SDL_GetTicks();
            flashDurationMs = std::max<uint64_t>(120, framesToMs(frames));
          } else if (taskId == 3 || taskId == 4) {
            // task::EndRoll995 / task::EndRoll900 ending staff roll
            skipMode = false;
            if (!skipMode) {
              waiting = true;
              waitDeadline = SDL_GetTicks() + 1500;
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
        commitStagedSlots(SDL_GetTicks(), 0, false);
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
      commitStagedSlots(SDL_GetTicks(), 0, false);
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

  void launchChapterFromTitle(const std::string &scenarioName) {
    originalTitle = false;
    titleShown = false;
    titleBackgroundTexture.reset();
    bgm.Stop();
    currentBgmId = 0;
    runtime.ClearScenes();
    runtime.AddScene(textbox.get(), Kotonoha::GameState::Gameplay);
    runtime.AddScene(choiceScene, Kotonoha::GameState::Gameplay);
    runtime.AddScene(backlog, Kotonoha::GameState::Gameplay);
    runtime.AddScene(menu, Kotonoha::GameState::Gameplay);
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
  impl.choiceScene = nullptr;
  impl.backlog = nullptr;
  impl.menu = nullptr;
  impl.titleBackgroundTexture.reset();
  impl.titleMenuTexture.reset();
  impl.objectTextures.clear();
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
  (void)Kotonoha::GetVNManagerRuntime(engine);
  impl.textbox = std::make_unique<AirTextBox>(engine, *impl.assets);
  impl.choiceScene = &Kotonoha::GetChoiceQuestionRuntime(engine);
  impl.backlog = &Kotonoha::GetVNBacklogRuntime(engine);
  impl.menu = &Kotonoha::GetInGameMenuRuntime(engine);
  impl.saveMenu = std::make_unique<Kotonoha::VNSaveMenu>(engine);

  auto &menu = *impl.menu;
  menu.SetOnReturnToMenu([this] { impl_->enterTitleScreen(); });
  menu.SetOnSave([this] {
    auto path = Kotonoha::saveDirectory() / "quicksave.kns";
    saveGame(path.string());
  });
  menu.SetOnLoad([this] {
    auto path = Kotonoha::saveDirectory() / "quicksave.kns";
    if (std::filesystem::exists(path))
      loadGame(path.string());
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
    AddScene(impl.choiceScene);
    return;
  }

  ClearScenes();
  AddScene(impl.textbox.get());
  AddScene(impl.choiceScene);
  AddScene(impl.backlog);
  AddScene(impl.menu);
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
  if (impl.engine && impl.running && !impl.dialogueVisible &&
      !impl.choiceActive && !impl.engine->isStoryPaused() && !impl.waiting) {
    impl.runUntilPause();
  }
  if (impl.waiting && (!impl.engine || !impl.engine->isStoryPaused()) &&
      !impl.moviePlayer) {
    if (SDL_GetTicks() >= impl.waitDeadline) {
      impl.waiting = false;
      impl.runUntilPause();
    }
  }
}

void LucaSystemRuntime::renderGameplay(Kotonoha::RendererRef renderer,
                                       float scale) {
  auto &impl = *impl_;
  const uint64_t now = SDL_GetTicks();

  if (impl.showingSplash && !impl.splashLogos.empty()) {
    const uint64_t elapsed = now - impl.splashStartTime;
    constexpr uint64_t logoDuration = 1800;
    const size_t index = static_cast<size_t>(elapsed / logoDuration);
    if (index >= impl.splashLogos.size()) {
      impl.showingSplash = false;
    } else {
      auto &logo = impl.splashLogos[index];
      if (!logo.texture && !logo.image.rgba.empty()) {
        logo.texture.reset(SDL_CreateTexture(
            &renderer, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STATIC,
            logo.image.width, logo.image.height));
        if (logo.texture) {
          SDL_UpdateTexture(logo.texture.get(), nullptr, logo.image.rgba.data(),
                            logo.image.width * 4);
          SDL_SetTextureBlendMode(logo.texture.get(), SDL_BLENDMODE_BLEND);
        }
      }
      const uint64_t logoElapsed = elapsed % logoDuration;
      float alpha = 1.0f;
      if (logoElapsed < 350)
        alpha = float(logoElapsed) / 350.0f;
      else if (logoElapsed > logoDuration - 350)
        alpha = float(logoDuration - logoElapsed) / 350.0f;
      alpha = std::clamp(alpha, 0.0f, 1.0f);

      SDL_SetRenderDrawColor(&renderer, 0, 0, 0, 255);
      SDL_FRect screen{0, 0, 1280, 720};
      SDL_RenderFillRect(&renderer, &screen);
      if (logo.texture) {
        SDL_SetTextureAlphaMod(logo.texture.get(),
                               static_cast<Uint8>(std::lround(alpha * 255.0f)));
        SDL_RenderTexture(&renderer, logo.texture.get(), nullptr, &screen);
      }
      return;
    }
  }

  if (impl.moviePlayer && !impl.movieFinished && impl.moviePlayer->isOpen()) {
    SDL_FRect screen{0, 0, 1280, 720};
    impl.moviePlayer->render(renderer, screen, nullptr, true);
    return;
  }

  if (impl.originalTitle) {
    auto texture = [&renderer](Kotonoha::TexturePtr &target,
                               const CzImage &image) -> SDL_Texture * {
      if (image.rgba.empty())
        return nullptr;
      if (!target) {
        target.reset(SDL_CreateTexture(&renderer, SDL_PIXELFORMAT_RGBA32,
                                       SDL_TEXTUREACCESS_STATIC, image.width,
                                       image.height));
        if (!target || !SDL_UpdateTexture(target.get(), nullptr,
                                          image.rgba.data(), image.width * 4))
          return nullptr;
        SDL_SetTextureScaleMode(target.get(), SDL_SCALEMODE_LINEAR);
        SDL_SetTextureBlendMode(target.get(), SDL_BLENDMODE_BLEND);
      }
      return target.get();
    };
    SDL_FRect screen{0, 0, 1280, 720};
    if (auto *bgTex =
            texture(impl.titleBackgroundTexture, impl.titleBackground)) {
      SDL_RenderTexture(&renderer, bgTex, nullptr, &screen);
    }
    if (auto *menuTex = texture(impl.titleMenuTexture, impl.titleMenu)) {
      SDL_FRect source{0, 0, 1856, 70};
      SDL_FRect destination{0, 620, 1280, 48.2759f};
      SDL_RenderTexture(&renderer, menuTex, &source, &destination);
      static constexpr int groups[][2] = {
          {15, 222},    {258, 542},   {577, 809},   {842, 950},  {984, 1154},
          {1188, 1352}, {1386, 1466}, {1500, 1679}, {1714, 1855}};
      const auto &group = groups[std::clamp(impl.titleSelection, 0, 8)];
      const float menuScale = 1280.0f / 1856.0f;
      SDL_FRect activeSource{float(group[0]), 70,
                             float(group[1] - group[0] + 1), 70};
      SDL_FRect activeDestination{group[0] * menuScale, 620,
                                  activeSource.w * menuScale, 48.2759f};
      SDL_RenderTexture(&renderer, menuTex, &activeSource, &activeDestination);
    }
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
      for (const auto &[key, slot] : impl.outgoingSlots)
        impl.renderSlottedLayer(renderer, slot, now, 1.0f);
      const float revealRight = 1280.0f * t;
      for (const auto &[key, slot] : impl.visibleSlots)
        impl.renderSlottedLayer(renderer, slot, now, 1.0f, revealRight);
    } else {
      for (const auto &[key, outSlot] : impl.outgoingSlots) {
        auto visIt = impl.visibleSlots.find(key);
        if (visIt != impl.visibleSlots.end() &&
            visIt->second.sameVisual(outSlot)) {
          // Unchanged layer: render once at full opacity so alpha doesn't dip
          impl.renderSlottedLayer(renderer, outSlot, now, 1.0f);
        } else {
          impl.renderSlottedLayer(renderer, outSlot, now, 1.0f - t);
        }
      }
      for (const auto &[key, visSlot] : impl.visibleSlots) {
        auto outIt = impl.outgoingSlots.find(key);
        if (outIt != impl.outgoingSlots.end() &&
            outIt->second.sameVisual(visSlot))
          continue;
        impl.renderSlottedLayer(renderer, visSlot, now, t);
      }
    }
  } else {
    for (const auto &[key, slot] : impl.visibleSlots)
      impl.renderSlottedLayer(renderer, slot, now, 1.0f);
  }

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

  Runtime::renderGameplay(renderer, scale);
}

void LucaSystemRuntime::advanceDialogue() { impl_->advanceDialogue(); }

bool LucaSystemRuntime::handleAdvanceAction() {
  auto &impl = *impl_;
  if (!impl.dialogueVisible)
    return false;
  impl.advanceDialogue();
  return true;
}

bool LucaSystemRuntime::onEvent(Kotonoha::EventRef event) {
  auto mapped = Kotonoha::MapControllerEvent(event);
  auto &impl = *impl_;

  if (impl.showingSplash && (mapped.type == SDL_EVENT_KEY_DOWN ||
                             mapped.type == SDL_EVENT_MOUSE_BUTTON_DOWN ||
                             mapped.type == SDL_EVENT_FINGER_DOWN ||
                             mapped.type == SDL_EVENT_GAMEPAD_BUTTON_DOWN)) {
    if (mapped.type != SDL_EVENT_KEY_DOWN || !mapped.key.repeat) {
      impl.showingSplash = false;
      return true;
    }
  }

  if (impl.moviePlayer && !impl.movieFinished &&
      (mapped.type == SDL_EVENT_KEY_DOWN ||
       mapped.type == SDL_EVENT_MOUSE_BUTTON_DOWN ||
       mapped.type == SDL_EVENT_FINGER_DOWN ||
       mapped.type == SDL_EVENT_GAMEPAD_BUTTON_DOWN)) {
    if (mapped.type != SDL_EVENT_KEY_DOWN || !mapped.key.repeat) {
      skipMovie();
      return true;
    }
  }

  if (impl.choiceActive && impl.choiceScene)
    return Runtime::onEvent(mapped);

  static constexpr int menuGroups[][2] = {
      {15, 222},    {258, 542},   {577, 809},   {842, 950},  {984, 1154},
      {1188, 1352}, {1386, 1466}, {1500, 1679}, {1714, 1855}};
  const auto titleHit = [&](float x, float y) {
    if (y < 610 || y > 690)
      return -1;
    const float atlasX = x * 1856.0f / 1280.0f;
    for (int i = 0; i < 9; ++i)
      if (atlasX >= menuGroups[i][0] && atlasX <= menuGroups[i][1])
        return i;
    return -1;
  };
  if (impl.originalTitle && mapped.type == SDL_EVENT_MOUSE_MOTION) {
    const int hit = titleHit(mapped.motion.x, mapped.motion.y);
    if (hit >= 0 && hit != impl.titleSelection) {
      impl.titleSelection = hit;
      impl.playSysSe("CURSOR");
    }
    return hit >= 0;
  }
  if (impl.originalTitle && mapped.type == SDL_EVENT_MOUSE_BUTTON_DOWN &&
      mapped.button.button == SDL_BUTTON_LEFT) {
    const int hit = titleHit(mapped.button.x, mapped.button.y);
    if (hit < 0)
      return false;
    impl.titleSelection = hit;
    SDL_Event activate{};
    activate.type = SDL_EVENT_KEY_DOWN;
    activate.key.key = SDLK_RETURN;
    activate.key.repeat = false;
    return onEvent(activate);
  }
  if (impl.originalTitle && mapped.type == SDL_EVENT_KEY_DOWN &&
      !mapped.key.repeat &&
      (mapped.key.key == SDLK_LEFT || mapped.key.key == SDLK_RIGHT)) {
    impl.titleSelection =
        (impl.titleSelection + (mapped.key.key == SDLK_RIGHT ? 1 : 8)) % 9;
    impl.playSysSe("CURSOR");
    return true;
  }
  if (impl.originalTitle && mapped.type == SDL_EVENT_KEY_DOWN &&
      !mapped.key.repeat &&
      (mapped.key.key == SDLK_RETURN || mapped.key.key == SDLK_SPACE)) {
    impl.playSysSe("ENTER");
    switch (impl.titleSelection) {
    case 0: // NEW GAME -> DREAM ("seen170")
      impl.launchChapterFromTitle(impl.startupScenario());
      return true;
    case 1: { // SELECT STORY -> Chapter Selector
      if (impl.choiceScene) {
        impl.choiceActive = true;
        std::vector<Kotonoha::ChoiceOption> chapters = {
            {"DREAM", 0},
            {"SUMMER", 1},
            {"AIR", 2},
            {"FIRST SKY (Hatsuzora)", 3},
            {"Cancel", 4}};
        impl.choiceScene->setQuestion(
            "SELECT STORY", chapters, [this](int chId, const std::string &) {
              impl_->choiceActive = false;
              if (impl_->choiceScene)
                impl_->choiceScene->hide();
              static const char *roots[] = {"seen170", "seen700", "seen163",
                                            "seen800"};
              if (chId >= 0 && chId < 4)
                impl_->launchChapterFromTitle(roots[chId]);
            });
      }
      return true;
    }
    case 2: // START "AIR" -> seen163
      impl.launchChapterFromTitle("seen163");
      return true;
    case 3: { // LOAD
      const auto savePath = Kotonoha::saveDirectory() / "quicksave.kns";
      if (std::filesystem::exists(savePath)) {
        impl.originalTitle = false;
        initGameplay(*impl.engine);
        loadGame(savePath.string());
      }
      return true;
    }
    case 4: // OPTIONS
      if (impl.menu) {
        SetState(Kotonoha::GameState::Menu);
        initMenu(*impl.engine);
      }
      return true;
    case 6: { // EXIT (button 6 at [1386..1466] in SYSCG.PAK/title_menu!)
      SDL_Event quit{};
      quit.type = SDL_EVENT_QUIT;
      SDL_PushEvent(&quit);
      return true;
    }
    case 8: { // MUSIC -> cycle/listen to MUSIC.PAK BGM tracks
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
              if (tid > 0)
                impl_->playBgm(static_cast<uint32_t>(tid), true);
            });
      }
      return true;
    }
    default:
      return true;
    }
  }

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
  }
  if (!impl.originalTitle && mapped.type == SDL_EVENT_KEY_UP &&
      mapped.key.key == SDLK_LCTRL) {
    impl.skipMode = false;
  }

  if (impl.dialogueVisible && (mapped.type == SDL_EVENT_KEY_DOWN ||
                               mapped.type == SDL_EVENT_MOUSE_BUTTON_DOWN ||
                               mapped.type == SDL_EVENT_FINGER_DOWN)) {
    if (mapped.type != SDL_EVENT_KEY_DOWN || !mapped.key.repeat) {
      advanceDialogue();
      return true;
    }
  }
  return Runtime::onEvent(mapped);
}

void LucaSystemRuntime::saveGame(const std::string &path) {
  const auto &impl = *impl_;
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
    const auto now = SDL_GetTicks();
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
  for (const auto &[key, slot] : impl.visibleSlots) {
    if (!slot.visible || slot.assetId == 0)
      continue;
    // Encode (group, slot) in high bits of assetId only? No, keep assetId pure
    // for SaveState V5 compatibility and store group/slot in x/y or re-derive
    // group from assetId range (10001..10999 -> group 2, else group 0).
    state.images.push_back({slot.assetId,
                            static_cast<int32_t>(std::lround(slot.posX)),
                            static_cast<int32_t>(std::lround(slot.posY))});
  }
  if (impl.engine) {
    Kotonoha::GetVNManagerRuntime(*impl.engine)
        .save(path, encodeSaveState(state));
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
  impl.stagedSlots = impl.visibleSlots;
  impl.syncLegacySceneLayers();

  if (auto bgmIt = impl.numericVars.find(99001);
      bgmIt != impl.numericVars.end() && bgmIt->second > 0) {
    impl.playBgm(static_cast<uint32_t>(bgmIt->second), true);
  }

  impl.dialogueIndex = state.dialogueIndex;
  impl.waiting = state.waiting;
  impl.waitDeadline = SDL_GetTicks() + state.waitRemainingMs;

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
  dialogueText_ = impl.dialogueVisible ? cleanText : std::string{};
  dialogueSpeaker_ = impl.dialogueVisible ? spk : std::string{};
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
  impl.instructionIndex = position;
  impl.callStack.clear();
  impl.stagedSlots.clear();
  impl.visibleSlots.clear();
  impl.outgoingSlots.clear();
  impl.sceneLayers.reset();
  impl.waiting = false;
  impl.choiceActive = false;
  impl.running = position < impl.instructions.size();
  dialogueText_.clear();
  dialogueSpeaker_.clear();
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

void LucaSystemRuntime::debugRestartScene() {
  if (impl_->currentScenario.empty() || !startScenario(impl_->currentScenario))
    throw std::runtime_error("There is no active LucaSystem scene to restart");
}

std::vector<std::string> LucaSystemRuntime::activeTranslationKeys() const {
  return originalActiveKeys_;
}

Kotonoha::Translation::SourceTexts
LucaSystemRuntime::translationSources() const {
  return scenarioTexts_.empty()
             ? Kotonoha::Translation::SourceTexts{}
             : Kotonoha::Translation::SourceTexts{{0, scenarioTexts_}};
}

} // namespace LucaSystem
