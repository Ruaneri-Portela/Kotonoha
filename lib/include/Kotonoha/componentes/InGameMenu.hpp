#pragma once

#include <Kotonoha/core/Scene.hpp>
#include <functional>
#include <string>

namespace Kotonoha {

// Pause menu. Subclasses replace the items, their actions and drawing, or
// the whole screen (open/close/IsOpen) with a game specific one.
class InGameMenu : public Scene {
protected:
  bool isOpen = false;
  int selected = 0;
  virtual int itemCount() const { return 6; }
  virtual std::string itemLabel(int index) const;
  virtual SDL_FRect itemRect(int index) const;
  virtual void activate(int index);
  virtual void openBacklog();
  virtual void renderPanel(RendererRef renderer);
  virtual void renderItem(RendererRef renderer, int index, bool focused);

  std::function<void()> onResumeCb;
  std::function<void()> onReturnToMenuCb;
  std::function<void()> onSettingsCb;
  std::function<void()> onSaveCb, onLoadCb;

public:
  bool designSpace() const override { return true; }
  explicit InGameMenu(Engine &engineRef);

  virtual void open() { isOpen = true; }
  virtual void close() { isOpen = false; }
  void toggle() { IsOpen() ? close() : open(); }
  virtual bool IsOpen() const { return isOpen; }
  bool capturesInput() const override { return IsOpen(); }
  bool pausesStory() const override { return IsOpen(); }

  void SetOnResume(std::function<void()> cb) { onResumeCb = std::move(cb); }
  void SetOnReturnToMenu(std::function<void()> cb) {
    onReturnToMenuCb = std::move(cb);
  }
  void SetOnSettings(std::function<void()> cb) { onSettingsCb = std::move(cb); }
  void SetOnSave(std::function<void()> cb) { onSaveCb = std::move(cb); }
  void SetOnLoad(std::function<void()> cb) { onLoadCb = std::move(cb); }

  void onEvent(EventRef event) override;
  void update() override;
  void render(RendererRef renderer, const float scale) override;
};

InGameMenu &GetInGameMenuRuntime(Engine &engineRef);

} // namespace Kotonoha