// Copyright 2026 Kotonoha contributors. SPDX-License-Identifier: MIT
#pragma once

#include "machine/reference.h"

#include <cstddef>
#include <cstdint>
#include <limits>
#include <stdexcept>

namespace RealLive {

// Integer banks use inclusive ranges, as in rlvm's Mem.setrng and Mem.sum.
// Check both endpoints before touching the first element: iterating towards
// a reversed endpoint can otherwise mutate thousands of unrelated variables.
inline std::size_t checkedIntegerRange(const IntReferenceIterator &first,
                                       const IntReferenceIterator &last,
                                       std::size_t maxCount = 1000000) {
  if (maxCount == 0)
    throw std::out_of_range("REALLIVE empty integer range limit");
  if (first.bank() < 0 || last.bank() < 0) {
    // The VM store register is a single address, never a range of pointers.
    if (first == last)
      return 1;
    throw std::out_of_range("REALLIVE invalid store-register range");
  }
  if (first.bank() != last.bank() || first.index() < 0 ||
      last.index() < first.index())
    throw std::out_of_range("REALLIVE invalid integer bank range");

  const auto count = static_cast<std::uint64_t>(
      static_cast<std::int64_t>(last.index()) - first.index() + 1);
  if (count > maxCount)
    throw std::out_of_range("REALLIVE integer range exceeds limit");
  return static_cast<std::size_t>(count);
}

// Validate every address in a monotonic stepped sequence without generating
// intermediates. The int64 arithmetic keeps large signed VM operands safe.
inline void checkedIntegerSequence(const IntReferenceIterator &first,
                                   std::int64_t step, std::size_t count,
                                   std::size_t maxCount = 1000000) {
  if (count > maxCount)
    throw std::out_of_range("REALLIVE integer sequence exceeds limit");
  if (count == 0)
    return;
  if (first.bank() < 0) {
    if (count > 1 && step != 0)
      throw std::out_of_range("REALLIVE store-register offset");
    return;
  }
  if (first.index() < 0)
    throw std::out_of_range("REALLIVE negative integer reference");
  const auto last = static_cast<std::int64_t>(first.index()) +
                    step * static_cast<std::int64_t>(count - 1);
  if (last < 0 || last > std::numeric_limits<int>::max())
    throw std::out_of_range("REALLIVE integer sequence out of bounds");
}

inline IntReferenceIterator
checkedIntegerOffset(const IntReferenceIterator &origin, int offset) {
  checkedIntegerSequence(origin, offset, offset == 0 ? 1 : 2);
  return origin + offset;
}

}  // namespace RealLive
