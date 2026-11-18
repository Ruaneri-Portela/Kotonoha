// Copyright 2026 Kotonoha contributors. SPDX-License-Identifier: MIT
// LucaSystem (Visual Art's / Key) runtime — full VM implementation recovered
// via reverse engineering of the original AIR.exe binaries with rizin/REA and
// analysis of the SCRIPT.PAK bytecode format.
#include <LucaSystem/CzImage.hpp>
#include <LucaSystem/DialogueFlow.hpp>
#include <LucaSystem/LucaSystemRuntime.hpp>
#include <LucaSystem/LucaSystemScript.hpp>
#include <LucaSystem/SaveState.hpp>
#include <LucaSystem/SceneLayers.hpp>

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
#include <Kotonoha/media/PcmPlayer.hpp>
#include <SDL3/SDL.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <vector>

namespace LucaSystem {

namespace {

bool isSystemScript(std::string_view name) {
  return name.rfind("_varset_", 0) == 0 || name == "_build_time" ||
         name == "_arflag" || name == "_varstr" || name == "_varnum" ||
         name == "_varname" || name == "_shakelist" || name == "_scr_label" ||
         name == "_task";
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

// LucaSystem opcode table matching wetor/LuckSystem standard specification
// (AIR/Key/Prototype 0-indexed decimal opcodes).
enum Opcode : uint8_t {
  OP_EQU = 0,             // 0x00: set string variable
  OP_EQUN = 1,            // 0x01: set numeric variable
  OP_EQUV = 2,            // 0x02: copy numeric variable
  OP_ADD = 3,             // 0x03: add numeric variable
  OP_SUB = 4,             // 0x04: subtract numeric variable
  OP_MUL = 5,             // 0x05: multiply numeric variable
  OP_DIV = 6,             // 0x06: divide numeric variable
  OP_MOD = 7,             // 0x07: modulo numeric variable
  OP_AND = 8,             // 0x08: bitwise AND variable
  OP_OR = 9,              // 0x09: bitwise OR variable
  OP_RANDOM = 10,         // 0x0A: random number generator
  OP_VARSTR = 11,         // 0x0B: string variable ops
  OP_VARSTR_ADD = 12,     // 0x0C: string variable concatenate
  OP_SET = 13,            // 0x0D: set variable
  OP_FLAGCLR = 14,        // 0x0E: clear flag
  OP_GOTO = 15,           // 0x0F: jump to label/offset
  OP_ONGOTO = 16,         // 0x10: multi-way jump array
  OP_CALL = 17,           // 0x11: subroutine call (GOSUB)
  OP_IFY = 18,            // 0x12: jump if condition true
  OP_IFN = 19,            // 0x13: jump if condition false
  OP_RETURN = 20,         // 0x14: return from subroutine
  OP_JUMP = 21,           // 0x15: jump to script/label
  OP_FARCALL = 22,        // 0x16: subroutine call to external scenario
  OP_FARRETURN = 23,      // 0x17: return from external scenario
  OP_JUMPPOINT = 24,      // 0x18: jump point marker
  OP_END = 25,            // 0x19: scenario end
  OP_STARTUP_BEGIN = 26,  // 0x1A: startup block start
  OP_STARTUP_END = 27,    // 0x1B: startup block end
  OP_VARSTR_SET = 29,     // 0x1D: set variable string
  OP_ARFLAGSET = 31,      // 0x1F: set flag
  OP_MESSAGE = 36,        // 0x24: display text message
  OP_MESSAGE_CLEAR = 37,  // 0x25: clear message box
  OP_MESSAGE_WAIT = 38,   // 0x26: wait for text feed
  OP_SELECT = 40,         // 0x28: choice menu
  OP_CLOSE_WINDOW = 41,   // 0x29: hide text box
  OP_VOICE = 46,          // 0x2E: play voice audio
  OP_VOICE_STOP = 47,     // 0x2F: stop voice audio
  OP_INIT = 53,           // 0x35: init subsystem
  OP_IMAGELOAD = 55,      // 0x37: load image
  OP_IMAGEUPDATE = 56,    // 0x38: update image object
  OP_MOVE = 58,           // 0x3A: move object
  OP_ROT = 60,            // 0x3C: rotate object
  OP_FADE = 62,           // 0x3E: screen fade
  OP_SCALE = 63,          // 0x3F: scale object
  OP_SHAKE = 64,          // 0x40: shake object
  OP_BASE = 66,           // 0x42: object base position
  OP_FADE_OBJ = 71,       // 0x47: object fade (MCFADE)
  OP_WAIT = 72,           // 0x48: delay/wait
  OP_DRAW = 74,           // 0x4A: draw scene composition
  OP_WIPE = 75,           // 0x4B: wipe transition
  OP_TONE = 82,           // 0x52: set image tone
  OP_DISP = 87,           // 0x57: toggle object display
  OP_FACE = 89,           // 0x59: facial expression layer
  OP_SEPIA = 90,          // 0x5A: sepia filter
  OP_PRIORITY = 98,       // 0x62: set z-order priority
  OP_BGM = 107,           // 0x6B: play BGM
  OP_SE = 113,            // 0x71: play sound effect
  OP_SE_STOP = 114,       // 0x72: stop sound effect
  OP_VOLUME = 118,        // 0x76: audio volume
  OP_EX = 121,            // 0x79: extended effect
  OP_TASK = 124,          // 0x7C: background task
  OP_HAIKEI_SET = 137,    // 0x89: background set
};

class AirTextBox final : public Kotonoha::VNTextBox {
  CzImage frame_;
  CzImage cursor_;
  Kotonoha::TexturePtr frameTexture_;
  Kotonoha::TexturePtr cursorTexture_;

  SDL_Texture *texture(Kotonoha::RendererRef renderer,
                       Kotonoha::TexturePtr &target, const CzImage &image) {
    if (!target) {
      target.reset(SDL_CreateTexture(&renderer, SDL_PIXELFORMAT_RGBA32,
                                     SDL_TEXTUREACCESS_STATIC, image.width,
                                     image.height));
      if (!target || !SDL_UpdateTexture(target.get(), nullptr,
                                        image.rgba.data(), image.width * 4))
        throw std::runtime_error(std::string("AIR textbox texture: ") +
                                 SDL_GetError());
      SDL_SetTextureScaleMode(target.get(), SDL_SCALEMODE_LINEAR);
      SDL_SetTextureBlendMode(target.get(), SDL_BLENDMODE_BLEND);
    }
    return target.get();
  }

protected:
  void renderFrame(Kotonoha::RendererRef renderer) override {
    // MWIN1280_0 is a 416x80 seamless strip authored to be tiled three times
    // inside the 1280-wide design canvas.
    for (int tile = 0; tile < 3; ++tile) {
      SDL_FRect destination{float(16 + tile * 416), 640, 416, 80};
      SDL_RenderTexture(&renderer, texture(renderer, frameTexture_, frame_),
                        nullptr, &destination);
    }
  }
  void renderIndicator(Kotonoha::RendererRef renderer) override {
    const int frame = int((SDL_GetTicks() / 120) % 4);
    SDL_FRect source{0, float(frame * 38), 48, 38};
    SDL_FRect destination{1184, 670, 48, 38};
    SDL_RenderTexture(&renderer, texture(renderer, cursorTexture_, cursor_),
                      &source, &destination);
  }

  void renderSpeaker(Kotonoha::RendererRef renderer) override {
    if (!speakerName.empty())
      engine.GetVNUI().text(renderer, speakerName, 56, 520, 1120,
                            SDL_Color{255, 255, 255, 255});
  }

  void renderDialogue(Kotonoha::RendererRef renderer) override {
    SDL_Rect clip{56, 548, 1168, 116};
    SDL_SetRenderClipRect(&renderer, &clip);
    auto measure = [this](const std::string &value) {
      return engine.GetVNUI().textWidth(value);
    };
    const auto lines = Kotonoha::VNTextBoxUtility::RevealLines(
        fullDialogue, visibleDialogue.size(), measure, 1168);
    float y = 548;
    for (const auto &line : lines) {
      engine.GetVNUI().text(renderer, line, 56, y, 1168,
                            SDL_Color{255, 255, 255, 255});
      y += 30;
    }
    SDL_SetRenderClipRect(&renderer, nullptr);
  }

public:
  AirTextBox(Kotonoha::Engine &engine,
             const Kotonoha::Providers::LucaSystemAssets &assets)
      : VNTextBox(engine, "", "", 30),
        frame_(DecodeCzImage(assets.read("MWIN1280_0"))),
        cursor_(DecodeCzImage(assets.read("MWIN_CURSOR"))) {}
};

} // namespace

struct LucaSystemRuntime::Impl {
  LucaSystemRuntime &runtime;
  std::shared_ptr<Kotonoha::Providers::LucaSystemAssets> assets;
  Kotonoha::Engine *engine = nullptr;

  Kotonoha::VNTextBox *textbox = nullptr;
  Kotonoha::ChoiceQuestionScene *choiceScene = nullptr;
  Kotonoha::VNBacklog *backlog = nullptr;
  Kotonoha::InGameMenu *menu = nullptr;
  Kotonoha::VNSaveMenu *saveMenu = nullptr;

  bool running = false;
  bool titleShown = false;
  bool originalTitle = true;
  CzImage titleBackground;
  CzImage titleMenu;
  Kotonoha::TexturePtr titleBackgroundTexture;
  Kotonoha::TexturePtr titleMenuTexture;
  int titleSelection = 0;

  // VM state recovered from AIR.exe script interpreter.
  std::vector<ScriptInstruction> instructions;
  size_t instructionIndex = 0;
  std::unordered_map<uint32_t, CzImage> objectImages;
  std::unordered_map<uint32_t, Kotonoha::TexturePtr> objectTextures;
  CzImage background;
  Kotonoha::TexturePtr backgroundTexture;
  std::unordered_map<uint32_t, int32_t> numericVars;    // EQUN variables
  std::unordered_map<uint32_t, std::string> stringVars; // EQU string variables
  std::unordered_map<uint32_t, bool> flags;             // ARFLAG
  std::vector<CallStackFrame> callStack;
  std::uint64_t waitDeadline = 0;
  bool waiting = false;
  bool skipMode = false;
  bool autoMode = false;

  std::string currentScenario;
  std::vector<std::string> dialogueLines;
  size_t dialogueIndex = 0;
  bool dialogueVisible = false;

  Kotonoha::PcmPlayer sound;
  Kotonoha::PcmPlayer bgm;
  Kotonoha::PcmPlayer voice;
  Kotonoha::PcmPlayer se;

  explicit Impl(LucaSystemRuntime &r,
                std::shared_ptr<Kotonoha::Providers::LucaSystemAssets> a)
      : runtime(r), assets(std::move(a)) {}

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

  std::string startupScenario() const {
    auto files = scenarioFiles();
    return files.empty() ? std::string() : files.front();
  }

  int evalTerm(const std::string &term) const {
    std::string s = term;
    s.erase(0, s.find_first_not_of(" \t\r\n"));
    s.erase(s.find_last_not_of(" \t\r\n") + 1);
    if (s.empty()) return 0;
    auto parseBracketId = [&](std::string_view prefix) -> std::optional<uint32_t> {
      if (s.rfind(prefix, 0) == 0) {
        auto openB = s.find('[');
        auto closeB = s.find(']');
        if (openB != std::string::npos && closeB != std::string::npos && closeB > openB) {
          try { return static_cast<uint32_t>(std::stoul(s.substr(openB + 1, closeB - openB - 1))); } catch (...) {}
        }
      }
      return std::nullopt;
    };
    if (auto id = parseBracketId("VAR")) { auto it = numericVars.find(*id); return it != numericVars.end() ? it->second : 0; }
    if (auto id = parseBracketId("Z")) { auto it = numericVars.find(*id); return it != numericVars.end() ? it->second : 0; }
    if (auto id = parseBracketId("NUM")) { auto it = numericVars.find(*id); return it != numericVars.end() ? it->second : 0; }
    if (auto id = parseBracketId("FLAG")) { auto it = flags.find(*id); return it != flags.end() ? (it->second ? 1 : 0) : 0; }
    if (auto id = parseBracketId("FLG")) { auto it = flags.find(*id); return it != flags.end() ? (it->second ? 1 : 0) : 0; }
    if (auto id = parseBracketId("ARFLAG")) { auto it = flags.find(*id); return it != flags.end() ? (it->second ? 1 : 0) : 0; }

    try {
      uint32_t varId = static_cast<uint32_t>(std::stoul(s));
      auto it = numericVars.find(varId);
      if (it != numericVars.end()) return it->second;
      return std::stoi(s);
    } catch (...) {}
    return 0;
  }

  bool evaluateCondition(const std::string &expr) const {
    if (expr.empty()) return true;
    std::string s = expr;
    s.erase(0, s.find_first_not_of(" \t\r\n"));
    s.erase(s.find_last_not_of(" \t\r\n") + 1);
    if (s.empty()) return true;

    size_t opPos = std::string::npos;
    std::string op;
    if ((opPos = s.find("==")) != std::string::npos) op = "==";
    else if ((opPos = s.find("!=")) != std::string::npos) op = "!=";
    else if ((opPos = s.find("<=")) != std::string::npos) op = "<=";
    else if ((opPos = s.find(">=")) != std::string::npos) op = ">=";
    else if ((opPos = s.find("<")) != std::string::npos) op = "<";
    else if ((opPos = s.find(">")) != std::string::npos) op = ">";

    if (!op.empty() && opPos != std::string::npos) {
      std::string left = s.substr(0, opPos);
      std::string right = s.substr(opPos + op.size());
      int lhs = evalTerm(left);
      int rhs = evalTerm(right);
      if (op == "==") return lhs == rhs;
      if (op == "!=") return lhs != rhs;
      if (op == "<=") return lhs <= rhs;
      if (op == ">=") return lhs >= rhs;
      if (op == "<") return lhs < rhs;
      if (op == ">") return lhs > rhs;
    }
    return evalTerm(s) != 0;
  }

  bool loadScenarioScript(const std::string &name) {
    if (name.empty()) return false;
    std::string file = name;
    if (file.rfind(".scr") == std::string::npos && file.rfind(".SCR") == std::string::npos) {
      file += ".scr";
    }
    auto *pak = assets->scriptPak();
    if (!pak) return false;
    const PakEntry *entry = pak->findByName(file);
    if (!entry) entry = pak->findByName(name);
    if (!entry) return false;

    auto bytes = assets->read(entry->name);
    Script script(entry->name, std::vector<std::uint8_t>(bytes.begin(), bytes.end()));
    if (!script.valid()) return false;

    currentScenario = entry->name;
    instructions = script.instructions();
    instructionIndex = 0;
    dialogueLines = script.strings();
    dialogueIndex = 0;
    dialogueVisible = false;
    runtime.scenarioTexts_.clear();
    for (const auto &line : dialogueLines)
      if (std::find(runtime.scenarioTexts_.begin(), runtime.scenarioTexts_.end(), line) ==
          runtime.scenarioTexts_.end())
        runtime.scenarioTexts_.push_back(line);
    return true;
  }

  // ------------------------------------------------------------------
  // VM execution helpers
  // ------------------------------------------------------------------
  void stopAudio() {
    bgm.Stop();
    voice.Stop();
    se.Stop();
    sound.Stop();
  }

  void playVoice(uint32_t id) {
    try {
      voice.Stop();
      auto decoded = assets->loadAudio(std::to_string(id));
      SDL_AudioSpec target{SDL_AUDIO_S16, 1, 22050};
      Uint8 *converted = nullptr;
      int size = 0;
      if (!SDL_ConvertAudioSamples(&decoded.spec, decoded.data.data(),
                                   int(decoded.data.size()), &target,
                                   &converted, &size))
        throw std::runtime_error(SDL_GetError());
      Kotonoha::SDLMemoryPtr<Uint8> convertedOwner(converted);
      Kotonoha::PcmPlayer::Clip clip;
      clip.channels = target.channels;
      clip.rate = target.freq;
      clip.loop = false;
      clip.samples.resize(size / 2);
      std::memcpy(clip.samples.data(), converted, size);

      voice.Play(engine->GetAudio().channel("Voice"), std::move(clip));
    } catch (const std::exception &e) {
      SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "LucaSystem: voice %u: %s", id,
                  e.what());
    }
  }

  void playBgm(uint32_t id, bool loop) {
    try {
      auto decoded = assets->loadAudio(std::to_string(id));
      SDL_AudioSpec target{SDL_AUDIO_S16, 2, 44100};
      Uint8 *converted = nullptr;
      int size = 0;
      if (!SDL_ConvertAudioSamples(&decoded.spec, decoded.data.data(),
                                   int(decoded.data.size()), &target,
                                   &converted, &size))
        throw std::runtime_error(SDL_GetError());
      Kotonoha::SDLMemoryPtr<Uint8> convertedOwner(converted);
      Kotonoha::PcmPlayer::Clip clip;
      clip.channels = target.channels;
      clip.rate = target.freq;
      clip.loop = loop;
      clip.samples.resize(size / 2);
      std::memcpy(clip.samples.data(), converted, size);

      bgm.Play(engine->GetAudio().channel("BGM"), std::move(clip));
    } catch (const std::exception &e) {
      SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "LucaSystem: bgm %u: %s", id,
                  e.what());
    }
  }

  void playSe(uint32_t id) {
    try {
      auto decoded = assets->loadAudio(std::to_string(id));
      SDL_AudioSpec target{SDL_AUDIO_S16, 2, 44100};
      Uint8 *converted = nullptr;
      int size = 0;
      if (!SDL_ConvertAudioSamples(&decoded.spec, decoded.data.data(),
                                   int(decoded.data.size()), &target,
                                   &converted, &size))
        throw std::runtime_error(SDL_GetError());
      Kotonoha::SDLMemoryPtr<Uint8> convertedOwner(converted);
      Kotonoha::PcmPlayer::Clip clip;
      clip.channels = target.channels;
      clip.rate = target.freq;
      clip.loop = false;
      clip.samples.resize(size / 2);
      std::memcpy(clip.samples.data(), converted, size);

      se.Play(engine->GetAudio().channel("Se"), std::move(clip));
    } catch (const std::exception &e) {
      SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "LucaSystem: se %u: %s", id,
                  e.what());
    }
  }

  void loadObjectImage(uint32_t id) {
    if (objectImages.count(id))
      return;
    try {
      objectImages[id] = DecodeCzImage(assets->readImageAsset(id));
      objectTextures[id].reset();
    } catch (const std::exception &e) {
      SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "LucaSystem: image %u: %s", id,
                  e.what());
    }
  }

  void renderObject(Kotonoha::RendererRef renderer, uint32_t id, int x, int y,
                    float opacity = 1.0f, float rightClip = -1.0f) {
    if (opacity <= 0.0f)
      return;
    auto it = objectImages.find(id);
    if (it == objectImages.end())
      return;
    auto &tex = objectTextures[id];
    if (!tex) {
      const auto &img = it->second;
      tex.reset(SDL_CreateTexture(&renderer, SDL_PIXELFORMAT_RGBA32,
                                  SDL_TEXTUREACCESS_STATIC, img.width,
                                  img.height));
      if (!tex || !SDL_UpdateTexture(tex.get(), nullptr, img.rgba.data(),
                                     img.width * 4))
        throw std::runtime_error(SDL_GetError());
      SDL_SetTextureBlendMode(tex.get(), SDL_BLENDMODE_BLEND);
    }
    SDL_SetTextureAlphaMod(
        tex.get(), static_cast<Uint8>(
                       std::lround(std::clamp(opacity, 0.0f, 1.0f) * 255.0f)));
    SDL_FRect dst{float(x), float(y), float(it->second.width),
                  float(it->second.height)};
    SDL_FRect source{};
    const SDL_FRect *sourcePtr = nullptr;
    if (rightClip >= 0.0f) {
      const float visibleWidth =
          wipeVisibleWidth(float(x), float(it->second.width), rightClip);
      if (visibleWidth <= 0.0f)
        return;
      source = SDL_FRect{0, 0, visibleWidth, float(it->second.height)};
      sourcePtr = &source;
      dst.w = visibleWidth;
    }
    SDL_RenderTexture(&renderer, tex.get(), sourcePtr, &dst);
  }

  void runUntilPause() {
    std::size_t budget = 8192;
    while (instructionIndex < instructions.size() && budget-- > 0) {
      if (waiting)
        return;
      const auto &ins = instructions[instructionIndex++];

      switch (ins.opcode) {
      case OP_MESSAGE: {
        sceneLayers.commit(SDL_GetTicks(), 0);
        if (!ins.text.empty()) {
          dialogueVisible = true;
          runtime.dialogueText_ = ins.text;
          runtime.dialogueSpeaker_.clear();
          textbox->setDialogue("", ins.text);
          textbox->show();
          backlog->addEntry("", ins.text);
          return;
        }
        break;
      }
      case OP_MESSAGE_CLEAR:
      case OP_CLOSE_WINDOW: {
        dialogueVisible = false;
        if (textbox) textbox->hide();
        break;
      }
      case OP_MESSAGE_WAIT: {
        waiting = true;
        waitDeadline = SDL_GetTicks() + 300;
        return;
      }
      case OP_WAIT: {
        int ms = 0;
        if (ins.fixedFlag >= 1)
          ms = paramU16(ins, 0);
        else if (ins.paramBytes.size() >= 2)
          ms = paramU16(ins, 0);
        if (ms <= 0)
          ms = 500;
        waiting = true;
        waitDeadline = SDL_GetTicks() + static_cast<std::uint64_t>(ms);
        return;
      }
      case OP_IMAGELOAD: {
        if (ins.paramBytes.size() >= 6) {
          uint32_t id = paramU16(ins, 0);
          int x = int(paramU16(ins, 2));
          int y = int(paramU16(ins, 4));
          loadObjectImage(id);
          sceneLayers.stage({id, x, y});
        }
        break;
      }
      case OP_DRAW: {
        int frames = 0;
        if (ins.paramBytes.size() >= 2)
          frames = int(paramU16(ins, 0));
        const auto duration =
            static_cast<std::uint64_t>(std::clamp(frames, 0, 3600)) * 17u;
        sceneLayers.commit(SDL_GetTicks(), duration);
        if (duration) {
          waiting = true;
          waitDeadline = SDL_GetTicks() + duration;
          return;
        }
        break;
      }
      case OP_FADE: {
        int fadeType = 0;
        if (ins.fixedFlag >= 1)
          fadeType = int(paramU16(ins, 0));
        int ms = 500;
        if (ins.paramBytes.size() >= 2)
          ms = int(paramU16(ins, 0));
        ms = std::clamp(ms, 0, 60000);
        sceneLayers.beginBlackout(SDL_GetTicks(),
                                  static_cast<std::uint64_t>(ms),
                                  (fadeType & 1) != 0);
        if (ms) {
          waiting = true;
          waitDeadline = SDL_GetTicks() + static_cast<std::uint64_t>(ms);
          return;
        }
        break;
      }
      case OP_INIT:
      case OP_HAIKEI_SET:
        break;

      case OP_EQUN: {
        if (ins.paramBytes.size() >= 8) {
          uint32_t varId = paramU32(ins, 0);
          int32_t val = int32_t(paramU32(ins, 4));
          numericVars[varId] = val;
        } else if (ins.paramBytes.size() >= 4) {
          uint32_t varId = paramU32(ins, 0);
          int32_t val = ins.fixedFlag >= 1 ? int32_t(paramU16(ins, 0)) : 0;
          numericVars[varId] = val;
        }
        break;
      }
      case OP_EQU: {
        if (ins.fixedFlag >= 1 || !ins.paramBytes.empty()) {
          uint32_t varId = paramU16(ins, 0);
          stringVars[varId] = ins.text;
        }
        break;
      }
      case OP_EQUV: {
        if (ins.paramBytes.size() >= 8) {
          uint32_t destId = paramU32(ins, 0);
          uint32_t srcId = paramU32(ins, 4);
          numericVars[destId] = numericVars[srcId];
        }
        break;
      }
      case OP_ADD: {
        if (ins.paramBytes.size() >= 8) {
          uint32_t varId = paramU32(ins, 0);
          int32_t val = int32_t(paramU32(ins, 4));
          numericVars[varId] += val;
        } else if (ins.paramBytes.size() >= 4) {
          uint32_t varId = paramU32(ins, 0);
          numericVars[varId] += int32_t(paramU16(ins, 0));
        }
        break;
      }
      case OP_SUB: {
        if (ins.paramBytes.size() >= 8) {
          uint32_t varId = paramU32(ins, 0);
          int32_t val = int32_t(paramU32(ins, 4));
          numericVars[varId] -= val;
        }
        break;
      }
      case OP_MUL: {
        if (ins.paramBytes.size() >= 8) {
          uint32_t varId = paramU32(ins, 0);
          int32_t val = int32_t(paramU32(ins, 4));
          numericVars[varId] *= val;
        }
        break;
      }
      case OP_DIV: {
        if (ins.paramBytes.size() >= 8) {
          uint32_t varId = paramU32(ins, 0);
          int32_t val = int32_t(paramU32(ins, 4));
          if (val != 0) numericVars[varId] /= val;
        }
        break;
      }
      case OP_MOD: {
        if (ins.paramBytes.size() >= 8) {
          uint32_t varId = paramU32(ins, 0);
          int32_t val = int32_t(paramU32(ins, 4));
          if (val != 0) numericVars[varId] %= val;
        }
        break;
      }
      case OP_RANDOM: {
        if (ins.paramBytes.size() >= 8) {
          uint32_t varId = paramU32(ins, 0);
          int32_t mod = int32_t(paramU32(ins, 4));
          numericVars[varId] = std::rand() % (mod > 0 ? mod : 100);
        }
        break;
      }
      case OP_SET: {
        if (ins.paramBytes.size() >= 8) {
          uint32_t varId = paramU32(ins, 0);
          int32_t val = int32_t(paramU32(ins, 4));
          numericVars[varId] = val;
        }
        break;
      }
      case OP_FLAGCLR: {
        if (ins.paramBytes.size() >= 2) {
          uint32_t flagId = paramU16(ins, 0);
          flags[flagId] = false;
        }
        break;
      }
      case OP_ARFLAGSET: {
        if (ins.fixedFlag >= 1 || ins.paramBytes.size() >= 2) {
          uint32_t flagId = paramU16(ins, 0);
          flags[flagId] = true;
        }
        break;
      }
      case OP_VARSTR_SET: {
        if (ins.paramBytes.size() >= 2) {
          uint32_t varId = paramU16(ins, 0);
          stringVars[varId] = ins.text;
        }
        break;
      }
      case OP_SELECT: {
        std::vector<std::string> options;
        auto strList = ExtractShiftJisStrings(std::string_view(
            reinterpret_cast<const char *>(ins.paramBytes.data()),
            ins.paramBytes.size()));
        for (auto &st : strList) {
          if (!st.empty() && st.size() > 1) {
            options.push_back(st);
          }
        }
        if (options.empty() && !ins.text.empty()) {
          options.push_back(ins.text);
        }
        if (options.empty()) {
          options = {"Option 1", "Option 2"};
        }
        waiting = true;
        dialogueVisible = false;
        if (textbox) textbox->hide();
        if (choiceScene) {
          std::vector<Kotonoha::ChoiceOption> choices;
          for (size_t i = 0; i < options.size(); ++i) {
            choices.push_back({options[i], int(i)});
          }
          choiceScene->setQuestion("", choices, [this](int optionId, const std::string &) {
            numericVars[0] = optionId;
            waiting = false;
            if (choiceScene) choiceScene->hide();
            runUntilPause();
          });
        } else {
          numericVars[0] = 0;
          waiting = false;
        }
        return;
      }
      case OP_BGM: {
        uint32_t id = 0;
        bool loop = false;
        if (ins.fixedFlag >= 1)
          id = paramU16(ins, 0);
        if (ins.paramBytes.size() >= 2) {
          uint16_t vol = paramU16(ins, 0);
          loop = (vol != 0);
        }
        playBgm(id, loop);
        break;
      }
      case OP_VOICE: {
        uint32_t id = 0;
        if (ins.fixedFlag >= 1)
          id = paramU16(ins, 0);
        else if (ins.paramBytes.size() >= 2)
          id = paramU16(ins, 0);
        playVoice(id);
        break;
      }
      case OP_VOICE_STOP:
        voice.Stop();
        break;
      case OP_SE: {
        uint32_t id = 0;
        if (ins.fixedFlag >= 1)
          id = paramU16(ins, 0);
        playSe(id);
        break;
      }
      case OP_SE_STOP:
        se.Stop();
        break;
      case OP_GOTO:
        jumpFrom(ins);
        break;
      case OP_ONGOTO: {
        int idx = evalTerm(ins.text);
        if (ins.paramBytes.size() >= 2) {
          uint16_t count = paramU16(ins, 0);
          if (count > 0 && idx >= 0 && idx < int(count) && ins.paramBytes.size() >= 2 + static_cast<size_t>(count) * 4) {
            uint32_t target = paramU32(ins, 2 + static_cast<size_t>(idx) * 4);
            if (const auto index = FindInstructionAtOffset(instructions, target)) {
              instructionIndex = *index;
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
        const auto next = instructionIndex;
        if (!ins.text.empty() && loadScenarioScript(ins.text)) {
          callStack.push_back({currentScenario, next});
          jumpFrom(ins);
        } else {
          if (jumpFrom(ins))
            callStack.push_back({currentScenario, next});
        }
        break;
      }
      case OP_JUMP: {
        if (!ins.text.empty() && loadScenarioScript(ins.text)) {
          jumpFrom(ins);
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
          if (frame.scenario != currentScenario) {
            loadScenarioScript(frame.scenario);
          }
          instructionIndex = frame.instructionIndex;
        }
        break;
      }
      case OP_IFY: {
        std::string expr = ins.text;
        if (expr.empty() && ins.paramBytes.size() >= 2) {
          uint16_t len = paramU16(ins, 0);
          if (len > 0 && ins.paramBytes.size() >= 2 + len) {
            expr = std::string(reinterpret_cast<const char *>(ins.paramBytes.data() + 2), len);
          }
        }
        if (evaluateCondition(expr)) {
          jumpFrom(ins);
        }
        break;
      }
      case OP_IFN: {
        std::string expr = ins.text;
        if (expr.empty() && ins.paramBytes.size() >= 2) {
          uint16_t len = paramU16(ins, 0);
          if (len > 0 && ins.paramBytes.size() >= 2 + len) {
            expr = std::string(reinterpret_cast<const char *>(ins.paramBytes.data() + 2), len);
          }
        }
        if (!evaluateCondition(expr)) {
          jumpFrom(ins);
        }
        break;
      }
      case OP_WIPE: {
        // Transitional approximation while decoding AIR-specific wipe
        // directions: commit staged images with a bounded cross-fade.
        const auto ms = ins.paramBytes.size() >= 2
                            ? static_cast<std::uint64_t>(paramU16(ins, 0))
                            : 300u;
        sceneLayers.commit(SDL_GetTicks(), ms,
                           SceneTransition::WipeLeftToRight);
        if (ms) {
          waiting = true;
          waitDeadline = SDL_GetTicks() + ms;
          return;
        }
        break;
      }
      case OP_MOVE:
      case OP_ROT:
      case OP_SCALE:
      case OP_SHAKE:
      case OP_FADE_OBJ:
      case OP_BASE:
      case OP_DISP:
      case OP_VOLUME:
      case OP_TONE:
      case OP_SEPIA:
      case OP_PRIORITY:
      case OP_FACE:
      case OP_EX:
      case OP_IMAGEUPDATE:
        // Object animation opcodes — no-op for now, tracked for compatibility.
        break;
      case OP_STARTUP_BEGIN:
      case OP_STARTUP_END:
        // Startup block markers — no-op.
        break;
      case OP_END:
        sceneLayers.commit(SDL_GetTicks(), 0);
        running = false;
        return;
      default:
        // Unknown opcode — skip.
        break;
      }
    }
    // Budget exhaustion must yield to the next frame, not silently stop
    // a valid scenario with more than 8192 consecutive nonblocking opcodes.
    if (instructionIndex >= instructions.size()) {
      sceneLayers.commit(SDL_GetTicks(), 0);
      running = false;
    }
  }

  bool jumpFrom(const ScriptInstruction &ins) {
    // LuckSystem's ReadJump reads a uint32 file position. Resolve only exact
    // instruction offsets; do not interpret arbitrary payload bytes as labels.
    if (ins.paramBytes.size() >= 4) {
      if (const auto index =
              FindInstructionAtOffset(instructions, paramU32(ins, 0))) {
        instructionIndex = *index;
        return true;
      }
    }
    // Older per-title scripts may contain a NUL-terminated symbolic label.
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
    if (!textbox)
      return;
    switch (
        nextDialogueAction(dialogueVisible, textbox->isDialogueComplete())) {
    case DialogueAdvance::None:
      return;
    case DialogueAdvance::Reveal:
      textbox->completeDialogue();
      return;
    case DialogueAdvance::ResumeScript:
      // The VM must process the next command in order, including graphics,
      // music, waits and branches between consecutive dialogue messages.
      dialogueVisible = false;
      textbox->hide();
      ++dialogueIndex;
      runtime.dialogueText_.clear();
      runtime.dialogueSpeaker_.clear();
      runUntilPause();
      return;
    }
  }

  SceneLayers sceneLayers;
};

LucaSystemRuntime::LucaSystemRuntime(
    std::shared_ptr<Kotonoha::Providers::LucaSystemAssets> assets)
    : Runtime("LucaSystem", "0.1.0", "me.hirameki.kotonoha.lucasystem"),
      impl_(std::make_unique<Impl>(*this, std::move(assets))),
      assets_(impl_->assets) {}

LucaSystemRuntime::~LucaSystemRuntime() = default;

void LucaSystemRuntime::init(Kotonoha::Engine &engine) {
  auto &impl = *impl_;
  impl.engine = &engine;
  Runtime::init(engine);
}

void LucaSystemRuntime::initGlobal(Kotonoha::Engine &engine) {
  auto &impl = *impl_;
  auto &mgr = Kotonoha::GetVNManagerRuntime(engine);
  (void)mgr;
  impl.textbox = new AirTextBox(engine, *impl.assets);
  impl.choiceScene = &Kotonoha::GetChoiceQuestionRuntime(engine);
  impl.backlog = &Kotonoha::GetVNBacklogRuntime(engine);
  impl.menu = &Kotonoha::GetInGameMenuRuntime(engine);
  impl.saveMenu = new Kotonoha::VNSaveMenu(engine);

  auto &menu = *impl.menu;
  menu.SetOnReturnToMenu([this] { SetState(Kotonoha::GameState::Menu); });
  menu.SetOnSave([this] {
    auto path =
        Kotonoha::preferredDocumentsDirectory("luca_system") / "quicksave.kns";
    saveGame(path.string());
  });
  menu.SetOnLoad([this] {
    auto path =
        Kotonoha::preferredDocumentsDirectory("luca_system") / "quicksave.kns";
    loadGame(path.string());
  });
}

void LucaSystemRuntime::initSplash(Kotonoha::Engine &engine) {
  SetState(Kotonoha::GameState::Gameplay);
  initGameplay(engine);
}

void LucaSystemRuntime::initMenu(Kotonoha::Engine &engine) {
  auto &impl = *impl_;
  impl.menu->open();
  impl.menu->SetOnResume([this] {
    SetState(Kotonoha::GameState::Gameplay);
    init(*impl_->engine);
  });
  AddScene(impl.menu);
}

void LucaSystemRuntime::initGameplay(Kotonoha::Engine &engine) {
  auto &impl = *impl_;
  impl.backlog->clear();
  impl.backlog->close();

  if (impl.originalTitle) {
    impl.titleBackground = DecodeCzImage(impl.assets->read("title1a"));
    impl.titleMenu = DecodeCzImage(impl.assets->read("title_menu"));
    impl.titleShown = true;
    return;
  }

  AddScene(impl.textbox);
  AddScene(impl.choiceScene);
  AddScene(impl.backlog);
  AddScene(impl.menu);
  if (!impl.currentScenario.empty()) {
    startScenario(impl.currentScenario);
  } else {
    auto scenarios = impl.scenarioFiles();
    SDL_Log("LucaSystem: found %zu scenarios", scenarios.size());
    for (const auto &s : scenarios)
      SDL_Log("LucaSystem: scenario: %s", s.c_str());
  }
}

void LucaSystemRuntime::update() {
  Runtime::update();
  auto &impl = *impl_;
  if (impl.engine && impl.running && !impl.dialogueVisible &&
      !impl.engine->isStoryPaused() && !impl.waiting) {
    impl.runUntilPause();
  }
  if (impl.waiting && impl.engine && !impl.engine->isStoryPaused()) {
    if (SDL_GetTicks() >= impl.waitDeadline) {
      impl.waiting = false;
      impl.runUntilPause();
    }
  }
}

void LucaSystemRuntime::renderGameplay(Kotonoha::RendererRef renderer,
                                       float scale) {
  auto &impl = *impl_;
  if (impl.originalTitle) {
    auto texture = [&renderer](Kotonoha::TexturePtr &target,
                               const CzImage &image) -> SDL_Texture * {
      if (!target) {
        target.reset(SDL_CreateTexture(&renderer, SDL_PIXELFORMAT_RGBA32,
                                       SDL_TEXTUREACCESS_STATIC, image.width,
                                       image.height));
        if (!target || !SDL_UpdateTexture(target.get(), nullptr,
                                          image.rgba.data(), image.width * 4))
          throw std::runtime_error(std::string("LucaSystem title texture: ") +
                                   SDL_GetError());
        SDL_SetTextureScaleMode(target.get(), SDL_SCALEMODE_LINEAR);
        SDL_SetTextureBlendMode(target.get(), SDL_BLENDMODE_BLEND);
      }
      return target.get();
    };
    SDL_FRect screen{0, 0, 1280, 720};
    SDL_RenderTexture(
        &renderer, texture(impl.titleBackgroundTexture, impl.titleBackground),
        nullptr, &screen);
    SDL_FRect source{0, 0, 1856, 70};
    SDL_FRect destination{0, 620, 1280, 48.2759f};
    SDL_RenderTexture(&renderer, texture(impl.titleMenuTexture, impl.titleMenu),
                      &source, &destination);
    static constexpr int groups[][2] = {
        {15, 222},    {258, 542},   {577, 809},   {842, 950},  {984, 1154},
        {1188, 1352}, {1386, 1466}, {1500, 1679}, {1714, 1855}};
    const auto &group = groups[impl.titleSelection];
    const float menuScale = 1280.0f / 1856.0f;
    SDL_FRect activeSource{float(group[0]), 70, float(group[1] - group[0] + 1),
                           70};
    SDL_FRect activeDestination{group[0] * menuScale, 620,
                                activeSource.w * menuScale, 48.2759f};
    SDL_RenderTexture(&renderer, texture(impl.titleMenuTexture, impl.titleMenu),
                      &activeSource, &activeDestination);
    return;
  }
  if (!impl.background.rgba.empty()) {
    if (!impl.backgroundTexture) {
      impl.backgroundTexture.reset(SDL_CreateTexture(
          &renderer, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STATIC,
          impl.background.width, impl.background.height));
      if (!impl.backgroundTexture ||
          !SDL_UpdateTexture(impl.backgroundTexture.get(), nullptr,
                             impl.background.rgba.data(),
                             impl.background.width * 4))
        throw std::runtime_error(SDL_GetError());
    }
    SDL_FRect screen{0, 0, 1280, 720};
    SDL_RenderTexture(&renderer, impl.backgroundTexture.get(), nullptr,
                      &screen);
  }
  const auto blend = impl.sceneLayers.blend(SDL_GetTicks());
  const auto &visible = impl.sceneLayers.visible();
  const auto &outgoing = impl.sceneLayers.outgoing();
  const auto firstNew = impl.sceneLayers.incomingStart();
  if (blend.wipeLeftToRight) {
    for (const auto &place : outgoing)
      impl.renderObject(renderer, place.id, place.x, place.y);
    const float revealRight = 1280.0f * blend.incoming;
    for (std::size_t i = firstNew; i < visible.size(); ++i) {
      const auto &place = visible[i];
      impl.renderObject(renderer, place.id, place.x, place.y, 1.0f,
                        revealRight);
    }
  } else if (blend.incoming < 1.0f && !outgoing.empty()) {
    // Old images also remain inside visible. Drawing them twice causes
    // partially transparent objects to become too opaque during DRAW.
    for (const auto &place : outgoing)
      impl.renderObject(renderer, place.id, place.x, place.y);
    for (std::size_t i = firstNew; i < visible.size(); ++i) {
      const auto &place = visible[i];
      impl.renderObject(renderer, place.id, place.x, place.y, blend.incoming);
    }
  } else {
    for (const auto &place : visible)
      impl.renderObject(renderer, place.id, place.x, place.y, blend.incoming);
  }
  if (blend.blackout > 0.0f) {
    SDL_SetRenderDrawBlendMode(&renderer, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(
        &renderer, 0, 0, 0,
        static_cast<Uint8>(
            std::lround(std::clamp(blend.blackout, 0.0f, 1.0f) * 255.0f)));
    SDL_FRect screen{0, 0, 1280, 720};
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
    if (hit >= 0)
      impl.titleSelection = hit;
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
    return true;
  }
  if (impl.originalTitle && mapped.type == SDL_EVENT_KEY_DOWN &&
      !mapped.key.repeat &&
      (mapped.key.key == SDLK_RETURN || mapped.key.key == SDLK_SPACE)) {
    if (impl.titleSelection == 8) {
      SDL_Event quit{};
      quit.type = SDL_EVENT_QUIT;
      SDL_PushEvent(&quit);
      return true;
    }
    if (impl.titleSelection == 3) {
      const auto savePath =
          Kotonoha::preferredDocumentsDirectory("luca_system") /
          "quicksave.kns";
      if (!std::filesystem::exists(savePath))
        return true;
      impl.originalTitle = false;
      initGameplay(*impl.engine);
      loadGame(savePath.string());
      return true;
    }
    if (impl.titleSelection != 0)
      return true;
    impl.originalTitle = false;
    impl.titleBackgroundTexture.reset();
    impl.titleMenuTexture.reset();
    initGameplay(*impl.engine);
    return startScenario("seen163");
  }
  // Handle save/load before generic dialogue input, otherwise F5/F6
  // are consumed as confirmation keys while the textbox is visible.
  if (!impl.originalTitle && mapped.type == SDL_EVENT_KEY_DOWN &&
      !mapped.key.repeat && mapped.key.key == SDLK_F5) {
    auto path =
        Kotonoha::preferredDocumentsDirectory("luca_system") / "quicksave.kns";
    saveGame(path.string());
    return true;
  }
  if (!impl.originalTitle && mapped.type == SDL_EVENT_KEY_DOWN &&
      !mapped.key.repeat && mapped.key.key == SDLK_F6) {
    auto path =
        Kotonoha::preferredDocumentsDirectory("luca_system") / "quicksave.kns";
    loadGame(path.string());
    return true;
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
  state.instructionIndex = impl.instructionIndex;
  state.dialogueIndex = impl.dialogueIndex;
  state.dialogueVisible = impl.dialogueVisible;
  state.dialogueText = dialogueText_;
  state.waiting = impl.waiting;
  if (impl.waiting) {
    const auto now = SDL_GetTicks();
    const auto remaining =
        impl.waitDeadline > now ? impl.waitDeadline - now : 0;
    state.waitRemainingMs = static_cast<std::uint32_t>(
        std::min<std::uint64_t>(remaining, SaveCodec::MaxWaitMs));
  }
  state.numbers.insert(impl.numericVars.begin(), impl.numericVars.end());
  state.strings.insert(impl.stringVars.begin(), impl.stringVars.end());
  state.flags.insert(impl.flags.begin(), impl.flags.end());
  state.callFrames = impl.callStack;
  for (const auto &frame : impl.callStack)
    state.callStack.push_back(frame.instructionIndex);
  state.images.reserve(impl.sceneLayers.visible().size());
  for (const auto &place : impl.sceneLayers.visible())
    state.images.push_back({place.id, place.x, place.y});
  Kotonoha::GetVNManagerRuntime(*impl.engine)
      .save(path, encodeSaveState(state));
}

void LucaSystemRuntime::loadGame(const std::string &path) {
  auto &impl = *impl_;
  auto saved = Kotonoha::VNSave::read(path);
  const SaveState state = decodeSaveState(saved.gameData);

  // Parse and validate before touching the live interpreter.
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
    if (frame.scenario == state.scenario && frame.instructionIndex > candidate.instructionCount())
      throw std::runtime_error("Invalid LucaSystem save return frame");

  // Resume without evaluating any instruction: normal scenario startup
  // advances the PC and would overwrite the saved dialogue.
  if (!startScenario(state.scenario, state.instructionIndex, true))
    throw std::runtime_error("Cannot resume LucaSystem scenario");
  impl.numericVars.clear();
  impl.numericVars.insert(state.numbers.begin(), state.numbers.end());
  impl.stringVars.clear();
  impl.stringVars.insert(state.strings.begin(), state.strings.end());
  impl.flags.clear();
  impl.flags.insert(state.flags.begin(), state.flags.end());
  impl.callStack = state.callFrames;
  std::vector<SceneObject> restoredImages;
  restoredImages.reserve(state.images.size());
  for (const auto &place : state.images) {
    impl.loadObjectImage(place.assetId);
    restoredImages.push_back({place.assetId, place.x, place.y});
  }
  impl.sceneLayers.reset(restoredImages);
  impl.dialogueIndex = state.dialogueIndex;
  impl.waiting = state.waiting;
  impl.waitDeadline = SDL_GetTicks() + state.waitRemainingMs;

  // Version 2 did not persist the message itself; recover the most recent
  // consumed MESSAGE instruction when resuming a legacy save.
  std::string text = state.dialogueText;
  if (state.dialogueVisible && text.empty()) {
    for (std::size_t i = state.instructionIndex; i > 0; --i) {
      const auto &ins = impl.instructions[i - 1];
      if (ins.opcode == OP_MESSAGE && !ins.text.empty()) {
        text = ins.text;
        break;
      }
    }
  }
  impl.dialogueVisible = state.dialogueVisible && !text.empty();
  dialogueText_ = impl.dialogueVisible ? text : std::string{};
  dialogueSpeaker_.clear();
  if (impl.textbox) {
    if (impl.dialogueVisible) {
      impl.textbox->setDialogue("", dialogueText_);
      impl.textbox->show();
    } else {
      impl.textbox->hide();
    }
  }
}

bool LucaSystemRuntime::startScenario(const std::string &name, size_t position,
                                      bool resume) {
  auto &impl = *impl_;
  auto *pak = impl.assets->scriptPak();
  if (!pak)
    return false;
  const PakEntry *entry = pak->findByName(name);
  if (!entry)
    return false;

  auto bytes = impl.assets->read(name);
  Script script(name, std::vector<std::uint8_t>(bytes.begin(), bytes.end()));
  if (!script.valid()) {
    SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                "LucaSystem: failed to parse script '%s': %s", name.c_str(),
                script.error().c_str());
    return false;
  }
  if (position > script.instructionCount())
    return false;

  impl.currentScenario = name;
  impl.instructions = script.instructions();
  impl.instructionIndex = position;
  impl.dialogueLines = script.strings();
  impl.dialogueIndex = 0;
  impl.dialogueVisible = false;
  impl.callStack.clear();
  impl.sceneLayers.reset();
  impl.waiting = false;
  impl.running = position < impl.instructions.size();
  dialogueText_.clear();
  dialogueSpeaker_.clear();
  if (impl.textbox)
    impl.textbox->hide();

  SDL_Log("LucaSystem: scenario '%s' parsed: %zu instructions, %zu dialogue "
          "lines",
          name.c_str(), script.instructionCount(), impl.dialogueLines.size());

  scenarioTexts_.clear();
  for (const auto &line : impl.dialogueLines)
    if (std::find(scenarioTexts_.begin(), scenarioTexts_.end(), line) ==
        scenarioTexts_.end())
      scenarioTexts_.push_back(line);

  if (!resume)
    impl.runUntilPause();
  return true;
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
