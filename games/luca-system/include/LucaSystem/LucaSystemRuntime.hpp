#pragma once

#include <LucaSystem/LucaSystemAssets.hpp>
#include <Kotonoha/core/Runtime.hpp>

#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace LucaSystem {

// Runtime for a LucaSystem (Visual Art's / Key) game. Mounts the game's
// .PAK archives, parses the LucaSystem script bytecode and presents the
// extracted dialogue through Kotonoha scenes (textbox, choices, backlog,
// menu).
//
// The VM implements the opcode semantics recovered by reverse engineering
// the original AIR.exe binaries (via rizin/REA) and analysis of the
// SCRIPT.PAK bytecode: MESSAGE, FADE, IMAGELOAD, DRAW, WAIT, INIT,
// HAIKIE_SET, EQUN/EQU, BGM, VOICE, SE, JUMP, CALL, RETURN, CLOSE_WINDOW,
// WIPE, IMAGEUPDATE and object animation opcodes.
class LucaSystemRuntime : public Kotonoha::Runtime {
public:
  LucaSystemRuntime(std::shared_ptr<LucaSystemAssets> assets);
  ~LucaSystemRuntime() override;

  void init(Kotonoha::Engine &engine) override;
  void initGlobal(Kotonoha::Engine &engine) override;
  void initSplash(Kotonoha::Engine &engine) override;
  void initMenu(Kotonoha::Engine &engine) override;
  void initGameplay(Kotonoha::Engine &engine) override;
  void update() override;
  void renderGameplay(Kotonoha::RendererRef renderer, float scale) override;
  bool onEvent(Kotonoha::EventRef event) override;

  bool handleAdvanceAction() override;

  void saveGame(const std::string &path) override;
  void loadGame(const std::string &path) override;

  // Start / resume a scenario script by SCRIPT.PAK entry name.
  bool startScenario(const std::string &name, size_t position = 0);
  bool debugJumpToLabel(const std::string &label);

  void advanceDialogue();

  std::string dialogueText() const override { return dialogueText_; }
  std::string dialogueSpeaker() const override { return dialogueSpeaker_; }
  std::string debugNode() const override;
  void debugRestartScene() override;
  std::vector<std::string> activeTranslationKeys() const override;
  Kotonoha::Translation::SourceTexts translationSources() const override;
  bool continuousTouch() const override { return true; }

  const LucaSystemAssets &assets() const { return *assets_; }

  // Current dialogue state (filled by the script interpreter).
  std::string dialogueText_;
  std::string dialogueSpeaker_;
  std::vector<std::string> originalActiveKeys_;
  std::vector<std::string> scenarioTexts_;

private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
  std::shared_ptr<LucaSystemAssets> assets_;
};

} // namespace LucaSystem
