// SPDX-License-Identifier: MIT
#include "RealLiveSelectRules.hpp"

#include <iostream>
#include <stdexcept>
#include <vector>

namespace {
void require(bool ok, const char *message) {
  if (!ok)
    throw std::runtime_error(message);
}
}

int main() {
  try {
    using RealLive::SelectOptionState;
    using RealLive::applySelectCondition;
    using RealLive::selectWindowIndex;
    const auto checkDefault = [] {
      const SelectOptionState state;
      require(state.visible && state.enabled && state.colourIndex == -1,
              "unconditional choices remain selectable");
    };
    checkDefault();

    SelectOptionState hidden;
    applySelectCondition(hidden, 0x32, true, true);
    require(!hidden.visible, "true OPTION_HIDE condition must hide an option");
    hidden = {};
    applySelectCondition(hidden, 0x32, true, false);
    require(hidden.visible, "false OPTION_HIDE condition must show an option");
    hidden = {};
    applySelectCondition(hidden, 0x32, false, false);
    require(!hidden.visible, "unconditional OPTION_HIDE hides an option");

    SelectOptionState title;
    applySelectCondition(title, 0x31, true, true, 5);
    require(title.visible && !title.enabled && title.colourIndex == 5,
            "OPTION_TITLE disables the entry and retains its colour");
    title = {};
    applySelectCondition(title, 0x31, true, false, 15);
    require(title.visible && title.enabled && title.colourIndex == -1,
            "false OPTION_TITLE leaves an ordinary clickable option");
    title = {};
    applySelectCondition(title, 0x31, false, false);
    require(title.visible && !title.enabled,
            "unconditional OPTION_TITLE is display-only");

    title = {};
    applySelectCondition(title, 0x31, true, true, -8);
    require(title.colourIndex == 0, "colour index clamps negative values");
    title = {};
    applySelectCondition(title, 0x31, true, true, 20000);
    require(title.colourIndex == 999, "colour index clamps oversized values");

    SelectOptionState untouched;
    for (int effect : {0x30, 0x33, 0x34, 0x7f})
      applySelectCondition(untouched, static_cast<unsigned char>(effect),
                           true, true, 2);
    require(untouched.visible && untouched.enabled &&
                untouched.colourIndex == -1,
            "other attributes do not silently alter route eligibility");
    // Conditions accumulate; an explicitly hidden option stays hidden even
    // if a later rule leaves the enabled flag unchanged.
    SelectOptionState mixed;
    applySelectCondition(mixed, 0x32, true, true);
    applySelectCondition(mixed, 0x31, true, false);
    require(!mixed.visible && mixed.enabled,
            "visibility and selectability are separate properties");

    require(selectWindowIndex(3, -1) == 3,
            "missing select_w override uses configured window");
    require(selectWindowIndex(3, 7) == 7,
            "select_w can choose a different native window");
    require(selectWindowIndex(-5, -1) == 0 &&
                selectWindowIndex(5000, -1) == 999,
            "invalid configured windows are bounded");
    require(selectWindowIndex(3, 10000) == 3,
            "invalid explicit window must not access non-existent UI");

    // Preserve source indices 0,1,2 even when 1 is hidden and 2 is disabled.
    std::vector<SelectOptionState> authored(3);
    applySelectCondition(authored[1], 0x32, true, true);
    applySelectCondition(authored[2], 0x31, true, true);
    std::vector<int> presentedIds;
    for (std::size_t index = 0; index < authored.size(); ++index)
      if (authored[index].visible)
        presentedIds.push_back(static_cast<int>(index));
    require(presentedIds == std::vector<int>({0, 2}) &&
                authored[0].enabled && !authored[2].enabled,
            "hidden/disabled entries retain original script route indices");

    std::cout << "RealLive conditional selection regressions passed\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << "RealLive selection regression: " << error.what() << '\n';
    return 1;
  }
}
