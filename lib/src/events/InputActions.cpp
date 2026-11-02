#include <Kotonoha/componentes/InGameMenu.hpp>
#include <Kotonoha/componentes/VNBacklog.hpp>
#include <Kotonoha/core/Engine.hpp>
#include <Kotonoha/events/InputActions.hpp>
#include <algorithm>
#include <cmath>
namespace Kotonoha {
bool HandleNavigation(Engine &engine, EventRef event) {
  auto &runtime = engine.GetRuntime();
  if (runtime.GetState() != GameState::Gameplay)
    return false;
  for (auto *scene : runtime.GetActiveScenes())
    if (scene && scene->capturesInput())
      return false;
  auto &actions = engine.GetInputActions();
  if (actions.back(event)) {
    if (runtime.handleBackAction())
      return true;
    for (auto *scene : runtime.GetActiveScenes())
      if (auto *menu = dynamic_cast<InGameMenu *>(scene)) {
        menu->open();
        return true;
      }
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
    f.moved |= std::hypot(dx, dy) > .025f;
    if (!multi && fingers.size() == 1) {
      if (f.x < .04f && dx > .12f && dx > std::abs(dy) * 1.5f) {
        backGesture = true;
        multi = true;
      } else if (dy < -.12f && -dy > std::abs(dx) * 1.5f) {
        backlogGesture = true;
        multi = true;
      }
    }
  } else if (e.type == SDL_EVENT_FINGER_UP) {
    auto i = fingers.find(key);
    if (i == fingers.end())
      return;
    if (multi && fingers.size() == 2) {
      bool still = true;
      for (auto &[id, f] : fingers)
        still &= !f.moved;
      backGesture = still;
    }
    i->second.moved |= std::hypot(e.tfinger.x - i->second.x,
                                  e.tfinger.y - i->second.y) > .025f;
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
    dragging = touch = true;
    finger = e.tfinger.fingerID;
    lastY = e.tfinger.y * height;
    velocity = 0;
    return true;
  } else if (e.type == SDL_EVENT_FINGER_MOTION && dragging && touch &&
             finger == e.tfinger.fingerID) {
    auto y = e.tfinger.y * height;
    delta = lastY - y;
    lastY = y;
  } else if (e.type == SDL_EVENT_FINGER_UP && touch &&
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
