#pragma once

#include <Kotonoha/core/Engine.hpp>
#include <Kotonoha/core/Scene.hpp>

#include <functional>
#include <string>

namespace Kotonoha {

// Title menu. Subclasses replace the items, their actions and drawing.
class GameMenu : public Scene {
protected:
  bool isOpen = false;
  int selected = 0;
  virtual int itemCount() const { return 4; }
  virtual std::string itemLabel(int index) const;
  virtual SDL_FRect itemRect(int index) const;
  virtual void activate(int index);
  virtual void renderTitle(RendererRef renderer);
  virtual void renderItem(RendererRef renderer, int index, bool focused);

  std::function<void()> onResumeCb;
  std::function<void()> onOpenBacklogCb;
  std::function<void()> onSettingsCb;
  std::function<void()> onQuitCb;

public:
  bool designSpace() const override { return true; }
  explicit GameMenu(Engine &engineRef);

  virtual void open() { isOpen = true; }
  virtual void close() { isOpen = false; }
  void toggle() { isVisible() ? close() : open(); }
  virtual bool isVisible() const { return isOpen; }

  // Callbacks para integrar com o fluxo do jogo
  void setOnResume(std::function<void()> cb) { onResumeCb = std::move(cb); }
  void setOnOpenBacklog(std::function<void()> cb) {
    onOpenBacklogCb = std::move(cb);
  }
  void setOnSettings(std::function<void()> cb) { onSettingsCb = std::move(cb); }
  void setOnQuit(std::function<void()> cb) { onQuitCb = std::move(cb); }

  void onEvent(EventRef event) override;
  void update() override;
  void render(RendererRef renderer, float scale) override;
};

GameMenu &GetGameMenuRuntime(Engine &engineRef);

} // namespace Kotonoha