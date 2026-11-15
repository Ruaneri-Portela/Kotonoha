#include <Kotonoha/componentes/InGameMenu.hpp>
#include <Kotonoha/componentes/VNBacklog.hpp>
#include <Kotonoha/core/Engine.hpp>
#include <Kotonoha/events/InputActions.hpp>
#include <algorithm>
#include <cmath>
namespace Kotonoha {
static constexpr float GESTURE_MIN_MOVE = 0.025f;
static constexpr float GESTURE_BACK_X_THRESHOLD = 0.04f;
static constexpr float GESTURE_BACK_DX_THRESHOLD = 0.12f;
static constexpr float GESTURE_BACK_ASPECT_RATIO = 1.5f;
static constexpr float GESTURE_BACKLOG_DY_THRESHOLD = -0.12f;

bool TouchPointer::cancel(WindowRef window, SDL_Event &pointer) {
  if (!active)
    return false;
  active.reset();
  pointer = {};
  pointer.type = SDL_EVENT_MOUSE_BUTTON_UP;
  pointer.button.windowID = SDL_GetWindowID(&window);
  pointer.button.which = SDL_TOUCH_MOUSEID;
  pointer.button.button = SDL_BUTTON_LEFT;
  pointer.button.x = pointer.button.y = -1;
  return true;
}

bool TouchPointer::translate(EventRef event, WindowRef window,
                             SDL_Event &pointer) {
  if (event.type == SDL_EVENT_WINDOW_FOCUS_LOST) {
    fingers.clear();
    return cancel(window, pointer);
  }
  if (event.type < SDL_EVENT_FINGER_DOWN ||
      event.type > SDL_EVENT_FINGER_CANCELED)
    return false;
  const auto id = std::make_pair(event.tfinger.touchID, event.tfinger.fingerID);
  if (event.type == SDL_EVENT_FINGER_DOWN) {
    fingers.insert(id);
    if (fingers.size() > 1) {
      return cancel(window, pointer);
    }
    active = id;
  } else if (event.type == SDL_EVENT_FINGER_UP ||
             event.type == SDL_EVENT_FINGER_CANCELED) {
    fingers.erase(id);
  }
  if (!active || *active != id)
    return false;
  int width = 0, height = 0;
  SDL_GetWindowSize(&window, &width, &height);
  pointer = {};
  if (event.type == SDL_EVENT_FINGER_MOTION) {
    pointer.type = SDL_EVENT_MOUSE_MOTION;
    pointer.motion.windowID = SDL_GetWindowID(&window);
    pointer.motion.which = SDL_TOUCH_MOUSEID;
    pointer.motion.x = event.tfinger.x * width;
    pointer.motion.y = event.tfinger.y * height;
    pointer.motion.state = SDL_BUTTON_LMASK;
  } else {
    pointer.type = event.type == SDL_EVENT_FINGER_DOWN
                       ? SDL_EVENT_MOUSE_BUTTON_DOWN
                       : SDL_EVENT_MOUSE_BUTTON_UP;
    pointer.button.windowID = SDL_GetWindowID(&window);
    pointer.button.which = SDL_TOUCH_MOUSEID;
    pointer.button.button = SDL_BUTTON_LEFT;
    pointer.button.down = event.type == SDL_EVENT_FINGER_DOWN;
    pointer.button.x = event.tfinger.x * width;
    pointer.button.y = event.tfinger.y * height;
    if (!pointer.button.down) {
      active.reset();
      if (event.type == SDL_EVENT_FINGER_CANCELED)
        pointer.button.x = pointer.button.y = -1;
    }
  }
  return true;
}
#if defined(__ANDROID__) || defined(ANDROID)
void MobileTouchAdapter::reset() {
  primary_.reset();
  fingers_.clear();
  dragged_ = blocked_ = backSent_ = false;
  scrollRemainder_ = 0;
  pressedAtNs_ = 0;
  dragControls_ = false;
}

std::vector<SDL_Event> MobileTouchAdapter::translate(EventRef raw,
                                                     WindowRef window,
                                                     bool nativeDragControls) {
  if (raw.type == SDL_EVENT_WINDOW_FOCUS_LOST) {
    reset();
    return {};
  }
  if (raw.type < SDL_EVENT_FINGER_DOWN ||
      raw.type > SDL_EVENT_FINGER_CANCELED) return {};
  int width = 0, height = 0;
  if (!SDL_GetWindowSize(&window, &width, &height) ||
      width <= 0 || height <= 0) return {};
  const auto key = FingerKey{raw.tfinger.touchID, raw.tfinger.fingerID};
  const SDL_WindowID id = SDL_GetWindowID(&window);
  std::vector<SDL_Event> out;
  const auto motion = [&](float x, float y) {
    SDL_Event mouse{};
    mouse.type = SDL_EVENT_MOUSE_MOTION;
    mouse.motion.windowID = id;
    mouse.motion.which = SDL_TOUCH_MOUSEID;
    mouse.motion.x = x * width;
    mouse.motion.y = y * height;
    return mouse;
  };
  const auto button = [&](SDL_EventType type, float x, float y) {
    SDL_Event mouse{};
    mouse.type = type;
    mouse.button.windowID = id;
    mouse.button.which = SDL_TOUCH_MOUSEID;
    mouse.button.button = SDL_BUTTON_LEFT;
    mouse.button.down = type == SDL_EVENT_MOUSE_BUTTON_DOWN;
    mouse.button.x = x * width;
    mouse.button.y = y * height;
    return mouse;
  };
  const auto escape = [&] {
    SDL_Event keyEvent{};
    keyEvent.type = SDL_EVENT_KEY_DOWN;
    keyEvent.key.windowID = id;
    keyEvent.key.key = SDLK_ESCAPE;
    keyEvent.key.repeat = false;
    out.push_back(keyEvent);
  };

  if (raw.type == SDL_EVENT_FINGER_DOWN) {
    fingers_.insert(key);
    if (fingers_.size() != 1) {
      if (dragControls_ && primary_)
        out.push_back(button(SDL_EVENT_MOUSE_BUTTON_UP, -1, -1));
      blocked_ = true;
      return out;
    }
    primary_ = key;
    dragControls_ = nativeDragControls;
    startX_ = lastX_ = raw.tfinger.x;
    startY_ = lastY_ = raw.tfinger.y;
    pressedAtNs_ = raw.tfinger.timestamp ? raw.tfinger.timestamp : SDL_GetTicksNS();
    dragged_ = blocked_ = backSent_ = false;
    scrollRemainder_ = 0;
    // Hover before activation is essential for CMAP controls. It changes no
    // selection semantics on PC and causes no premature activation on touch.
    out.push_back(motion(lastX_, lastY_));
    // The engine Settings sliders capture on press and track MOUSE_MOTION
    // while pressed. Retail CMAP panels must NOT receive this early press.
    if (dragControls_)
      out.push_back(button(SDL_EVENT_MOUSE_BUTTON_DOWN, lastX_, lastY_));
    return out;
  }
  if (!primary_ || *primary_ != key) {
    if (raw.type == SDL_EVENT_FINGER_UP ||
        raw.type == SDL_EVENT_FINGER_CANCELED) {
      fingers_.erase(key);
      if (fingers_.empty()) reset();
    }
    return out;
  }
  const float x = raw.tfinger.x, y = raw.tfinger.y;
  const float dx = x - startX_, dy = y - startY_;
  if (std::hypot(dx, dy) > 0.03f) dragged_ = true;
  if (raw.type == SDL_EVENT_FINGER_MOTION) {
    if (!blocked_) {
      if (!backSent_ && startX_ < .055f && dx > .14f &&
          dx > std::abs(dy) * 1.5f) {
        if (dragControls_)
          out.push_back(button(SDL_EVENT_MOUSE_BUTTON_UP, -1, -1));
        escape();
        backSent_ = blocked_ = true;
      } else if (dragged_) {
        if (dragControls_) out.push_back(motion(x, y));
        // One tick per 5.5% of screen height; the runtime can page its native
        // lists without guessing where tiny desktop scroll arrows are.
        // Horizontal gestures are slider drags and must not scroll the page.
        if (!dragControls_ || std::abs(dy) > std::abs(dx) * 1.25f)
          scrollRemainder_ += lastY_ - y;
        // Android content follows the finger: swipe UP means scroll DOWN,
        // which SDL expresses as a negative wheel delta. Emit multiple ticks
        // for a fast drag rather than dropping all but one per MOTION event.
        for (int step = 0; step < 8 && std::abs(scrollRemainder_) >= .055f;
             ++step) {
          const float travelSign = scrollRemainder_ > 0 ? 1.0f : -1.0f;
          SDL_Event wheel{};
          wheel.type = SDL_EVENT_MOUSE_WHEEL;
          wheel.wheel.windowID = id;
          wheel.wheel.which = SDL_TOUCH_MOUSEID;
          wheel.wheel.direction = SDL_MOUSEWHEEL_NORMAL;
          wheel.wheel.y = -travelSign;
          wheel.wheel.integer_y = travelSign > 0 ? -1 : 1;
          wheel.wheel.mouse_x = x * width;
          wheel.wheel.mouse_y = y * height;
          out.push_back(wheel);
          scrollRemainder_ -= travelSign * .055f;
        }
      } else {
        out.push_back(motion(x, y));
      }
    }
    lastX_ = x;
    lastY_ = y;
    return out;
  }
  fingers_.erase(key);
  if (raw.type == SDL_EVENT_FINGER_UP && !blocked_) {
    const Uint64 now = raw.tfinger.timestamp ? raw.tfinger.timestamp : SDL_GetTicksNS();
    if (dragControls_) {
      const bool held = pressedAtNs_ && now >= pressedAtNs_ &&
                        now - pressedAtNs_ >= Uint64{600000000};
      // End captured sliders; suppress hit activation on dragged/long-pressed
      // rows by releasing outside the native menu's authored hit regions.
      out.push_back(button(SDL_EVENT_MOUSE_BUTTON_UP,
                           dragged_ || held ? -1 : x,
                           dragged_ || held ? -1 : y));
      if (!dragged_ && held)
        escape();
    } else
    if (!dragged_) {
      if (pressedAtNs_ && now >= pressedAtNs_ &&
          now - pressedAtNs_ >= Uint64{600000000}) {
        escape(); // long press is Android's right-click/Menu equivalent
      } else {
        out.push_back(motion(x, y));
        out.push_back(button(SDL_EVENT_MOUSE_BUTTON_DOWN, x, y));
        out.push_back(button(SDL_EVENT_MOUSE_BUTTON_UP, x, y));
      }
    }
  }
  if (fingers_.empty()) reset();
  else primary_.reset();
  return out;
}
#endif

bool HandleNavigation(Engine &engine, EventRef event) {
  auto &runtime = engine.GetRuntime();
  if (runtime.GetState() != GameState::Gameplay)
    return false;
  for (auto *scene : runtime.GetActiveScenes())
    if (scene && scene->capturesInput())
      return false;
  auto &actions = engine.GetInputActions();
  if (event.type == SDL_EVENT_KEY_DOWN && !event.key.repeat &&
      event.key.key == SDLK_ESCAPE) {
    if (runtime.handleBackAction())
      return true;
    for (auto *scene : runtime.GetActiveScenes())
      if (auto *menu = dynamic_cast<InGameMenu *>(scene)) {
        menu->open();
        return true;
      }
  } else if (actions.back(event)) {
    if (runtime.handleBackAction())
      return true;
  }
  if (actions.backlog(event)) {
    if (runtime.handleBacklogAction())
      return true;
    for (auto *scene : runtime.GetActiveScenes())
      if (auto *log = dynamic_cast<VNBacklog *>(scene)) {
        log->open();
        return true;
      }
  }
  return false;
}

void InputActions::reset() {
  fingers.clear();
  multi = false;
  backGesture = backlogGesture = tapGesture = false;
}
void InputActions::process(EventRef e) {
  backGesture = backlogGesture = tapGesture = false;
  if (e.type == SDL_EVENT_WINDOW_FOCUS_LOST) {
    reset();
    return;
  }
  if (e.type < SDL_EVENT_FINGER_DOWN || e.type > SDL_EVENT_FINGER_CANCELED)
    return;
  if (e.type == SDL_EVENT_FINGER_CANCELED) {
    reset();
    return;
  }
  auto key = std::make_pair(e.tfinger.touchID, e.tfinger.fingerID);
  if (e.type == SDL_EVENT_FINGER_DOWN) {
    fingers[key] = {e.tfinger.x, e.tfinger.y, e.tfinger.x, e.tfinger.y};
    if (fingers.size() > 1)
      multi = true;
  } else if (e.type == SDL_EVENT_FINGER_MOTION) {
    auto i = fingers.find(key);
    if (i == fingers.end())
      return;
    auto &f = i->second;
    f.lastX = e.tfinger.x;
    f.lastY = e.tfinger.y;
    float dx = f.lastX - f.x, dy = f.lastY - f.y;
    f.moved |= std::hypot(dx, dy) > GESTURE_MIN_MOVE;
    if (!multi && fingers.size() == 1) {
      if (f.x < GESTURE_BACK_X_THRESHOLD && dx > GESTURE_BACK_DX_THRESHOLD &&
          dx > std::abs(dy) * GESTURE_BACK_ASPECT_RATIO) {
        backGesture = true;
        multi = true;
      } else if (dy < GESTURE_BACKLOG_DY_THRESHOLD &&
                 -dy > std::abs(dx) * GESTURE_BACK_ASPECT_RATIO) {
        backlogGesture = true;
        multi = true;
      }
    }
  } else if (e.type == SDL_EVENT_FINGER_UP) {
    auto i = fingers.find(key);
    if (i == fingers.end())
      return;
    i->second.moved |= std::hypot(e.tfinger.x - i->second.x,
                                  e.tfinger.y - i->second.y) > GESTURE_MIN_MOVE;
    if (multi && fingers.size() == 2) {
      bool still = true;
      for (auto &[id, f] : fingers)
        still &= !f.moved;
      backGesture = still;
    }
    tapGesture = !multi && !i->second.moved && fingers.size() == 1;
    fingers.erase(i);
    if (fingers.empty())
      multi = false;
  }
}
bool InputActions::back(EventRef e) const {
  return (e.type == SDL_EVENT_KEY_DOWN && !e.key.repeat &&
          (e.key.key == SDLK_ESCAPE || e.key.key == SDLK_AC_BACK)) ||
         (e.type == SDL_EVENT_MOUSE_BUTTON_DOWN &&
          e.button.button == SDL_BUTTON_RIGHT) ||
         ((e.type >= SDL_EVENT_FINGER_DOWN &&
           e.type <= SDL_EVENT_FINGER_CANCELED) &&
          backGesture);
}
bool InputActions::backlog(EventRef e) const {
  return (e.type == SDL_EVENT_KEY_DOWN && !e.key.repeat &&
          e.key.key == SDLK_B) ||
         (e.type == SDL_EVENT_MOUSE_WHEEL &&
          e.wheel.y * (e.wheel.direction == SDL_MOUSEWHEEL_FLIPPED ? -1 : 1) >
              0) ||
         ((e.type >= SDL_EVENT_FINGER_DOWN &&
           e.type <= SDL_EVENT_FINGER_CANCELED) &&
          backlogGesture);
}
bool ScrollGesture::handle(EventRef e, float height, float &target,
                           float &velocity, bool &follow, float maximum) {
  maximum = std::max(0.f, maximum);
  float delta = 0;
  if (e.type == SDL_EVENT_MOUSE_WHEEL)
    delta = -e.wheel.y *
            (e.wheel.direction == SDL_MOUSEWHEEL_FLIPPED ? -1.f : 1.f) * 30;
  else if (e.type == SDL_EVENT_KEY_DOWN && !e.key.repeat) {
    switch (e.key.key) {
    case SDLK_UP:
      delta = -50;
      break;
    case SDLK_DOWN:
      delta = 50;
      break;
    case SDLK_PAGEUP:
      delta = -height * .8f;
      break;
    case SDLK_PAGEDOWN:
      delta = height * .8f;
      break;
    case SDLK_HOME:
      delta = -maximum;
      break;
    case SDLK_END:
      delta = maximum;
      break;
    default:
      return false;
    }
  } else if (e.type == SDL_EVENT_FINGER_DOWN) {
    if (dragging && touch &&
        (device != e.tfinger.touchID || finger != e.tfinger.fingerID)) {
      reset();
      return true;
    }
    dragging = touch = true;
    device = e.tfinger.touchID;
    finger = e.tfinger.fingerID;
    lastY = e.tfinger.y * height;
    velocity = 0;
    return true;
  } else if (e.type == SDL_EVENT_FINGER_MOTION && dragging && touch &&
             device == e.tfinger.touchID && finger == e.tfinger.fingerID) {
    auto y = e.tfinger.y * height;
    delta = lastY - y;
    lastY = y;
  } else if ((e.type == SDL_EVENT_FINGER_UP ||
              e.type == SDL_EVENT_FINGER_CANCELED) &&
             touch && device == e.tfinger.touchID &&
             finger == e.tfinger.fingerID) {
    dragging = false;
    return true;
  } else if (e.type == SDL_EVENT_MOUSE_BUTTON_DOWN &&
             e.button.button == SDL_BUTTON_LEFT) {
    dragging = true;
    touch = false;
    lastY = e.button.y;
    velocity = 0;
    return false;
  } else if (e.type == SDL_EVENT_MOUSE_MOTION && dragging && !touch) {
    delta = lastY - e.motion.y;
    lastY = e.motion.y;
  } else if (e.type == SDL_EVENT_MOUSE_BUTTON_UP && !touch) {
    dragging = false;
    return false;
  } else if (e.type == SDL_EVENT_WINDOW_FOCUS_LOST) {
    reset();
    return false;
  } else
    return false;
  follow = false;
  target = std::clamp(target + delta, 0.f, maximum);
  velocity = 0;
  return true;
}
} // namespace Kotonoha
