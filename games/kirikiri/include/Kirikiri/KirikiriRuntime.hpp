#pragma once

#include <Kirikiri/KagScript.hpp>
#include <Kirikiri/KirikiriAssets.hpp>
#include <Kotonoha/core/Runtime.hpp>
#include <memory>
#include <string>
#include <unordered_map>

namespace Kirikiri {

// Runtime for a Kirikiri2 / KirikiriZ game. Loads scenario scripts from the
// mounted assets, interprets KAG tags and drives Kotonoha scenes (backgrounds,
// characters, textbox, choices).
class KirikiriRuntime : public Kotonoha::Runtime {
public:
  KirikiriRuntime(std::shared_ptr<Kotonoha::Providers::KirikiriAssets> assets);
  ~KirikiriRuntime() override;

  void init(Kotonoha::Engine &engine) override;
  void initGlobal(Kotonoha::Engine &engine) override;
  void initSplash(Kotonoha::Engine &engine) override;
  void initMenu(Kotonoha::Engine &engine) override;
  void initGameplay(Kotonoha::Engine &engine) override;
  void update() override;
  void renderGameplay(Kotonoha::RendererRef renderer, float scale) override;
  bool onEvent(Kotonoha::EventRef event) override;

  // Advance a blocked dialogue node (called on player advance input or MCP).
  bool handleAdvanceAction() override;

  void saveGame(const std::string &path) override;
  void loadGame(const std::string &path) override;

  // Starts a scenario script by file name (e.g. "first.ks").
  bool startScenario(const std::string &name, std::size_t position = 0);
  // Debug jump to a labeled position inside the current scenario.
  bool debugJumpToLabel(const std::string &label);

  // Advances a blocked dialogue node (called on player advance input).
  void advanceDialogue();

  // Current dialogue text (set by KAG script parser via MCP-safe callback).
  std::string dialogueText_;
  std::string dialogueSpeaker_;
  std::string originalDialogueText_;
  std::string originalDialogueSpeaker_;
  std::vector<std::string> originalActiveKeys_;
  std::vector<std::string> scenarioTexts_;

  std::string debugNode() const override;
  void debugRestartScene() override;
  std::string dialogueText() const override { return dialogueText_; }
  std::string dialogueSpeaker() const override { return dialogueSpeaker_; }
  std::vector<std::string> activeTranslationKeys() const override;
  Kotonoha::Translation::SourceTexts translationSources() const override;
  bool continuousTouch() const override { return true; }

private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

} // namespace Kirikiri
