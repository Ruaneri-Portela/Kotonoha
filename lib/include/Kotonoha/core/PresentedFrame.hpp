#pragma once

#include <Kotonoha/core/SDL.hpp>

namespace Kotonoha {

// Snapshot of the last fully composed logical presentation.  This is a generic
// renderer primitive: game runtimes decide when a transition needs a retained
// frame and when the incoming content is ready to replace it.
class PresentedFrame {
public:
  PresentedFrame() = default;
  PresentedFrame(const PresentedFrame &) = delete;
  PresentedFrame &operator=(const PresentedFrame &) = delete;

  bool capture(RendererRef renderer);
  bool present(RendererRef renderer) const;
  void reset();
  bool available() const { return texture != nullptr; }

private:
  TexturePtr texture;
};

} // namespace Kotonoha
