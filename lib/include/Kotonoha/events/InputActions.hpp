#pragma once
#include <Kotonoha/core/SDL.hpp>
#include <map>
#include <optional>
#include <set>
namespace Kotonoha {
class Engine;
bool HandleNavigation(Engine &, EventRef);
// Per-engine gesture state. Process every raw event once, before dispatch.
class InputActions {
  struct Finger {
    float x, y, lastX, lastY;
    bool moved = false;
  };
  std::map<std::pair<SDL_TouchID, SDL_FingerID>, Finger> fingers;
  bool multi = false, backGesture = false, backlogGesture = false,
       tapGesture = false;

public:
  void process(EventRef);
  void reset();
  bool back(EventRef) const;
  bool backlog(EventRef) const;
  bool tap() const { return tapGesture; }
};
// Converts one physical touch to a window-space pointer for mouse-based UIs.
class TouchPointer {
  std::set<std::pair<SDL_TouchID, SDL_FingerID>> fingers;
  std::optional<std::pair<SDL_TouchID, SDL_FingerID>> active;

public:
  bool translate(EventRef event, WindowRef window, SDL_Event &pointer);
  bool cancel(WindowRef window, SDL_Event &pointer);
};
// Reusable drag/wheel/keyboard scrolling for touch-aware lists and histories.
class ScrollGesture {
  bool dragging = false, touch = false;
  SDL_FingerID finger = 0;
  float lastY = 0;

public:
  bool handle(EventRef, float height, float &target, float &velocity,
              bool &followEnd, float maximum);
  bool isDragging() const { return dragging; }
  void reset() { dragging = false; }
};
} // namespace Kotonoha
