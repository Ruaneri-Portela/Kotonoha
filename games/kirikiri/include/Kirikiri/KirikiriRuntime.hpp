#pragma once

#include <Kotonoha/core/Runtime.hpp>
#include <Kirikiri/KirikiriAssets.hpp>
#include <Kirikiri/KagScript.hpp>
#include <memory>
#include <string>
#include <unordered_map>

namespace Kirikiri {

// Runtime for a Kirikiri2 / KirikiriZ game. Loads scenario scripts from the
// mounted assets, interprets KAG tags and drives Kotonoha scenes (backgrounds,
// characters, textbox, choices).
class KirikiriRuntime : public Kotonoha::Runtime {
public:
  KirikiriRuntime(std::shared_ptr<KirikiriAssets> assets);
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
  bool startScenario(const std::string &name);
  // Debug jump to a labeled position inside the current scenario.
  bool debugJumpToLabel(const std::string &label);

  // Advances a blocked dialogue node (called on player advance input).
  void advanceDialogue();

  // Current dialogue text (set by KAG script parser via MCP-safe callback).
  std::string dialogueText_;
  std::string dialogueSpeaker_;

  std::string debugNode() const override;
  std::string dialogueText() const override { return dialogueText_; }
  std::string dialogueSpeaker() const override { return dialogueSpeaker_; }
  bool continuousTouch() const override { return true; }

private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

} // namespace Kirikiri
