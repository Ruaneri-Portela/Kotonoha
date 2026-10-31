#pragma once

#include "SnowSession.hpp"

#include <Kotonoha/componentes/ChoiceQuestionScene.hpp>
#include <Kotonoha/componentes/GameMenu.hpp>
#include <Kotonoha/componentes/InGameMenu.hpp>
#include <Kotonoha/componentes/VNBacklog.hpp>
#include <Kotonoha/componentes/VNTextBox.hpp>
#include <Kotonoha/core/Scene.hpp>

#include <functional>
#include <string>
#include <vector>

// The engine components specialised for Snow Radish Vacation!!. Each keeps the
// engine behaviour (input routing, story pause, state changes) and overrides
// only what differs: what is drawn, where it is hit and what an item does.
// Every scene draws and receives events in 800x600 game units (SnowRuntime).
namespace SnowRadish {

// Cross-component actions wired by SnowRuntime (it owns every scene).
struct SnowLinks {
  std::function<void()> openMenu, openBacklog, openLoad;
  std::function<void(const std::string &, const std::string &)> record;
};

// Logo, white flash and title (T00x.IMG) behind the GameMenu contract.
// activate(0) starts the story through GameMenu (Gameplay state).
class SnowTitleMenu : public Kotonoha::GameMenu {
public:
  bool designSpace() const override { return false; }
  SnowTitleMenu(Kotonoha::Engine &engine, SnowSession &session, SnowLinks &links);

  bool isVisible() const override;
  void onEvent(Kotonoha::EventRef event) override;
  void update() override;

protected:
  int itemCount() const override { return 0; } // the title picture has its own items
  void activate(int index) override;
  void renderTitle(Kotonoha::RendererRef renderer) override;

private:
  SnowSession &session_;
  SnowLinks &links_;
};

// Background, sprites with tint and lip sync, uniform select and wipes.
class SnowStage : public Kotonoha::Scene {
public:
  SnowStage(Kotonoha::Engine &engine, SnowSession &session) : Scene(engine), session_(session) {}
  void render(Kotonoha::RendererRef renderer, float scale) override;

private:
  SnowSession &session_;
};

// WAKUH message window: instant pages, voice replay / auto buttons, auto mode and Ctrl skip.
class SnowTextBox : public Kotonoha::VNTextBox {
public:
  bool designSpace() const override { return false; }
  SnowTextBox(Kotonoha::Engine &engine, SnowSession &session, SnowLinks &links);

  bool handleEvent(Kotonoha::EventRef event) override;
  void update() override;

protected:
  double charactersPerSecond() const override { return 1e9; }
  void recordDialogue(const std::string &speaker, const std::string &text) override;
  void openMenu() override { links_.openMenu(); }
  void openBacklog() override { links_.openBacklog(); }
  void renderFrame(Kotonoha::RendererRef renderer) override;
  void renderSpeaker(Kotonoha::RendererRef) override {} // drawn with the dialogue
  void renderDialogue(Kotonoha::RendererRef renderer) override;
  void renderIndicator(Kotonoha::RendererRef renderer) override;

private:
  void SyncPage();

  SnowSession &session_;
  SnowLinks &links_;
  std::string pageKey_;
  Uint64 doneAt_ = 0;
};

// Shared choice behaviour: options come from the script, picking one advances it.
class SnowChoice : public Kotonoha::ChoiceQuestionScene {
public:
  bool designSpace() const override { return false; }
  SnowChoice(Kotonoha::Engine &engine, SnowSession &session, SnowLinks &links, SnowSession::ChoiceKind kind);
  void update() override;
  void onEvent(Kotonoha::EventRef event) override;

protected:
  SDL_FRect listArea() const override { return {0, 0, float(kGameWidth), float(kGameHeight)}; }
  void select(std::size_t index) override;
  void openMenu() override { links_.openMenu(); }
  void openBacklog() override { links_.openBacklog(); }
  void renderPanel(Kotonoha::RendererRef) override {}
  void renderOption(Kotonoha::RendererRef, std::size_t, bool) override {}

  // Keyboard / pointer handling of each choice screen; -1 = no pick.
  virtual int Hit(float x, float y);
  virtual bool Blocked() const { return false; }
  virtual bool Key(SDL_Keycode key);
  void Focus(int index);
  int Focused() const { return session_.Focus(); }

  SnowSession &session_;
  SnowLinks &links_;

private:
  SnowSession::ChoiceKind kind_;
  std::vector<std::string> shown_;
};

// Text choices: SELH strips centred on the scene; a-i focus, Up/Down, Enter.
class SnowTextChoice : public SnowChoice {
public:
  SnowTextChoice(Kotonoha::Engine &engine, SnowSession &session, SnowLinks &links)
      : SnowChoice(engine, session, links, SnowSession::ChoiceKind::Text) {}

protected:
  void layout() override;
  bool Key(SDL_Keycode key) override;
  void renderOption(Kotonoha::RendererRef renderer, std::size_t index, bool focused) override;
};

// The map: destination hotspots, clock, character badges and a preview.
class SnowMapChoice : public SnowChoice {
public:
  SnowMapChoice(Kotonoha::Engine &engine, SnowSession &session, SnowLinks &links)
      : SnowChoice(engine, session, links, SnowSession::ChoiceKind::Map) {}

protected:
  void layout() override;
  void select(std::size_t index) override;
  void renderPanel(Kotonoha::RendererRef renderer) override { session_.DrawMap(&renderer); }
};

// Uniform select: the stage draws it; the hit test uses the STSEL alpha.
class SnowUniformChoice : public SnowChoice {
public:
  SnowUniformChoice(Kotonoha::Engine &engine, SnowSession &session, SnowLinks &links)
      : SnowChoice(engine, session, links, SnowSession::ChoiceKind::Uniform) {}

protected:
  void layout() override {}
  int Hit(float x, float y) override { return session_.UniformHit(x, y); }
  bool Blocked() const override { return session_.Fading(); }
};

// Pause menu drawn in game units with the CJK font. Item 5 returns to the title,
// which is also where the end of the story leaves.
class SnowPauseMenu : public Kotonoha::InGameMenu {
public:
  bool designSpace() const override { return false; }
  SnowPauseMenu(Kotonoha::Engine &engine, SnowSession &session, SnowLinks &links);
  void onEvent(Kotonoha::EventRef event) override;
  void update() override;

protected:
  std::string itemLabel(int index) const override;
  SDL_FRect itemRect(int index) const override;
  void openBacklog() override { links_.openBacklog(); }
  void renderPanel(Kotonoha::RendererRef renderer) override;
  void renderItem(Kotonoha::RendererRef renderer, int index, bool focused) override;

private:
  SnowSession &session_;
  SnowLinks &links_;
};

// Dialogue history in game units with the CJK font (engine UI has no kana).
class SnowBacklog : public Kotonoha::VNBacklog {
public:
  bool designSpace() const override { return false; }
  SnowBacklog(Kotonoha::Engine &engine, SnowSession &session) : VNBacklog(engine), session_(session) {}
  void onEvent(Kotonoha::EventRef event) override;

protected:
  void renderFrame(Kotonoha::RendererRef renderer) override;
  void renderEntries(Kotonoha::RendererRef renderer) override;

private:
  std::vector<std::string> Wrap(SDL_Renderer *r, const std::string &text, float width);

  SnowSession &session_;
};

} // namespace SnowRadish
