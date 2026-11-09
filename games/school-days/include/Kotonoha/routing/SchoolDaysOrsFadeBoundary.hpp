#pragma once

extern "C" {
#include <Kotonoha/parsers/Ors.h>
}

namespace Kotonoha {

// The retail ORS fade expires at its authored end tick (FADE_RE.md, original
// 0x0043AA8C). Kotonoha deliberately defers a completed scene's handoff until
// the following update. If an OUT fade ends on that exact Next tick, its now
// uncovered, retired background must not leak into the one extra v3 frame.
// This rule affects ONLY that deferred presentation frame; ordinary fade
// duration, intermediate frames, IN fades, and real authored gaps are intact.
class SchoolDaysOrsFadeBoundary final {
public:
  static bool CoversDeferredNext(const Kotonoha_orsEvent &fade,
                                 Kotonoha_SceneTick atTick,
                                 Kotonoha_SceneTick nextTick,
                                 bool sceneAdvancePending) {
    return sceneAdvancePending && atTick >= nextTick &&
           fade.endTick == nextTick &&
           (fade.command == BLACK_FADE || fade.command == WHITE_FADE) &&
           fade.data.fade != nullptr && fade.data.fade->a == 0;
  }
};

} // namespace Kotonoha
