// Copyright 2026 Kotonoha contributors. SPDX-License-Identifier: MIT
#pragma once
#include <cstddef>
#include <optional>

namespace RealLive {
// A case selector's empty result denotes its default branch. Evaluate all
// explicit candidates before falling back: the default may precede a match
// in compiled RealLive bytecode. The evaluator is lazy and may throw.
template <typename Evaluate>
std::optional<std::size_t> selectCase(std::size_t count, Evaluate evaluate) {
  std::optional<std::size_t> fallback;
  for (std::size_t i = 0; i < count; ++i) {
    const std::optional<bool> match = evaluate(i);
    if (!match) {
      if (!fallback)
        fallback = i;
    } else if (*match) {
      return i;
    }
  }
  return fallback;
}
} // namespace RealLive
