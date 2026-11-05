#pragma once

#include "DdlcAssets.hpp"

#include <Kotonoha/core/SDL.hpp>

#include <memory>
#include <string>
#include <vector>

namespace Kotonoha {
class Engine;
}

namespace Ddlc {

// Coordinates the Ren'Py VM, presentation and audio for the engine scenes.
class GameSession {
public:
  GameSession(Kotonoha::Engine &engine, std::shared_ptr<GameAssets> assets);
  ~GameSession();
  GameSession(const GameSession &) = delete;
  GameSession &operator=(const GameSession &) = delete;

  void update(bool suspended);
  void startGame();
  void saveGame(const std::string&path);
  void loadGame(const std::string&path);
  void setPaused(bool);
  bool backAction();
  bool openHistory();
  bool historyOpen() const;
  void translationChanged(const std::string &original,
                          const std::string &previous,
                          const std::string &translated);
  std::string dialogueText() const;
  std::string dialogueSourceText() const;
  std::string dialogueSpeaker() const;
  std::string visibleDialogueText() const;
  bool dialogueComplete() const;
  void completeDialogue();
  std::vector<std::string> choiceTexts() const;
  int translationRoute() const;
  bool event(Kotonoha::EventRef event);
  bool inMenu() const;
  bool playing() const;
  bool paused() const;
  bool choosing() const;
  bool showingDialogue() const;
  std::string debugNode() const;
  std::string nodeClockKey() const;
  void debugJump(const std::string &label);

  void beginFrame(Kotonoha::RendererRef renderer, float pixelScale);
  void drawStage(Kotonoha::RendererRef renderer);
  void drawDialogue(Kotonoha::RendererRef renderer);
  void drawChoices(Kotonoha::RendererRef renderer);
  void drawMenu(Kotonoha::RendererRef renderer);
  void drawPause(Kotonoha::RendererRef renderer);
  void endFrame(Kotonoha::RendererRef renderer);
  void abortFrame(Kotonoha::RendererRef renderer) noexcept;

private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

} // namespace Ddlc
