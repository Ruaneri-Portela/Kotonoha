// Copyright 2026 Kotonoha contributors. SPDX-License-Identifier: MIT
#include <N2System/N2Runtime.hpp>
#include <N2System/ONScripterConverter.hpp>

#include <Kotonoha/components/ChoiceQuestionScene.hpp>
#include <Kotonoha/components/InGameMenu.hpp>
#include <Kotonoha/components/VNBacklog.hpp>
#include <Kotonoha/components/VNTextBox.hpp>
#include <Kotonoha/core/Engine.hpp>
#include <Kotonoha/events/ControllerEvent.hpp>

#include <filesystem>
#include <fstream>
#include <iostream>

namespace N2System {

struct N2Runtime::Impl {
  std::shared_ptr<Kotonoha::VNTextBox> textBox;
  std::shared_ptr<Kotonoha::VNBacklog> backlog;
  std::shared_ptr<Kotonoha::InGameMenu> menu;
  std::shared_ptr<Kotonoha::ChoiceQuestionScene> choiceScene;
  Kotonoha::Engine *engine = nullptr;
  bool choiceActive = false;
};

N2Runtime::N2Runtime(std::shared_ptr<N2ArchiveAssets> assets)
    : Kotonoha::Runtime("N2System", "0.1.0", "n2system"),
      impl_(std::make_unique<Impl>()), assets_(std::move(assets)) {
  if (!assets_) {
    assets_ = std::make_shared<N2ArchiveAssets>();
  }
}

N2Runtime::~N2Runtime() = default;

void N2Runtime::init(Kotonoha::Engine &engine) {
  impl_->engine = &engine;

  impl_->textBox = std::make_shared<Kotonoha::VNTextBox>(engine, "", "", 30.0);
  impl_->backlog = std::make_shared<Kotonoha::VNBacklog>(engine);
  impl_->menu = std::make_shared<Kotonoha::InGameMenu>(engine);
  impl_->choiceScene = std::make_shared<Kotonoha::ChoiceQuestionScene>(
      engine, "", std::vector<Kotonoha::ChoiceOption>{{"", 0}}, nullptr);

  std::string scriptSource;
  std::string initialChapter = "main";

  // Check ONScripter / PONScripter (.utf) files first e.g. Saya no Uta 00.utf / 01.utf
  std::string utf00, utf01;
  if (assets_ && assets_->containsAsset("00.utf")) {
    auto b = assets_->readAsset("00.utf");
    utf00.assign(reinterpret_cast<const char *>(b.data()), b.size());
  }
  if (assets_ && assets_->containsAsset("01.utf")) {
    auto b = assets_->readAsset("01.utf");
    utf01.assign(reinterpret_cast<const char *>(b.data()), b.size());
  }

  if (utf00.empty() || utf01.empty()) {
    std::filesystem::path rootDir = assets_ ? assets_->rootDirectory() : std::filesystem::path();
    if (rootDir.empty() || !std::filesystem::exists(rootDir))
      rootDir = "/Volumes/Nero 256GB/KotonohaAssets/SayaNoUta_PT-BR";

    if (std::filesystem::exists(rootDir / "00.utf") && utf00.empty()) {
      std::ifstream ifs(rootDir / "00.utf");
      if (ifs)
        utf00.assign((std::istreambuf_iterator<char>(ifs)), std::istreambuf_iterator<char>());
    }
    if (std::filesystem::exists(rootDir / "01.utf") && utf01.empty()) {
      std::ifstream ifs(rootDir / "01.utf");
      if (ifs)
        utf01.assign((std::istreambuf_iterator<char>(ifs)), std::istreambuf_iterator<char>());
    }
  }

  if (!utf01.empty()) {
    std::string combined = utf00 + "\n" + utf01;
    scriptSource = ONScripterConverter::convert(combined);

    try {
      auto index = ScriptIndex::Parse(scriptSource);
      if (index.findChapter("001"))
        initialChapter = "001";
      else if (index.findChapter("_001"))
        initialChapter = "_001";
      else if (index.findChapter("start"))
        initialChapter = "start";
      else if (!index.chapters().empty())
        initialChapter = index.chapters().front().name;
    } catch (...) {
      initialChapter = "main";
    }
  }

  // Attempt to load from mounted archive or game-assets directory if ONScripter script was not found
  if (scriptSource.empty()) {
    for (const char *path : {"scenario.nss", "n2system.nss", "start.nss", "games/n2system/game-assets/scenario.nss"}) {
      if (assets_ && assets_->containsAsset(path)) {
        auto bytes = assets_->readAsset(path);
        scriptSource.assign(reinterpret_cast<const char *>(bytes.data()), bytes.size());
        break;
      } else if (std::filesystem::exists(path)) {
        std::ifstream ifs(path);
        if (ifs) {
          scriptSource.assign((std::istreambuf_iterator<char>(ifs)),
                              std::istreambuf_iterator<char>());
          break;
        }
      }
    }
  }

  // Built-in fallback script if no file is present
  if (scriptSource.empty()) {
    scriptSource =
        "chapter main {\n"
        "SetVariable(\"$progress\", \"1\");\n"
        "CreateSprite(\"bg\", \"bg_lab.png\");\n"
        "PlayBgm(\"bgm_gate.ogg\", 1);\n"
        "SetDialogue(\"Sistema\", \"N2System Motor - NitroSharp Reference Port\");\n"
        "WaitKey();\n"
        "SetDialogue(\"Okabe Rintarou\", \"Eu sou o cientista louco, Hououin Kyouma! El Psy Kongroo!\");\n"
        "WaitKey();\n"
        "SetDialogue(\"Makise Kurisu\", \"Para de falar besteira, Okabe! Voce esta atrapalhando os experimentos.\");\n"
        "WaitKey();\n"
        "SetDialogue(\"Mayuri Shiina\", \"Tutturu~! Oiii, Okarin! A Mayushii trouxe frango frito!\");\n"
        "WaitKey();\n"
        "SetDialogue(\"Okabe Rintarou\", \"O que devemos fazer agora no laboratorio?\");\n"
        "WaitKey();\n"
        "CreateChoice(\"choice_1\", \"Conversar com a Kurisu\", \"Comer frango frito com a Mayuri\", \"Ativar o PhoneWave\");\n"
        "WaitKey();\n"
        "Chapter(\"route_kurisu\");\n"
        "}\n"
        "chapter route_kurisu {\n"
        "SetDialogue(\"Makise Kurisu\", \"Uma maquina do tempo baseada em micro-ondas? Isso desafia as leis da fisica!\");\n"
        "WaitKey();\n"
        "SetDialogue(\"Okabe Rintarou\", \"Mas os D-Mails funcionam, Christina! Nos enviamos mensagens para o passado!\");\n"
        "WaitKey();\n"
        "SetDialogue(\"Makise Kurisu\", \"Nao me chame de Christina! E alem disso, Dr Pepper nao eh o combustivel do tempo.\");\n"
        "WaitKey();\n"
        "Chapter(\"ending\");\n"
        "}\n"
        "chapter ending {\n"
        "SetDialogue(\"Okabe Rintarou\", \"Este eh o destino da linha do tempo. El Psy Kongroo!\");\n"
        "WaitKey();\n"
        "SetDialogue(\"Sistema\", \"Fim da demonstracao do N2System Motor.\");\n"
        "WaitKey();\n"
        "Finish();\n"
        "}\n";
    initialChapter = "main";
  }

  vm_.loadScript(scriptSource, initialChapter);
}

void N2Runtime::initGlobal(Kotonoha::Engine &engine) { (void)engine; }

void N2Runtime::initSplash(Kotonoha::Engine &engine) { (void)engine; }

void N2Runtime::initMenu(Kotonoha::Engine &engine) { (void)engine; }

void N2Runtime::initGameplay(Kotonoha::Engine &engine) {
  impl_->engine = &engine;
}

void N2Runtime::update() {
  float dt = 0.016f; // approx 60 fps
  vm_.update(dt);

  // Check if VM reached a choice
  if (vm_.hasPendingChoice() && !impl_->choiceActive) {
    auto options = vm_.pendingChoiceOptions();
    std::vector<Kotonoha::ChoiceOption> choiceItems;
    for (size_t i = 0; i < options.size(); ++i) {
      choiceItems.push_back({options[i], static_cast<int>(i)});
    }
    impl_->choiceActive = true;
    impl_->choiceScene->setQuestion(
        "Escolha uma opcao:", choiceItems,
        [this](int optionId, const std::string &label) {
          (void)label;
          impl_->choiceActive = false;
          vm_.selectChoice(optionId);
          // Jump to corresponding route chapter based on choice index
          if (optionId == 0)
            vm_.variables().set("$route", int64_t(0));
          else if (optionId == 1)
            vm_.variables().set("$route", int64_t(1));
          else
            vm_.variables().set("$route", int64_t(2));
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
  // Clear screen / Draw dark background
  SDL_SetRenderDrawColor(&renderer, 20, 24, 32, 255);
  SDL_RenderClear(&renderer);

  // Render SceneGraph entities (e.g. background sprite, character images, overlay color)
  for (const auto &name : vm_.sceneGraph().renderOrder()) {
    const auto *entity = vm_.sceneGraph().findEntity(name);
    if (!entity || !entity->visible || entity->opacity <= 0.01f)
      continue;

    if (entity->type == EntityType::Sprite) {
      // Draw a subtle decorative background card for sprites
      SDL_FRect rect{entity->x * scale, entity->y * scale, 1280.0f * scale, 720.0f * scale};
      SDL_SetRenderDrawColor(&renderer, 30, 36, 48, static_cast<Uint8>(entity->opacity * 255.0f));
      SDL_RenderFillRect(&renderer, &rect);
    }
  }

  // Render dialogue textbox
  if (impl_->textBox && !impl_->choiceActive) {
    impl_->textBox->render(renderer, scale);
  }

  // Render choice UI overlay if active
  if (impl_->choiceActive && impl_->choiceScene) {
    impl_->choiceScene->render(renderer, scale);
  }
}

bool N2Runtime::onEvent(Kotonoha::EventRef event) {
  if (impl_->choiceActive && impl_->choiceScene) {
    if (impl_->choiceScene->onEvent(event))
      return true;
  }

  if (event.type == SDL_EVENT_KEY_DOWN) {
    if (event.key.key == SDLK_F5) {
      saveGame("n2system_quick.n2sv");
      return true;
    }
    if (event.key.key == SDLK_F6) {
      loadGame("n2system_quick.n2sv");
      return true;
    }
  }

  if (event.type == SDL_EVENT_KEY_DOWN ||
      event.type == SDL_EVENT_MOUSE_BUTTON_DOWN) {
    return handleAdvanceAction();
  }
  return false;
}

bool N2Runtime::handleAdvanceAction() { return vm_.advance(); }

void N2Runtime::saveGame(const std::string &path) {
  auto saveData = vm_.saveState();
  auto bytes = SaveStateCodec::serializeBinary(saveData);
  std::ofstream ofs(path, std::ios::binary);
  if (ofs) {
    ofs.write(reinterpret_cast<const char *>(bytes.data()), bytes.size());
  }
}

void N2Runtime::loadGame(const std::string &path) {
  std::ifstream ifs(path, std::ios::binary);
  if (ifs) {
    std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(ifs)),
                               std::istreambuf_iterator<char>());
    SaveData loaded;
    if (SaveStateCodec::deserializeBinary(bytes, loaded)) {
      vm_.loadState(loaded);
    }
  }
}

std::string N2Runtime::dialogueText() const {
  return std::string(vm_.dialogueSystem().fullText());
}

std::string N2Runtime::dialogueSpeaker() const {
  return std::string(vm_.dialogueSystem().speaker());
}

} // namespace N2System
