#include <Kotonoha/core/Engine.hpp>
#include <Kotonoha/events/BackAction.hpp>
namespace Kotonoha {
bool BackAction(Engine &engine, EventRef event) {
  return engine.GetInputActions().back(event);
}
} // namespace Kotonoha
