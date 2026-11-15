#pragma once
#include <Kotonoha/core/SDL.hpp>
#include <map>
#include <optional>
#include <set>
#include <vector>
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
#if defined(__ANDROID__) || defined(ANDROID)
// One Android ingress adapter for all game/engine screens. Finger events are
// normalized only once into window-space mouse/key/wheel actions, then the
// regular renderer logical-presentation conversion handles all coordinates.
// Desktop input and its original hover-first semantics are left untouched.
class MobileTouchAdapter {
  using FingerKey = std::pair<SDL_TouchID, SDL_FingerID>;
  std::optional<FingerKey> primary_;
  std::set<FingerKey> fingers_;
  float startX_ = 0, startY_ = 0, lastX_ = 0, lastY_ = 0;
  float scrollRemainder_ = 0;
  Uint64 pressedAtNs_ = 0;
  bool dragged_ = false, blocked_ = false, backSent_ = false;
  bool dragControls_ = false;

public:
  std::vector<SDL_Event> translate(EventRef raw, WindowRef window,
                                   bool nativeDragControls = false);
  void reset();
};
#endif
// Reusable drag/wheel/keyboard scrolling for touch-aware lists and histories.
class ScrollGesture {
  bool dragging = false, touch = false;
  SDL_TouchID device = 0;
  SDL_FingerID finger = 0;
  float lastY = 0;

public:
  bool handle(EventRef, float height, float &target, float &velocity,
              bool &followEnd, float maximum);
  bool isDragging() const { return dragging; }
  void reset() { dragging = false; }
};
} // namespace Kotonoha
