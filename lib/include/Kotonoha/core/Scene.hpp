#pragma once

#include "SDL.hpp"

#include <functional>
#include <string>
#include <vector>

namespace Kotonoha {

class Engine;
enum class RenderSpace { Novel, Screen };
enum class RenderLayer { Background, Foreground };

class Scene {
public:
  explicit Scene(Engine &engineRef) : engine(engineRef) {}
  virtual ~Scene() = default;

  virtual void onEvent(EventRef event) {}
  virtual bool handleEvent(EventRef event) {
    onEvent(event);
    return false;
  }
  virtual bool capturesInput() const { return false; }
  virtual bool pausesStory() const { return false; }
  virtual RenderSpace renderSpace() const { return RenderSpace::Novel; }
  virtual RenderLayer renderLayer() const { return RenderLayer::Foreground; }
  // Engine components are laid out in the 1280x720 design space (UI::width x
  // UI::height); the runtime fits it into the game viewport. Game scenes that
  // draw in viewport coordinates keep the default.
  virtual bool designSpace() const { return false; }
  virtual void update() {}
  virtual void render(RendererRef renderer, float scale) {}

protected:
  Engine &engine;
};
} // namespace Kotonoha