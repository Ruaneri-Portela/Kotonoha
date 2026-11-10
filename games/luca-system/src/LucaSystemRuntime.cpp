// Copyright 2026 Kotonoha contributors. SPDX-License-Identifier: MIT
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
#include <filesystem>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string>

namespace LucaSystem {

namespace {

bool isSystemScript(std::string_view name) {
  return name.rfind("_varset_", 0) == 0 || name == "_build_time" ||
         name == "_arflag" || name == "_varstr" || name == "_varnum" ||
         name == "_varname" || name == "_shakelist" || name == "_scr_label" ||
         name == "_task";
}

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
    // inside the 1280-wide design canvas. Stretching it to 1280x204 destroys
    // its alpha gradient and was the source of the reported corrupt frame.
    for (int tile = 0; tile < 3; ++tile) {
      SDL_FRect destination{float(16 + tile * 416), 640, 416, 80};
      SDL_RenderTexture(&renderer, texture(renderer, frameTexture_, frame_),
                        nullptr, &destination);
    }
  }
  void renderIndicator(Kotonoha::RendererRef renderer) override {
    const int frame = int((SDL_GetTicks() / 120) % 4);
    // MWIN_CURSOR is four 48x38 animation frames stacked vertically.
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
  AirTextBox(Kotonoha::Engine &engine, const LucaSystemAssets &assets)
      : VNTextBox(engine, "", "", 30),
        frame_(DecodeCzImage(assets.readAsset("MWIN1280_0"))),
        cursor_(DecodeCzImage(assets.readAsset("MWIN_CURSOR"))) {}
};

} // namespace

struct LucaSystemRuntime::Impl {
  LucaSystemRuntime &runtime;
  std::shared_ptr<LucaSystemAssets> assets;
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

  std::vector<ScriptInstruction> instructions;
  size_t instructionIndex = 0;
  std::unordered_map<unsigned, unsigned> objectAssets;
  CzImage background;
  Kotonoha::TexturePtr backgroundTexture;

  std::string currentScenario;
  std::vector<std::string> dialogueLines;
  size_t dialogueIndex = 0;
  bool dialogueVisible = false;

  Kotonoha::PcmPlayer sound;

  explicit Impl(LucaSystemRuntime &r, std::shared_ptr<LucaSystemAssets> a)
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

  static unsigned paramU16(const ScriptInstruction &ins, size_t offset) {
    if (offset + 2 > ins.paramBytes.size())
      return 0;
    return unsigned(ins.paramBytes[offset]) |
           (unsigned(ins.paramBytes[offset + 1]) << 8);
  }

  void runUntilPause() {
    while (instructionIndex < instructions.size()) {
      const auto &ins = instructions[instructionIndex++];
      if (ins.opcode == 36 && !ins.text.empty()) {
        dialogueVisible = true;
        runtime.dialogueText_ = ins.text;
        runtime.dialogueSpeaker_.clear();
        textbox->setDialogue("", ins.text);
        textbox->show();
        return;
      }
      if (ins.opcode == 55) {
        objectAssets[paramU16(ins, 0)] = paramU16(ins, 2);
      } else if (ins.opcode == 137) {
        auto found = objectAssets.find(0);
        if (found != objectAssets.end()) {
          try {
            // 65001 is used by AIR as a special/procedural background token;
            // only ids present in the image archives are decoded here.
            background = DecodeCzImage(assets->readImageAsset(found->second));
            backgroundTexture.reset();
          } catch (const std::exception &error) {
            SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                        "LucaSystem: background asset %u: %s", found->second,
                        error.what());
          }
        }
      }
    }
    running = false;
  }

  void advanceDialogue() {
    SDL_Log("LucaSystem: advanceDialogue index=%zu size=%zu visible=%d",
            dialogueIndex, dialogueLines.size(), dialogueVisible ? 1 : 0);
    if (!dialogueVisible && dialogueIndex < dialogueLines.size()) {
      // Present the next dialogue line.
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
      // Finish current line and move to the next one.
      dialogueVisible = false;
      ++dialogueIndex;
      if (dialogueIndex < dialogueLines.size()) {
        advanceDialogue();
      }
    }
  }
};

LucaSystemRuntime::LucaSystemRuntime(std::shared_ptr<LucaSystemAssets> assets)
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
  // LucaSystem games go directly to gameplay; no engine splash screen.
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
    impl.titleBackground = DecodeCzImage(impl.assets->readAsset("title1a"));
    impl.titleMenu = DecodeCzImage(impl.assets->readAsset("title_menu"));
    impl.titleShown = true;
    return;
  }

  AddScene(impl.textbox);
  AddScene(impl.choiceScene);
  AddScene(impl.backlog);
  AddScene(impl.menu);
  if (!impl.currentScenario.empty()) {
    // Resume a running scenario.
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
      !impl.engine->isStoryPaused())
    impl.runUntilPause();
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
  data << "LUCA_SYSTEM_SAVE_1\n"
       << impl.currentScenario << '\n'
       << impl.instructionIndex << '\n';
  Kotonoha::GetVNManagerRuntime(*impl.engine).save(path, data.str());
}

void LucaSystemRuntime::loadGame(const std::string &path) {
  auto &impl = *impl_;
  auto saved = Kotonoha::VNSave::read(path);
  std::istringstream data(saved.gameData);
  std::string header, scenario;
  size_t index = 0;
  std::getline(data, header);
  if (header == "LUCA_SYSTEM_SAVE_1") {
    std::getline(data, scenario);
    data >> index;
  } else {
    scenario = std::move(header);
  }
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

  auto bytes = impl.assets->readAsset(name);
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

  SDL_Log("LucaSystem: scenario '%s' parsed: %zu instructions, %zu dialogue "
          "lines",
          name.c_str(), script.instructionCount(), impl.dialogueLines.size());

  // Register dialogue lines as translation sources for live translation.
  scenarioTexts_.clear();
  for (const auto &line : impl.dialogueLines)
    if (std::find(scenarioTexts_.begin(), scenarioTexts_.end(), line) ==
        scenarioTexts_.end())
      scenarioTexts_.push_back(line);

  impl.running = true;
  impl.runUntilPause();
  return true;
}

bool LucaSystemRuntime::debugJumpToLabel(const std::string &label) {
  // LucaSystem scripts are presented linearly; label jumps require the
  // game-specific opcode table. Not yet implemented.
  return false;
}

std::string LucaSystemRuntime::debugNode() const {
  auto &impl = *impl_;
  return impl.currentScenario.empty()
             ? std::string()
             : ("luca_system | scenario " + impl.currentScenario);
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
