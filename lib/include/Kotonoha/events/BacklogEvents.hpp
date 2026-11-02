#pragma once
#include <Kotonoha/core/SDL.hpp>
namespace Kotonoha {
class Engine;
bool ShouldOpenBacklog(Engine &, EventRef);
// Stateless wheel/keyboard compatibility helper; widgets use ScrollGesture
// for dragging so each instance owns its gesture state.
bool HandleBacklogScrollEvent(EventRef, float &, float &, bool &, float);
} // namespace Kotonoha
