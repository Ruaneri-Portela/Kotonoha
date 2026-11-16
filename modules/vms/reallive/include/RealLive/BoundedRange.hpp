// Copyright 2026 Kotonoha contributors. SPDX-License-Identifier: MIT
#pragma once
#include <cstddef>
#include <stdexcept>
#include <vector>

namespace RealLive {
// Validate the complete inclusive interval BEFORE writing into the script's
// string bank. In particular, reversed or malformed reference ranges must
// never partially erase a player's existing saved variables.
template <typename Iterator>
std::vector<Iterator> collectInclusiveRange(Iterator first, Iterator last,
                                            std::size_t maxEntries = 100000) {
  std::vector<Iterator> positions;
  for (std::size_t i = 0; i < maxEntries; ++i) {
    positions.push_back(first);
    if (!(first != last))
      return positions;
    ++first; // A checked iterator may throw here; no writes occurred yet.
  }
  throw std::out_of_range("REALLIVE invalid string reference range");
}
} // namespace RealLive
