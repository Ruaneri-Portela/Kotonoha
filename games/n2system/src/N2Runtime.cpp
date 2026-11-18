// Copyright 2026 Kotonoha contributors. SPDX-License-Identifier: MIT
#include <N2System/N2Runtime.hpp>

#include <Kotonoha/Filesystem/AtomicFile.hpp>
#include <Kotonoha/Filesystem/Filesystem.hpp>
#include <Kotonoha/Filesystem/StoragePaths.hpp>
#include <Kotonoha/components/ChoiceQuestionScene.hpp>
#include <Kotonoha/components/InGameMenu.hpp>
#include <Kotonoha/components/VNBacklog.hpp>
#include <Kotonoha/components/VNTextBox.hpp>
#include <Kotonoha/core/Engine.hpp>
#include <Kotonoha/events/ControllerEvent.hpp>

#include <filesystem>
#include <string>

namespace N2System {

struct N2Runtime::Impl {
  std::shared_ptr<Kotonoha::VNTextBox> textBox;
  std::shared_ptr<Kotonoha::VNBacklog> backlog;
  std::shared_ptr<Kotonoha::InGameMenu> menu;
  std::shared_ptr<Kotonoha::ChoiceQuestionScene> choiceScene;
  Kotonoha::Engine *engine = nullptr;
  bool choiceActive = false;
  std::string currentBgm;
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
  impl_->menu = std::make_shared<Kotonoha::InGameMenu>(engine);
  impl_->choiceScene = std::make_shared<Kotonoha::ChoiceQuestionScene>(
      engine, "", std::vector<Kotonoha::ChoiceOption>{{"", 0}}, nullptr);

  std::string scriptSource;
  std::string initialChapter = "main";

  for (const char *path : {"scenario.nss", "n2system.nss", "start.nss",
                           "nss/0_boot.nss", "nss/main.nss"}) {
    if (assets_ && assets_->contains(path)) {
      scriptSource = assets_->loadTextUtf8(path, "CP932");
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
    } catch (...) {
      scriptSource.clear();
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

  constexpr float dt = 1.0f / 60.0f;
  vm_.update(dt);

  if (vm_.hasPendingChoice() && !impl_->choiceActive && impl_->choiceScene) {
    auto options = vm_.pendingChoiceOptions();
    std::vector<Kotonoha::ChoiceOption> choiceItems;
    for (size_t i = 0; i < options.size(); ++i) {
      choiceItems.push_back({options[i], static_cast<int>(i)});
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
    impl_->textBox->setDialogue(std::string(vm_.dialogueSystem().speaker()),
                                vm_.dialogueSystem().visibleText());
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

  if (impl_->choiceActive && impl_->choiceScene) {
    if (impl_->choiceScene->onEvent(event))
      return true;
  }

  if (event.type == SDL_EVENT_KEY_DOWN) {
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
  return {};
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
  return {};
}

void N2Runtime::debugJumpToNode(const std::string &id) {
  if (impl_->ponscripter)
    impl_->ponscripter->debugJumpToNode(id);
}

void N2Runtime::debugRestartScene() {
  if (impl_->ponscripter)
    impl_->ponscripter->debugRestartScene();
}

int N2Runtime::translationRoute() const {
  if (impl_->ponscripter)
    return impl_->ponscripter->translationRoute();
  return -1;
}

std::vector<int> N2Runtime::translationRoutes() const {
  if (impl_->ponscripter)
    return impl_->ponscripter->translationRoutes();
  return {};
}

Kotonoha::Translation::SourceTexts N2Runtime::translationSources() const {
  if (impl_->ponscripter)
    return impl_->ponscripter->translationSources();
  return {};
}

std::vector<std::string> N2Runtime::activeTranslationKeys() const {
  if (impl_->ponscripter)
    return impl_->ponscripter->activeTranslationKeys();
  return {};
}

std::vector<Kotonoha::DebugAudioAsset> N2Runtime::debugSceneAudio() {
  if (impl_->ponscripter)
    return impl_->ponscripter->debugSceneAudio();
  return {};
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
}

} // namespace N2System
