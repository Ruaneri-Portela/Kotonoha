#include <Kotonoha/core/Engine.hpp>
#include <Kotonoha/events/BacklogEvents.hpp>
#include <Kotonoha/events/InputActions.hpp>
namespace Kotonoha {
bool ShouldOpenBacklog(Engine &engine, EventRef event) {
  return engine.GetRuntime().GetState() == GameState::Gameplay &&
         engine.GetInputActions().backlog(event);
}
bool HandleBacklogScrollEvent(EventRef event, float &target, float &velocity,
                              bool &follow, float maximum) {
  if (event.type != SDL_EVENT_KEY_DOWN && event.type != SDL_EVENT_MOUSE_WHEEL)
    return false;
  ScrollGesture scroll;
  return scroll.handle(event, 375, target, velocity, follow, maximum);
}
} // namespace Kotonoha
