#pragma once
#include <Kotonoha/componentes/TouchActivation.hpp>

#include <Kotonoha/core/Clock.hpp>
#include <Kotonoha/core/Engine.hpp>
#include <Kotonoha/core/Scene.hpp>

#include <functional>
#include <string>
#include <vector>

namespace Kotonoha {

struct DialogueEntry {
  std::string speaker;
  std::string text;
  std::string voice;
};

// Dialogue history. Subclasses keep their own entries (addEntry/clear),
// replace the screen (open/close/isVisible) or only its drawing.
class VNBacklog : public Scene {
protected:
  TouchActivation touchActivation;
  std::vector<DialogueEntry> entries;
  bool isOpen = false;
  bool scrollToBottom = false;
  float scroll = 0;
  float targetScroll = 0;
  float maxScroll = 0;

  bool isDragging = false;
  SDL_FingerID dragTouchId = 0;
  float dragStartY = 0;
  float dragStartScroll = 0;
  float lastDragY = 0;
  float scrollVelocity = 0;
  Clock clock;
  ScrollGesture scrollGesture;
  std::function<void(const std::string &)> voiceReplay;
  virtual SDL_FRect closeButtonArea() const { return {980, 100, 160, 44}; }
  virtual float scrollInputHeight() const { return 720; }

  virtual void renderFrame(RendererRef renderer);
  virtual void renderEntries(RendererRef renderer);

public:
  bool designSpace() const override { return true; }
  RenderLayer renderLayer() const override { return RenderLayer::Overlay; }
  explicit VNBacklog(Engine &engineRef);

  virtual void addEntry(const std::string &speaker, const std::string &text);
  virtual void clear();
  void setLastVoice(std::string id) {
    if (!entries.empty())
      entries.back().voice = std::move(id);
  }
  void setVoiceReplay(std::function<void(const std::string &)> callback) {
    voiceReplay = std::move(callback);
  }
  bool replayEntry(std::size_t index) {
    if (index >= entries.size() || entries[index].voice.empty() || !voiceReplay)
      return false;
    voiceReplay(entries[index].voice);
    return true;
  }
  const std::vector<DialogueEntry> &getEntries() const { return entries; }

  virtual void open() {
    touchActivation.reset();
    isOpen = true;
    scrollToBottom = true;
    isDragging = false;
    scrollGesture.reset();
    scrollVelocity = 0;
    clock.reset();
  }
  virtual void close() {
    isOpen = false;
    isDragging = false;
    scrollGesture.reset();
    scrollVelocity = 0;
  }
  void toggle() { isVisible() ? close() : open(); }

  virtual bool isVisible() const { return isOpen; }
  bool capturesInput() const override { return isVisible(); }
  bool pausesStory() const override { return isVisible(); }

  bool onEvent(EventRef event) override;
  void update() override;
  void render(RendererRef renderer, float scale) override;
};

VNBacklog &GetVNBacklogRuntime(Engine &engineRef);

} // namespace Kotonoha
