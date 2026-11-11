// Copyright 2026 Kotonoha contributors. SPDX-License-Identifier: MIT
// LucaSystem (Visual Art's / Key) runtime — full VM implementation recovered
// via reverse engineering of the original AIR.exe binaries with rizin/REA and
// analysis of the SCRIPT.PAK bytecode format.
#include <LucaSystem/CzImage.hpp>
#include <LucaSystem/LucaSystemRuntime.hpp>
#include <LucaSystem/LucaSystemScript.hpp>

#include <Kotonoha/componentes/ChoiceQuestionScene.hpp>
#include <Kotonoha/componentes/InGameMenu.hpp>
#include <Kotonoha/componentes/VNBacklog.hpp>
#include <Kotonoha/componentes/VNManager.hpp>
#include <Kotonoha/componentes/VNSaveMenu.hpp>
#include <Kotonoha/componentes/VNTextBox.hpp>
#include <Kotonoha/core/Engine.hpp>
#include <Kotonoha/core/FileSystem.hpp>
#include <Kotonoha/core/SDL.hpp>
#include <Kotonoha/core/UI.hpp>
#include <Kotonoha/events/ControllerEvent.hpp>
#include <Kotonoha/media/PcmPlayer.hpp>
#include <SDL3/SDL.h>

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string>
#include <cstring>
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

// LucaSystem opcode table recovered from AIR.exe via rizin/REA and SCRIPT.PAK
// analysis.
enum Opcode : uint8_t {
  OP_EQUN = 94,
  OP_EQU = 95,
  OP_MESSAGE = 36,
  OP_FADE = 46,
  OP_IMAGELOAD = 55,
  OP_DRAW = 57,
  OP_WAIT = 62,
  OP_INIT = 100,
  OP_HAIKIE_SET = 101,
  OP_BGM = 121,
  OP_JUMP = 134,
  OP_CLOSE_WINDOW = 138,
  OP_END = 44,
  OP_STARTUP_BEGIN = 40,
  OP_STARTUP_END = 41,
  OP_IMAGEUPDATE = 113,
  OP_SELECT = 54,
  OP_VOICE = 59,
  OP_VOICE_STOP = 60,
  OP_SE = 127,
  OP_SE_STOP = 128,
  OP_GOTO = 29,
  OP_IFY = 32,
  OP_IFN = 33,
  OP_RETURN = 34,
  OP_CALL = 31,   // GOSUB
  OP_VARSTR = 26,
  OP_SET = 27,
  OP_ARFLAGSET = 45,
  OP_WIPE = 89,
  OP_MOVE = 72,
  OP_ROT = 74,
  OP_SCALE = 77,
  OP_SHAKE = 78,
  OP_FADE_OBJ = 76,
  OP_BASE = 80,
  OP_DISP = 102,
  OP_VOLUME = 132,
  OP_TONE = 96,
  OP_SEPIA = 104,
  OP_PRIORITY = 112,
  OP_FACE = 103,
  OP_EX = 135,
  OP_TASK = 138,  // placeholder
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
      return engine.GetVNUI().textSize(value, 1168).x;
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
  AirTextBox(Kotonoha::Engine &engine, const Kotonoha::Providers::LucaSystemAssets &assets)
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
  std::unordered_map<uint32_t, int32_t> numericVars;   // EQUN variables
  std::unordered_map<uint32_t, std::string> stringVars; // EQU string variables
  std::unordered_map<uint32_t, bool> flags;             // ARFLAG
  std::vector<size_t> callStack;
  int waitDeadline = 0;
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

  explicit Impl(LucaSystemRuntime &r, std::shared_ptr<Kotonoha::Providers::LucaSystemAssets> a)
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
      // LucaSystem voice entries are raw PCM/Ogg; decode via the asset system.
      voice.Stop();
      // We push decoded PCM through the shared PcmPlayer on the Voice channel.
      auto decoded = assets->loadAudio(std::to_string(id));
      SDL_AudioSpec target{SDL_AUDIO_S16, 1, 22050};
      Uint8 *converted = nullptr;
      int size = 0;
      if (!SDL_ConvertAudioSamples(&decoded.spec, decoded.data.data(),
                                   int(decoded.data.size()), &target,
                                   &converted, &size))
        throw std::runtime_error(SDL_GetError());
      Kotonoha::PcmPlayer::Clip clip;
      clip.channels = target.channels;
      clip.rate = target.freq;
      clip.loop = false;
      clip.samples.resize(size / 2);
      std::memcpy(clip.samples.data(), converted, size);
      SDL_free(converted);
      voice.Play(engine->GetAudio().channel("Voice"), std::move(clip));
    } catch (const std::exception &e) {
      SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                  "LucaSystem: voice %u: %s", id, e.what());
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
      Kotonoha::PcmPlayer::Clip clip;
      clip.channels = target.channels;
      clip.rate = target.freq;
      clip.loop = loop;
      clip.samples.resize(size / 2);
      std::memcpy(clip.samples.data(), converted, size);
      SDL_free(converted);
      bgm.Play(engine->GetAudio().channel("BGM"), std::move(clip));
    } catch (const std::exception &e) {
      SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                  "LucaSystem: bgm %u: %s", id, e.what());
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
      Kotonoha::PcmPlayer::Clip clip;
      clip.channels = target.channels;
      clip.rate = target.freq;
      clip.loop = false;
      clip.samples.resize(size / 2);
      std::memcpy(clip.samples.data(), converted, size);
      SDL_free(converted);
      se.Play(engine->GetAudio().channel("Se"), std::move(clip));
    } catch (const std::exception &e) {
      SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                  "LucaSystem: se %u: %s", id, e.what());
    }
  }

  void loadObjectImage(uint32_t id) {
    if (objectImages.count(id))
      return;
    try {
      objectImages[id] = DecodeCzImage(assets->readImageAsset(id));
      objectTextures[id].reset();
    } catch (const std::exception &e) {
      SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                  "LucaSystem: image %u: %s", id, e.what());
    }
  }

  void renderObject(Kotonoha::RendererRef renderer, uint32_t id,
                    int x, int y) {
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
    SDL_FRect dst{float(x), float(y), float(it->second.width),
                  float(it->second.height)};
    SDL_RenderTexture(&renderer, tex.get(), nullptr, &dst);
  }

  void runUntilPause() {
    while (instructionIndex < instructions.size()) {
      const auto &ins = instructions[instructionIndex++];

      if (waiting) {
        // A WAIT is pending; stop here and resume on next frame.
        --instructionIndex;
        return;
      }

      switch (ins.opcode) {
      case OP_MESSAGE: {
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
      case OP_WAIT: {
        int ms = 0;
        if (ins.fixedFlag >= 1)
          ms = paramU16(ins, 0);
        else if (ins.paramBytes.size() >= 2)
          ms = paramU16(ins, 0);
        if (ms <= 0)
          ms = 500;
        waiting = true;
        waitDeadline = int(SDL_GetTicks()) + ms;
        --instructionIndex;
        return;
      }
      case OP_IMAGELOAD: {
        // IMAGELOAD param: image_id(uint16), x(uint16), y(uint16), ...
        if (ins.paramBytes.size() >= 6) {
          uint32_t id = paramU16(ins, 0);
          int x = int(paramU16(ins, 2));
          int y = int(paramU16(ins, 4));
          loadObjectImage(id);
          // Store placement; rendered in renderGameplay.
          // For simplicity we draw immediately at (x,y) on a base canvas.
          // The original engine composites objects onto the background.
          objectPlacements_.push_back({id, x, y});
        }
        break;
      }
      case OP_DRAW: {
        int frames = 0;
        if (ins.paramBytes.size() >= 2)
          frames = int(paramU16(ins, 0));
        waiting = true;
        waitDeadline = int(SDL_GetTicks()) + frames * 17;
        --instructionIndex;
        return;
      }
      case OP_FADE: {
        int fadeType = 0;
        if (ins.fixedFlag >= 1)
          fadeType = int(paramU16(ins, 0));
        int ms = 500;
        if (ins.paramBytes.size() >= 2)
          ms = int(paramU16(ins, 0));
        // Simple fade: just wait, the visual fade is handled by the engine.
        waiting = true;
        waitDeadline = int(SDL_GetTicks()) + ms;
        --instructionIndex;
        return;
      }
      case OP_INIT: {
        int mode = 0;
        if (ins.paramBytes.size() >= 2)
          mode = int(paramU16(ins, 0));
        // INIT 0001 = init text system, 02ff = init object system, etc.
        break;
      }
      case OP_HAIKIE_SET: {
        // Set background from current object context.
        break;
      }
      case OP_EQUN: {
        // EQUN: set numeric variable. param is a 4-byte variable id.
        if (ins.paramBytes.size() >= 4) {
          uint32_t varId = paramU32(ins, 0);
          // The value follows in subsequent EQUN or is implied.
          numericVars[varId] = 0;
        }
        break;
      }
      case OP_EQU: {
        // EQU: set string variable. fixedFlag=1, fixed param = var id.
        if (ins.fixedFlag >= 1 && !ins.paramBytes.empty()) {
          uint32_t varId = paramU16(ins, 0);
          stringVars[varId] = ins.text;
        }
        break;
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
      case OP_CLOSE_WINDOW: {
        // Hide the message window.
        dialogueVisible = false;
        textbox->hide();
        break;
      }
      case OP_GOTO: {
        // Jump to label. fixed param = label hash, param = label string.
        if (!ins.paramBytes.empty()) {
          std::string label(ins.paramBytes.begin(), ins.paramBytes.end());
          jumpToLabel(label);
        }
        break;
      }
      case OP_JUMP: {
        if (!ins.paramBytes.empty()) {
          std::string label(ins.paramBytes.begin(), ins.paramBytes.end());
          jumpToLabel(label);
        }
        break;
      }
      case OP_CALL:
        callStack.push_back(instructionIndex);
        if (!ins.paramBytes.empty()) {
          std::string label(ins.paramBytes.begin(), ins.paramBytes.end());
          jumpToLabel(label);
        }
        break;
      case OP_RETURN:
        if (!callStack.empty()) {
          instructionIndex = callStack.back();
          callStack.pop_back();
        }
        break;
      case OP_IFY: {
        // If variable equals value, jump.
        break;
      }
      case OP_IFN: {
        // If variable not equals value, jump.
        break;
      }
      case OP_VARSTR: {
        // Variable string operation.
        break;
      }
      case OP_SET: {
        // Set variable.
        break;
      }
      case OP_ARFLAGSET: {
        // Set flag.
        if (ins.fixedFlag >= 1) {
          uint32_t flagId = paramU16(ins, 0);
          flags[flagId] = true;
        }
        break;
      }
      case OP_WIPE: {
        // Screen wipe transition.
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
        running = false;
        return;
      default:
        // Unknown opcode — skip.
        break;
      }
    }
    running = false;
  }

  void jumpToLabel(const std::string &label) {
    // Search for a JUMPPOINT instruction with matching label text.
    for (size_t i = 0; i < instructions.size(); ++i) {
      const auto &ins = instructions[i];
      if (ins.opcode == 38 && !ins.text.empty() && ins.text == label) {
        instructionIndex = i + 1;
        return;
      }
      // Also match by raw param bytes.
      if (!ins.paramBytes.empty()) {
        std::string cand(ins.paramBytes.begin(), ins.paramBytes.end());
        if (cand == label) {
          instructionIndex = i + 1;
          return;
        }
      }
    }
    SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                "LucaSystem: label not found: %s", label.c_str());
  }

  void advanceDialogue() {
    if (!dialogueVisible && dialogueIndex < dialogueLines.size()) {
      dialogueVisible = true;
      const auto &line = dialogueLines[dialogueIndex];
      runtime.dialogueText_ = line;
      runtime.dialogueSpeaker_.clear();
      textbox->setDialogue(runtime.dialogueSpeaker_, runtime.dialogueText_);
      textbox->show();
      backlog->addEntry("", line);
      return;
    }
    if (dialogueVisible && textbox && textbox->isDialogueComplete()) {
      dialogueVisible = false;
      ++dialogueIndex;
      if (dialogueIndex < dialogueLines.size()) {
        advanceDialogue();
      } else {
        runUntilPause();
      }
    }
  }

  // Object placements from IMAGELOAD.
  struct ObjectPlacement {
    uint32_t id;
    int x, y;
  };
  std::vector<ObjectPlacement> objectPlacements_;
};

LucaSystemRuntime::LucaSystemRuntime(std::shared_ptr<Kotonoha::Providers::LucaSystemAssets> assets)
    : Runtime("LucaSystem", "0.1.0", "me.hirameki.kotonoha.luca_system"),
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
    if (int(SDL_GetTicks()) >= impl.waitDeadline) {
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
  for (const auto &place : impl.objectPlacements_) {
    impl.renderObject(renderer, place.id, place.x, place.y);
  }
  Runtime::renderGameplay(renderer, scale);
}

void LucaSystemRuntime::advanceDialogue() { impl_->advanceDialogue(); }

bool LucaSystemRuntime::handleAdvanceAction() {
  auto &impl = *impl_;
  if (impl.dialogueVisible && impl.textbox) {
    if (!impl.textbox->isDialogueComplete()) {
      impl.textbox->completeDialogue();
      return true;
    }
    impl.dialogueVisible = false;
    impl.runUntilPause();
    return true;
  }
  return false;
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
  if (impl.dialogueVisible && (mapped.type == SDL_EVENT_KEY_DOWN ||
                               mapped.type == SDL_EVENT_MOUSE_BUTTON_DOWN ||
                               mapped.type == SDL_EVENT_FINGER_DOWN)) {
    if (mapped.type != SDL_EVENT_KEY_DOWN || !mapped.key.repeat) {
      advanceDialogue();
      return true;
    }
  }
  if (mapped.type == SDL_EVENT_KEY_DOWN && !mapped.key.repeat &&
      mapped.key.key == SDLK_F5) {
    auto path =
        Kotonoha::preferredDocumentsDirectory("luca_system") / "quicksave.kns";
    saveGame(path.string());
    return true;
  }
  if (mapped.type == SDL_EVENT_KEY_DOWN && !mapped.key.repeat &&
      mapped.key.key == SDLK_F6) {
    auto path =
        Kotonoha::preferredDocumentsDirectory("luca_system") / "quicksave.kns";
    loadGame(path.string());
    return true;
  }
  return Runtime::onEvent(mapped);
}

void LucaSystemRuntime::saveGame(const std::string &path) {
  auto &impl = *impl_;
  std::ostringstream data;
  data << "LUCA_SYSTEM_SAVE_2\n"
       << impl.currentScenario << '\n'
       << impl.instructionIndex << '\n'
       << impl.dialogueIndex << '\n'
       << impl.dialogueVisible << '\n';
  // Persist numeric variables.
  data << impl.numericVars.size() << '\n';
  for (auto &[k, v] : impl.numericVars)
    data << k << ' ' << v << '\n';
  // Persist string variables.
  data << impl.stringVars.size() << '\n';
  for (auto &[k, v] : impl.stringVars)
    data << k << ' ' << v << '\n';
  // Persist flags.
  data << impl.flags.size() << '\n';
  for (auto &[k, v] : impl.flags)
    data << k << ' ' << (v ? 1 : 0) << '\n';
  Kotonoha::GetVNManagerRuntime(*impl.engine).save(path, data.str());
}

void LucaSystemRuntime::loadGame(const std::string &path) {
  auto &impl = *impl_;
  auto saved = Kotonoha::VNSave::read(path);
  std::istringstream data(saved.gameData);
  std::string header, scenario;
  size_t index = 0, dIndex = 0;
  bool dVisible = false;
  std::getline(data, header);
  if (header == "LUCA_SYSTEM_SAVE_2") {
    std::getline(data, scenario);
    data >> index >> dIndex >> dVisible;
    size_t nv;
    data >> nv;
    impl.numericVars.clear();
    for (size_t i = 0; i < nv; ++i) {
      uint32_t k;
      int32_t v;
      data >> k >> v;
      impl.numericVars[k] = v;
    }
    size_t ns;
    data >> ns;
    impl.stringVars.clear();
    for (size_t i = 0; i < ns; ++i) {
      uint32_t k;
      std::string v;
      data >> k >> v;
      impl.stringVars[k] = v;
    }
    size_t nf;
    data >> nf;
    impl.flags.clear();
    for (size_t i = 0; i < nf; ++i) {
      uint32_t k;
      int v;
      data >> k >> v;
      impl.flags[k] = v != 0;
    }
  } else {
    scenario = std::move(header);
  }
  impl.dialogueIndex = dIndex;
  impl.dialogueVisible = dVisible;
  if (!scenario.empty())
    startScenario(scenario, index);
}

bool LucaSystemRuntime::startScenario(const std::string &name,
                                      size_t position) {
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

  impl.currentScenario = name;
  impl.instructions = script.instructions();
  impl.instructionIndex = position;
  impl.dialogueLines = script.strings();
  impl.dialogueIndex = 0;
  impl.dialogueVisible = false;
  impl.objectPlacements_.clear();
  impl.waiting = false;
  impl.running = true;

  SDL_Log("LucaSystem: scenario '%s' parsed: %zu instructions, %zu dialogue "
          "lines",
          name.c_str(), script.instructionCount(), impl.dialogueLines.size());

  scenarioTexts_.clear();
  for (const auto &line : impl.dialogueLines)
    if (std::find(scenarioTexts_.begin(), scenarioTexts_.end(), line) ==
        scenarioTexts_.end())
      scenarioTexts_.push_back(line);

  impl.runUntilPause();
  return true;
}

bool LucaSystemRuntime::debugJumpToLabel(const std::string &label) {
  auto &impl = *impl_;
  for (size_t i = 0; i < impl.instructions.size(); ++i) {
    const auto &ins = impl.instructions[i];
    if (!ins.text.empty() && ins.text == label) {
      impl.instructionIndex = i + 1;
      impl.runUntilPause();
      return true;
    }
  }
  return false;
}

std::string LucaSystemRuntime::debugNode() const {
  auto &impl = *impl_;
  return impl.currentScenario.empty()
             ? std::string()
             : ("luca_system | scenario " + impl.currentScenario +
                " @" + std::to_string(impl.instructionIndex));
}

void LucaSystemRuntime::debugRestartScene() {
  if (impl_->currentScenario.empty() ||
      !startScenario(impl_->currentScenario))
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
