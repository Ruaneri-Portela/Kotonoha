#pragma once

#include <Kotonoha/core/Engine.hpp>
#include <Kotonoha/core/Scene.hpp>

#include <string>
#include <vector>

namespace Kotonoha {

struct DialogueEntry {
  std::string speaker;
  std::string text;
};

// Dialogue history. Subclasses keep their own entries (addEntry/clear),
// replace the screen (open/close/isVisible) or only its drawing.
class VNBacklog : public Scene {
protected:
  std::vector<DialogueEntry> entries;
  bool isOpen = false;
  bool scrollToBottom = false;
  float scroll = 0;
  float maxScroll = 0;
  virtual void renderFrame(RendererRef renderer);
  virtual void renderEntries(RendererRef renderer);

public:
  bool designSpace() const override { return true; }
  explicit VNBacklog(Engine &engineRef);

  virtual void addEntry(const std::string &speaker, const std::string &text);
  virtual void clear();
  const std::vector<DialogueEntry> &getEntries() const { return entries; }

  virtual void open() {
    isOpen = true;
    scrollToBottom = true;
  }
  virtual void close() { isOpen = false; }
  void toggle() { isVisible() ? close() : open(); }

  virtual bool isVisible() const { return isOpen; }
  bool capturesInput() const override { return isVisible(); }
  bool pausesStory() const override { return isVisible(); }

  void onEvent(EventRef event) override;
  void update() override;
  void render(RendererRef renderer, float scale) override;
};

VNBacklog &GetVNBacklogRuntime(Engine &engineRef);

} // namespace Kotonoha