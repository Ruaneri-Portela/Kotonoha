// SPDX-License-Identifier: MIT
#pragma once

#include <algorithm>
#include <cstdint>
#include <optional>

namespace RealLive {

// Interpretation of conditional RealLive selection attributes. Values are
// the public libreallive::SelectElement option codes, not a copy of rlvm.
// The original option index must be retained even for hidden entries.
struct SelectOptionState {
  bool visible = true;
  bool enabled = true;
  int colourIndex = -1;
};

inline void applySelectCondition(SelectOptionState &state, std::uint8_t effect,
                                 bool hasCondition, bool conditionTrue,
                                 std::optional<int> colourArgument = {}) {
  switch (effect) {
  case 0x32: // OPTION_HIDE: do not present when the condition is true.
    if (!hasCondition || conditionTrue)
      state.visible = false;
    break;
  case 0x31: // OPTION_TITLE: show but disable when the condition is true.
    if (!hasCondition || conditionTrue) {
      state.enabled = false;
      if (colourArgument)
        state.colourIndex = std::clamp(*colourArgument, 0, 999);
    }
    break;
  default:
    // OPTION_COLOUR/BLANK/CURSOR carry game-specific display metadata;
    // never incorrectly hide or enable an option for unknown attributes.
    break;
  }
}

inline int selectWindowIndex(int configured, int explicitWindow) {
  // -1 denotes "use DEFAULT_SEL_WINDOW"; other negative values are
  // invalid and should not address a nonexistent native text window.
  if (explicitWindow >= 0 && explicitWindow <= 999)
    return explicitWindow;
  return std::clamp(configured, 0, 999);
}

} // namespace RealLive
