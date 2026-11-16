// Copyright 2026 Kotonoha contributors. SPDX-License-Identifier: MIT
#include <RealLive/CaseSelection.hpp>
#include <cassert>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <vector>

int main() {
  const auto select = [](const std::vector<std::optional<int>> &cases,
                         int value) {
    return RealLive::selectCase(cases.size(),
                                [&](std::size_t i) -> std::optional<bool> {
                                  if (!cases[i])
                                    return std::nullopt;
                                  return *cases[i] == value;
                                });
  };
  const std::vector<std::optional<int>> firstDefault = {std::nullopt, 1, 2,
                                                        std::nullopt, 3};
  assert(select(firstDefault, 2) == 2);
  assert(select(firstDefault, 99) == 0);
  assert(select(firstDefault, 1) == 1);
  assert(select({3, std::nullopt, 3}, 3) == 0);
  assert(select({1, 2}, 2) == 1);
  assert(!select({1, 2}, 99));
  assert(!select({}, 7));

  unsigned inspected = 0;
  const auto match =
      RealLive::selectCase(5, [&](std::size_t index) -> std::optional<bool> {
        ++inspected;
        if (index > 2)
          throw std::runtime_error("evaluated after match");
        return index == 2;
      });
  assert(match && *match == 2 && inspected == 3);
  std::cout << "RealLive case dispatch tests passed\n";
}
