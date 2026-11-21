// Copyright 2026 Kotonoha contributors. SPDX-License-Identifier: MIT
#include <N2System/DebugCatalog.hpp>
#include <N2System/N2Runtime.hpp>
#include <N2System/ScriptIndex.hpp>

#include <Kotonoha/Filesystem/AtomicFile.hpp>
#include <Kotonoha/Filesystem/Filesystem.hpp>
#include <Kotonoha/Filesystem/StoragePaths.hpp>
#include <Kotonoha/components/ChoiceQuestionScene.hpp>
#include <Kotonoha/components/InGameMenu.hpp>
#include <Kotonoha/components/VNBacklog.hpp>
#include <Kotonoha/components/VNSaveMenu.hpp>
#include <Kotonoha/components/VNTextBox.hpp>
#include <Kotonoha/core/Engine.hpp>
#include <Kotonoha/events/ControllerEvent.hpp>

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <stdexcept>
#include <string>
#include <string_view>

namespace N2System {

namespace {
// Route the pause menu's History item to the backlog instance owned and
// populated by this runtime instead of the engine's unused shared backlog.
class N2GameMenu final : public Kotonoha::InGameMenu {
public:
  N2GameMenu(Kotonoha::Engine &engine, Kotonoha::VNBacklog &backlog)
      : InGameMenu(engine), backlog_(backlog) {}

protected:
  void openBacklog() override { backlog_.open(); }

private:
  Kotonoha::VNBacklog &backlog_;
};
} // namespace

struct N2Runtime::Impl {
  std::shared_ptr<Kotonoha::VNTextBox> textBox;
  std::shared_ptr<Kotonoha::VNBacklog> backlog;
  std::shared_ptr<Kotonoha::InGameMenu> menu;
  std::unique_ptr<Kotonoha::VNSaveMenu> saveMenu;
  std::shared_ptr<Kotonoha::ChoiceQuestionScene> choiceScene;
  Kotonoha::Engine *engine = nullptr;
  bool choiceActive = false;
  bool restartRequested = false;
  std::string currentBgm;
  std::string nativeScript;
  std::string nativeScriptId;
  ScriptIndex nativeIndex;
  std::unique_ptr<Ponscripter::PonscripterRuntime> ponscripter;
};

N2Runtime::N2Runtime(std::shared_ptr<N2ArchiveAssets> assets)
    : Kotonoha::Runtime("N2System", "0.1.0", "me.hirameki.kotonoha.n2system"),
      impl_(std::make_unique<Impl>()), assets_(std::move(assets)) {
  if (!assets_) {
    assets_ = std::make_shared<N2ArchiveAssets>();
  }
}

N2Runtime::~N2Runtime() { shutdown(); }

void N2Runtime::shutdown() {
  if (!impl_)
    return;
  if (impl_->ponscripter) {
    impl_->ponscripter->shutdown();
    impl_->ponscripter.reset();
  }
  impl_->choiceScene.reset();
  impl_->saveMenu.reset();
  impl_->menu.reset();
  impl_->backlog.reset();
  impl_->textBox.reset();
  impl_->engine = nullptr;
}

SDL_Point N2Runtime::viewport() const {
  if (impl_->ponscripter)
    return impl_->ponscripter->viewport();
  return SDL_Point{1280, 720};
}

bool N2Runtime::isPonscripterActive() const {
  return impl_->ponscripter != nullptr;
}

Ponscripter::PonscripterVm *N2Runtime::ponscripterVm() {
  return impl_->ponscripter ? &impl_->ponscripter->vm() : nullptr;
}

const Ponscripter::PonscripterVm *N2Runtime::ponscripterVm() const {
  return impl_->ponscripter ? &impl_->ponscripter->vm() : nullptr;
}

void N2Runtime::init(Kotonoha::Engine &engine) {
  impl_->engine = &engine;

  // Check if the mounted directory is a PONscripter title (e.g. 0.utf, 00.utf,
  // 01.utf, arc.nsa) such as SayaNoUta_PT-BR.
  if (assets_ && !assets_->rootDirectory().empty()) {
    auto ponAssets = std::make_shared<Ponscripter::PonscripterAssets>(
        assets_->rootDirectory().string(), assets_->overlayDirectory());
    if (!ponAssets->discoverScripts().empty()) {
      impl_->ponscripter =
          std::make_unique<Ponscripter::PonscripterRuntime>(ponAssets);
      engine.SetAssetProvider(ponAssets);
      impl_->ponscripter->init(engine);
      SetState(Kotonoha::GameState::Gameplay);
      return;
    }
  }

  impl_->textBox = std::make_shared<Kotonoha::VNTextBox>(engine, "", "", 30.0);
  impl_->backlog = std::make_shared<Kotonoha::VNBacklog>(engine);
  impl_->menu = std::make_shared<N2GameMenu>(engine, *impl_->backlog);
  impl_->saveMenu = std::make_unique<Kotonoha::VNSaveMenu>(engine);
  impl_->choiceScene = std::make_shared<Kotonoha::ChoiceQuestionScene>(
      engine, "", std::vector<Kotonoha::ChoiceOption>{{"", 0}}, nullptr);

  AddScene(impl_->backlog.get(), Kotonoha::GameState::Gameplay);
  AddScene(impl_->menu.get(), Kotonoha::GameState::Gameplay);

  const auto openSaves = [this](bool load) {
    if (!impl_->saveMenu)
      return;
    const auto dir = Kotonoha::saveDirectory() / "n2system";
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    impl_->saveMenu->open(dir.string(), load,
                          std::string(vm_.currentChapter()));
    const auto &scenes = GetActiveScenes();
    if (std::find(scenes.begin(), scenes.end(), impl_->saveMenu.get()) ==
        scenes.end())
      AddScene(impl_->saveMenu.get(), Kotonoha::GameState::Gameplay);
  };
  impl_->menu->SetOnSave([this, openSaves] {
    impl_->menu->close();
    openSaves(false);
  });
  impl_->menu->SetOnLoad([this, openSaves] {
    impl_->menu->close();
    openSaves(true);
  });
  impl_->menu->SetOnReturnToMenu([this] { impl_->restartRequested = true; });

  std::string scriptSource;
  std::string initialChapter = "main";
  impl_->nativeScriptId.clear();
  impl_->nativeScript.clear();

  for (const char *path : {"scenario.nss", "n2system.nss", "start.nss",
                           "nss/0_boot.nss", "nss/main.nss"}) {
    if (assets_ && assets_->contains(path)) {
      scriptSource = assets_->loadTextUtf8(path, "CP932");
      impl_->nativeScriptId = path;
      break;
    }
  }

  if (!scriptSource.empty()) {
    try {
      auto index = ScriptIndex::Parse(scriptSource);
      if (index.findChapter("main"))
        initialChapter = "main";
      else if (!index.chapters().empty())
        initialChapter = index.chapters().front().name;
      impl_->nativeIndex = std::move(index);
    } catch (...) {
      scriptSource.clear();
      impl_->nativeScriptId.clear();
    }
  }

  if (scriptSource.empty()) {
    scriptSource =
        "chapter main {\n"
        "SetVariable(\"$progress\", \"1\");\n"
        "CreateSprite(\"bg\", \"bg_lab.png\");\n"
        "PlayBgm(\"bgm_gate.ogg\", 1);\n"
        "SetDialogue(\"System\", \"N2System Engine Initialized.\");\n"
        "WaitKey();\n"
        "SetDialogue(\"Okabe\", \"El Psy Kongroo.\");\n"
        "WaitKey();\n"
        "Finish();\n"
        "}\n";
    initialChapter = "main";
  }

  impl_->nativeScript = scriptSource;
  impl_->nativeIndex = ScriptIndex::Parse(impl_->nativeScript);
  vm_.loadScript(scriptSource, initialChapter);
  SetState(Kotonoha::GameState::Gameplay);
}

void N2Runtime::initGlobal(Kotonoha::Engine &engine) { (void)engine; }

void N2Runtime::initSplash(Kotonoha::Engine &engine) { (void)engine; }

void N2Runtime::initMenu(Kotonoha::Engine &engine) { (void)engine; }

void N2Runtime::initGameplay(Kotonoha::Engine &engine) {
  impl_->engine = &engine;
}

void N2Runtime::update() {
  if (impl_->ponscripter) {
    impl_->ponscripter->update();
    return;
  }

  Runtime::update();
  if (impl_->restartRequested && impl_->engine) {
    impl_->restartRequested = false;
    auto *engine = impl_->engine;
    SetState(Kotonoha::GameState::Gameplay);
    // init() replaces every registered scene; drop stale pointers first.
    ClearScenes();
    init(*engine);
    return;
  }
  if (impl_->engine && impl_->engine->isStoryPaused())
    return;

  constexpr float dt = 1.0f / 60.0f;
  vm_.update(dt);

  if (vm_.hasPendingChoice() && !impl_->choiceActive && impl_->choiceScene) {
    auto options = vm_.pendingChoiceOptions();
    std::vector<Kotonoha::ChoiceOption> choiceItems;
    for (size_t i = 0; i < options.size(); ++i) {
      choiceItems.push_back({impl_->engine
                                 ? impl_->engine->GetTranslation().text(
                                       translationRoute(), options[i])
                                 : options[i],
                             static_cast<int>(i)});
    }
    impl_->choiceActive = true;
    impl_->choiceScene->setQuestion(
        "", choiceItems, [this](int optionId, const std::string &label) {
          (void)label;
          impl_->choiceActive = false;
          vm_.selectChoice(optionId);
          vm_.variables().set("$route", static_cast<int64_t>(optionId));
        });
  }

  if (!vm_.finished() && !impl_->choiceActive) {
    vm_.step();
  }

  if (impl_->textBox) {
    const auto &dialogue = vm_.dialogueSystem();
    const auto rawSpeaker = std::string(dialogue.speaker());
    const auto rawText = std::string(dialogue.fullText());
    const auto route = translationRoute();
    const auto speaker =
        impl_->engine ? impl_->engine->GetTranslation().text(route, rawSpeaker)
                      : rawSpeaker;
    const auto line = impl_->engine
                          ? impl_->engine->GetTranslation().text(route, rawText)
                          : rawText;
    // Preserve the native VM's reveal cadence for untranslated text.
    // Translations have a different glyph count and are displayed as a
    // complete line instead of slicing a multibyte UTF-8 character.
    impl_->textBox->setDialogue(
        speaker, line == rawText ? dialogue.visibleText() : line);
    impl_->textBox->update();
  }

  if (impl_->choiceActive && impl_->choiceScene) {
    impl_->choiceScene->update();
  }
}

void N2Runtime::renderGameplay(Kotonoha::RendererRef renderer, float scale) {
  if (impl_->ponscripter) {
    impl_->ponscripter->renderGameplay(renderer, scale);
    return;
  }

  SDL_SetRenderDrawColor(&renderer, 20, 24, 32, 255);
  SDL_RenderClear(&renderer);

  for (const auto &name : vm_.sceneGraph().renderOrder()) {
    const auto *entity = vm_.sceneGraph().findEntity(name);
    if (!entity || !entity->visible || entity->opacity <= 0.01f)
      continue;

    if (entity->type == EntityType::Sprite) {
      SDL_Texture *tex = nullptr;
      if (assets_ && !entity->imagePath.empty() &&
          assets_->contains(entity->imagePath)) {
        try {
          tex = &assets_->texture(renderer, entity->imagePath);
        } catch (...) {
          tex = nullptr;
        }
      }
      if (tex) {
        float tw = 1280.0f, th = 720.0f;
        SDL_GetTextureSize(tex, &tw, &th);
        SDL_SetTextureAlphaMod(tex,
                               static_cast<Uint8>(entity->opacity * 255.0f));
        SDL_FRect dst{entity->x * scale, entity->y * scale, tw * scale,
                      th * scale};
        SDL_RenderTexture(&renderer, tex, nullptr, &dst);
      } else {
        SDL_FRect rect{entity->x * scale, entity->y * scale, 1280.0f * scale,
                       720.0f * scale};
        SDL_SetRenderDrawColor(&renderer, 30, 36, 48,
                               static_cast<Uint8>(entity->opacity * 255.0f));
        SDL_RenderFillRect(&renderer, &rect);
      }
    }
  }

  if (impl_->textBox && !impl_->choiceActive) {
    impl_->textBox->render(renderer, scale);
  }

  if (impl_->choiceActive && impl_->choiceScene) {
    impl_->choiceScene->render(renderer, scale);
  }
}

bool N2Runtime::onEvent(Kotonoha::EventRef event) {
  if (impl_->ponscripter)
    return impl_->ponscripter->onEvent(event);

  if ((impl_->saveMenu && impl_->saveMenu->isOpen()) ||
      (impl_->backlog && impl_->backlog->isVisible()) ||
      (impl_->menu && impl_->menu->IsOpen()))
    return Runtime::onEvent(event);

  if (impl_->choiceActive && impl_->choiceScene) {
    if (impl_->choiceScene->onEvent(event))
      return true;
  }

  if (event.type == SDL_EVENT_KEY_DOWN && !event.key.repeat) {
    if (event.key.key == SDLK_ESCAPE && impl_->menu) {
      impl_->menu->open();
      return true;
    }
    if (event.key.key == SDLK_B && impl_->backlog) {
      impl_->backlog->toggle();
      return true;
    }
    if (event.key.key == SDLK_F5) {
      saveGame((Kotonoha::saveDirectory() / "n2system_quick.n2sv").string());
      return true;
    }
    if (event.key.key == SDLK_F6) {
      loadGame((Kotonoha::saveDirectory() / "n2system_quick.n2sv").string());
      return true;
    }
  }

  if (event.type == SDL_EVENT_KEY_DOWN ||
      event.type == SDL_EVENT_MOUSE_BUTTON_DOWN) {
    return handleAdvanceAction();
  }
  return false;
}

bool N2Runtime::handleAdvanceAction() {
  if (impl_->ponscripter)
    return impl_->ponscripter->handleAdvanceAction();
  return vm_.advance();
}

bool N2Runtime::advanceStoryTime(double seconds) {
  if (impl_->ponscripter)
    return impl_->ponscripter->advanceStoryTime(seconds);
  vm_.update(static_cast<float>(seconds));
  return true;
}

void N2Runtime::saveGame(const std::string &path) {
  if (impl_->ponscripter) {
    impl_->ponscripter->saveGame(path);
    return;
  }
  auto saveData = vm_.saveState();
  auto bytes = SaveStateCodec::serializeBinary(saveData);
  std::string err;
  (void)Kotonoha::Filesystem::writeBinaryFileAtomically(
      std::filesystem::path(path), bytes, &err);
}

void N2Runtime::loadGame(const std::string &path) {
  if (impl_->ponscripter) {
    impl_->ponscripter->loadGame(path);
    return;
  }
  try {
    auto bytes = Kotonoha::Filesystem::readLocalFile(path);
    SaveData loaded;
    if (SaveStateCodec::deserializeBinary(bytes, loaded)) {
      vm_.loadState(loaded);
    }
  } catch (...) {
  }
}

std::string N2Runtime::debugNode() const {
  if (impl_->ponscripter)
    return impl_->ponscripter->debugNode();
  const auto chapter = vm_.currentChapter();
  return chapter.empty() ? std::string{}
                         : "n2 | chapter " + std::string(chapter);
}

std::string N2Runtime::dialogueText() const {
  if (impl_->ponscripter)
    return impl_->ponscripter->dialogueText();
  return std::string(vm_.dialogueSystem().fullText());
}

std::string N2Runtime::dialogueSpeaker() const {
  if (impl_->ponscripter)
    return impl_->ponscripter->dialogueSpeaker();
  return std::string(vm_.dialogueSystem().speaker());
}

std::vector<Kotonoha::DebugNode> N2Runtime::debugNodes() const {
  if (impl_->ponscripter)
    return impl_->ponscripter->debugNodes();
  std::vector<Kotonoha::DebugNode> nodes;
  for (const auto &chapter : impl_->nativeIndex.chapters()) {
    Kotonoha::DebugNode node{
        "n2 | chapter " + chapter.name, "Chapter " + chapter.name, {}};
    if (!impl_->nativeScriptId.empty())
      node.assets.push_back(impl_->nativeScriptId);
    if (vm_.currentChapter() == chapter.name) {
      for (const auto &name : vm_.sceneGraph().renderOrder()) {
        const auto *entity = vm_.sceneGraph().findEntity(name);
        if (entity && !entity->imagePath.empty() && assets_ &&
            assets_->contains(entity->imagePath))
          node.assets.push_back(entity->imagePath);
      }
      const auto voice = std::string(vm_.dialogueSystem().voicePath());
      if (!voice.empty() && assets_ && assets_->contains(voice))
        node.assets.push_back(voice);
    }
    nodes.push_back(std::move(node));
    for (const auto &label : chapter.labels) {
      Kotonoha::DebugNode entry{
          "n2 | chapter " + chapter.name + " | label " + label.name,
          "Label " + label.name + " (line " + std::to_string(label.line) + ")",
          {}};
      if (!impl_->nativeScriptId.empty())
        entry.assets.push_back(impl_->nativeScriptId);
      nodes.push_back(std::move(entry));
    }
  }
  return nodes;
}

void N2Runtime::debugJumpToNode(const std::string &id) {
  if (impl_->ponscripter) {
    impl_->ponscripter->debugJumpToNode(id);
    return;
  }
  constexpr std::string_view prefix = "n2 | chapter ";
  constexpr std::string_view labelSep = " | label ";
  if (id.compare(0, prefix.size(), prefix) != 0)
    throw std::invalid_argument("Unknown N2System debug node: " + id);
  const std::string_view body(id.data() + prefix.size(),
                              id.size() - prefix.size());
  const auto labelAt = body.find(labelSep);
  const auto chapterName = body.substr(0, labelAt);
  const auto *chapter = impl_->nativeIndex.findChapter(chapterName);
  if (!chapter)
    throw std::invalid_argument("Unknown N2System chapter: " + id);
  const auto label = labelAt == std::string_view::npos
                         ? std::string_view{}
                         : body.substr(labelAt + labelSep.size());
  // Reject all unknown targets *before* resetting the VM state.
  if (labelAt != std::string_view::npos &&
      (label.empty() || !chapter->findLabel(label)))
    throw std::invalid_argument("Unknown N2System label: " + id);
  if (vm_.currentChapter() != chapterName || label.empty())
    vm_.loadScript(impl_->nativeScript, chapterName);
  if (!label.empty() && !vm_.debugJumpToLabel(label))
    throw std::logic_error("N2System validated label jump failed");
  impl_->choiceActive = false;
  SetState(Kotonoha::GameState::Gameplay);
}

void N2Runtime::debugRestartScene() {
  if (impl_->ponscripter) {
    impl_->ponscripter->debugRestartScene();
    return;
  }
  debugJumpToNode(debugNode());
}

int N2Runtime::translationRoute() const {
  if (impl_->ponscripter)
    return impl_->ponscripter->translationRoute();
  const auto current = vm_.currentChapter();
  const auto &chapters = impl_->nativeIndex.chapters();
  for (std::size_t i = 0; i < chapters.size(); ++i)
    if (chapters[i].name == current)
      return static_cast<int>(i);
  return -1;
}

std::vector<int> N2Runtime::translationRoutes() const {
  if (impl_->ponscripter)
    return impl_->ponscripter->translationRoutes();
  std::vector<int> result;
  for (std::size_t i = 0; i < impl_->nativeIndex.chapters().size(); ++i)
    result.push_back(static_cast<int>(i));
  return result;
}

Kotonoha::Translation::SourceTexts N2Runtime::translationSources() const {
  if (impl_->ponscripter)
    return impl_->ponscripter->translationSources();
  // The standalone NsScript catalog uses real argument positions and ignores
  // audio filenames, control IDs and comments without executing the VM.
  return ExtractTranslationTexts(impl_->nativeIndex, impl_->nativeScript);
}

std::vector<std::string> N2Runtime::activeTranslationKeys() const {
  if (impl_->ponscripter)
    return impl_->ponscripter->activeTranslationKeys();
  std::vector<std::string> keys;
  const auto append = [&](const std::string &value) {
    if (!value.empty() &&
        std::find(keys.begin(), keys.end(), value) == keys.end())
      keys.push_back(value);
  };
  append(std::string(vm_.dialogueSystem().speaker()));
  append(std::string(vm_.dialogueSystem().fullText()));
  if (vm_.hasPendingChoice())
    for (const auto &value : vm_.pendingChoiceOptions())
      append(value);
  return keys;
}

std::vector<Kotonoha::DebugAudioAsset> N2Runtime::debugSceneAudio() {
  if (impl_->ponscripter)
    return impl_->ponscripter->debugSceneAudio();
  const auto &dialogue = vm_.dialogueSystem();
  if (dialogue.voicePath().empty())
    return {};
  return {{std::string(dialogue.voicePath()), std::string(dialogue.speaker()),
           std::string(dialogue.fullText())}};
}

std::optional<Kotonoha::AudioClip>
N2Runtime::debugOriginalAudio(const std::string &id) {
  if (impl_->ponscripter)
    return impl_->ponscripter->debugOriginalAudio(id);
  return std::nullopt;
}

void N2Runtime::debugOverlayAssetChanged(const std::string &id) {
  if (impl_->ponscripter)
    impl_->ponscripter->debugOverlayAssetChanged(id);
  else if (assets_)
    assets_->clearCache();
}

} // namespace N2System
