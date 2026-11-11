#include <Kotonoha/components/InGameMenu.hpp>
#include <Kotonoha/components/VNBacklog.hpp>
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
