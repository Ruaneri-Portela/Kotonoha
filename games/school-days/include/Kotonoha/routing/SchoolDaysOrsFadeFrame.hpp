#pragma once

#include <Kotonoha/routing/SchoolDaysOrsFadeBoundary.hpp>

#include <cstdint>
#include <optional>

namespace Kotonoha {

// Original SCHOOLDAYS HQ.exe 0x0043AA8C..0x0043AD84: the authored fade
// controller samples absolute integer script ticks (24 fps), computes an
// integer 0..255 alpha by truncation, and stops at the exclusive end tick.
// This game-only projection avoids changing the generic FadeScene, whose
// time-continuous interpolation is appropriate for other titles.
class SchoolDaysOrsFadeFrame final {
public:
  struct Sample {
    bool drawable = false;
    bool deferredNextCover = false;
    uint8_t color = 0;
    uint8_t alpha = 0;
  };

  static Sample AtTick(const Kotonoha_orsData &script,
                       Kotonoha_SceneTick atTick,
                       std::optional<Kotonoha_SceneTick> nextTick,
                       bool sceneAdvancePending) {
    const Kotonoha_orsEvent *active = nullptr;
    const Kotonoha_orsEvent *terminal = nullptr;
    for (auto *event = script.data; event != nullptr; event = event->next) {
      if ((event->command != BLACK_FADE && event->command != WHITE_FADE) ||
          !event->data.fade || !event->eventTouched) {
        continue;
      }
      if (event->startTick < event->endTick && atTick >= event->startTick &&
          atTick < event->endTick &&
          (!active || event->startTick >= active->startTick)) {
        active = event;
      }
      if (nextTick &&
          SchoolDaysOrsFadeBoundary::CoversDeferredNext(
              *event, atTick, *nextTick, sceneAdvancePending) &&
          (!terminal || event->startTick >= terminal->startTick)) {
        terminal = event;
      }
    }

    // The last native fade ends at Next and its owner is retired then.
    // v3 renders one artificial outgoing-owner frame before Advance, so
    // cover exactly that artificial frame with the authored terminal color.
    // It is not part of the original fade's [start,end) envelope.
    if (terminal) {
      return {true, true,
              static_cast<uint8_t>(terminal->command == WHITE_FADE ? 255 : 0),
              255};
    }
    if (!active)
      return {};

    const uint64_t duration =
        static_cast<uint64_t>(active->endTick - active->startTick);
    const uint64_t elapsed = static_cast<uint64_t>(atTick - active->startTick);
    const uint8_t q = static_cast<uint8_t>((elapsed * 255u) / duration);
    const uint8_t alpha =
        static_cast<uint8_t>(active->data.fade->a != 0 ? 255u - q : q);
    return {true, false,
            static_cast<uint8_t>(active->command == WHITE_FADE ? 255 : 0),
            alpha};
  }
};

} // namespace Kotonoha
