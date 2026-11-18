// Copyright 2026 Kotonoha contributors. SPDX-License-Identifier: MIT
#include <N2System/N2Runtime.hpp>

#include <Kotonoha/components/ChoiceQuestionScene.hpp>
#include <Kotonoha/components/InGameMenu.hpp>
#include <Kotonoha/components/VNBacklog.hpp>
#include <Kotonoha/components/VNTextBox.hpp>
#include <Kotonoha/core/Engine.hpp>
#include <Kotonoha/events/ControllerEvent.hpp>

#include <fstream>
#include <iostream>

namespace N2System {

struct N2Runtime::Impl {
  std::shared_ptr<Kotonoha::VNTextBox> textBox;
  std::shared_ptr<Kotonoha::VNBacklog> backlog;
  std::shared_ptr<Kotonoha::InGameMenu> menu;
  std::shared_ptr<Kotonoha::ChoiceQuestionScene> choiceScene;
  Kotonoha::Engine *engine = nullptr;
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

  // Default sample script if none loaded from asset archive
  const std::string defaultScript =
      "chapter main {\n"
      "SetVariable(\"$progress\", \"1\");\n"
      "SetDialogue(\"System\", \"N2System Engine Initialized.\");\n"
      "WaitKey();\n"
      "SetDialogue(\"Okabe\", \"El Psy Kongroo.\");\n"
      "WaitKey();\n"
      "Finish();\n"
      "}\n";

  vm_.loadScript(defaultScript, "main");
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

  if (!vm_.finished()) {
    vm_.step();
  }

  if (impl_->textBox) {
    impl_->textBox->setDialogue(std::string(vm_.dialogueSystem().speaker()),
                                vm_.dialogueSystem().visibleText());
  }
}

void N2Runtime::renderGameplay(Kotonoha::RendererRef renderer, float scale) {
  (void)renderer;
  (void)scale;
  // Scene graph entities rendering happens through engine rendering pipeline
}

bool N2Runtime::onEvent(Kotonoha::EventRef event) {
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
