#pragma once

#include <Kotonoha/core/SDL.hpp>

#include <memory>

namespace Kotonoha {
class Engine;
class VoiceDubbingApplet;

// Player-facing dubbing window without the developer inspector or MCP tools.
class DubbingOverlay {
  Engine &engine_;
  std::unique_ptr<VoiceDubbingApplet> applet_;
  bool visible_ = false;
  float contentScale_ = 0;

public:
  explicit DubbingOverlay(Engine &engine);
  ~DubbingOverlay();
  bool handleEvent(EventRef event);
  void render(RendererRef renderer);
  bool visible() const { return visible_; }
};
} // namespace Kotonoha
