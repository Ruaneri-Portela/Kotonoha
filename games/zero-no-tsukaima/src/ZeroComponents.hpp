#pragma once

#include "ZeroSession.hpp"

#include <Kotonoha/componentes/ChoiceQuestionScene.hpp>
#include <Kotonoha/componentes/GameMenu.hpp>
#include <Kotonoha/componentes/InGameMenu.hpp>
#include <Kotonoha/componentes/VNBacklog.hpp>
#include <Kotonoha/componentes/VNTextBox.hpp>
#include <Kotonoha/core/Scene.hpp>

#include <string>
#include <vector>

// The engine components specialised for Zero no Tsukaima. Each one keeps the
// engine behaviour (input routing, story pause, state changes) and overrides
// only the hooks that differ: what is shown, where it is hit and what an item does.
// Every scene draws and receives events in 640x448 game units (ZeroRuntime).
namespace ZeroNoTsukaima {

// Title / mode menu / galleries: the retail front end behind the GameMenu
// contract. activate(0) starts the story through GameMenu (Gameplay state).
class ZeroTitleMenu : public Kotonoha::GameMenu {
public:
  bool designSpace() const override { return false; }
  ZeroTitleMenu(Kotonoha::Engine &engine, ZeroSession &session);

  bool isVisible() const override;
  void onEvent(Kotonoha::EventRef event) override;
  void update() override;

protected:
  int itemCount() const override { return 0; } // the front end draws its own items
  void activate(int index) override;
  void renderTitle(Kotonoha::RendererRef renderer) override;

private:
  ZeroSession &session_;
};

// Backgrounds, sprites, event CGs, transitions, movies and meters.
class ZeroStage : public Kotonoha::Scene {
public:
  ZeroStage(Kotonoha::Engine &engine, ZeroSession &session) : Scene(engine), session_(session) {}
  void render(Kotonoha::RendererRef renderer, float scale) override;

private:
  ZeroSession &session_;
};

// MAIN_MESSAGEBASE window with the original typewriter, auto mode and Ctrl skip.
class ZeroTextBox : public Kotonoha::VNTextBox {
public:
  bool designSpace() const override { return false; }
  ZeroTextBox(Kotonoha::Engine &engine, ZeroSession &session);

  bool handleEvent(Kotonoha::EventRef event) override;
  void update() override;

protected:
  double charactersPerSecond() const override;
  void recordDialogue(const std::string &, const std::string &) override {} // HistoryState keeps the log
  void openMenu() override { session_.OpenCommand(); }
  void openBacklog() override { session_.OpenHistory(); }
  void renderFrame(Kotonoha::RendererRef renderer) override;
  void renderSpeaker(Kotonoha::RendererRef) override {} // the name plate is a picture of the frame
  void renderDialogue(Kotonoha::RendererRef renderer) override;
  void renderIndicator(Kotonoha::RendererRef renderer) override;

private:
  void SyncPage();
  void Click();

  ZeroSession &session_;
  std::string pageKey_;
  Uint64 doneAt_ = 0;
};

// Shared choice behaviour: options come from the script, picking one advances it.
class ZeroChoice : public Kotonoha::ChoiceQuestionScene {
public:
  bool designSpace() const override { return false; }
  ZeroChoice(Kotonoha::Engine &engine, ZeroSession &session);
  void update() override;
  void onEvent(Kotonoha::EventRef event) override;

protected:
  virtual bool Wanted() const = 0;
  virtual std::vector<std::string> Labels() const = 0;
  virtual int InitialFocus() const { return -1; }

  SDL_FRect listArea() const override { return {0, 0, float(kGameWidth), float(kGameHeight)}; }
  void select(std::size_t index) override;
  void openMenu() override { session_.OpenCommand(); }
  void openBacklog() override { session_.OpenHistory(); }
  void renderPanel(Kotonoha::RendererRef) override {}
  void Focus(int index);

  ZeroSession &session_;
  int focus_ = -1; // -1: no button focused yet
  std::vector<std::string> shown_;
};

// SelectWindow: MAIN_SELECT buttons spread over the script select area.
class ZeroSelectChoice : public ZeroChoice {
public:
  using ZeroChoice::ZeroChoice;

protected:
  bool Wanted() const override;
  std::vector<std::string> Labels() const override;
  void layout() override;
  void renderOption(Kotonoha::RendererRef renderer, std::size_t index, bool focused) override;
};

// MoveState: the map with destination buttons and the focused character pose.
class ZeroMoveChoice : public ZeroChoice {
public:
  using ZeroChoice::ZeroChoice;

protected:
  bool Wanted() const override;
  std::vector<std::string> Labels() const override;
  int InitialFocus() const override { return 0; }
  void layout() override;
  void renderPanel(Kotonoha::RendererRef renderer) override;
  void renderOption(Kotonoha::RendererRef renderer, std::size_t index, bool focused) override;
};

// RouletteState (tndlslot) over the story.
class ZeroRoulette : public Kotonoha::Scene {
public:
  ZeroRoulette(Kotonoha::Engine &engine, ZeroSession &session) : Scene(engine), session_(session) {}
  bool capturesInput() const override { return session_.RouletteActive(); }
  void onEvent(Kotonoha::EventRef event) override { session_.RouletteEvent(event); }
  void render(Kotonoha::RendererRef renderer, float) override { session_.DrawRoulette(&renderer); }

private:
  ZeroSession &session_;
};

// CommandState (and its character / save / load screens) as the in-game menu.
// The "main menu" item (5) is the original return to the title.
class ZeroCommandMenu : public Kotonoha::InGameMenu {
public:
  bool designSpace() const override { return false; }
  ZeroCommandMenu(Kotonoha::Engine &engine, ZeroSession &session);

  void open() override { session_.OpenCommand(); }
  void close() override {}
  bool IsOpen() const override { return session_.ScreenActive() && !session_.HistoryActive(); }
  void onEvent(Kotonoha::EventRef event) override;
  void update() override;

protected:
  int itemCount() const override { return 0; } // CommandState draws its own buttons
  void openBacklog() override { session_.OpenHistory(); }
  void renderPanel(Kotonoha::RendererRef renderer) override { session_.DrawScreen(&renderer); }

private:
  ZeroSession &session_;
};

// HistoryState as the backlog: the entries live in the session (with voices).
class ZeroBacklog : public Kotonoha::VNBacklog {
public:
  bool designSpace() const override { return false; }
  ZeroBacklog(Kotonoha::Engine &engine, ZeroSession &session) : VNBacklog(engine), session_(session) {}

  void addEntry(const std::string &, const std::string &) override {}
  void open() override { session_.OpenHistory(); }
  void close() override {}
  bool isVisible() const override { return session_.HistoryActive(); }
  void onEvent(Kotonoha::EventRef event) override;

protected:
  void renderFrame(Kotonoha::RendererRef renderer) override { session_.DrawScreen(&renderer); }
  void renderEntries(Kotonoha::RendererRef) override {}

private:
  ZeroSession &session_;
};

} // namespace ZeroNoTsukaima
