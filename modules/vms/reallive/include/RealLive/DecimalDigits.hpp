// Copyright 2026 Kotonoha contributors. SPDX-License-Identifier: MIT
#pragma once
#include <cstdint>
#include <stdexcept>

namespace RealLive {
// RealLive Str opcodes 19 ("digits") and 20 ("digit") count decimal
// digits without a leading sign. The index for "digit" is 1-based
// from the right; invalid positions are rejected before writes.
inline std::uint32_t decimalMagnitude(std::int32_t value) {
  return value < 0 ? static_cast<std::uint32_t>(-std::int64_t(value))
                   : static_cast<std::uint32_t>(value);
}
inline int decimalDigits(std::int32_t value) {
  auto number = decimalMagnitude(value);
  int digits = 1;
  while (number >= 10) {
    number /= 10;
    ++digits;
  }
  return digits;
}
inline int decimalDigit(std::int32_t value, int fromRight) {
  const int digits = decimalDigits(value);
  if (fromRight < 1 || fromRight > digits)
    throw std::out_of_range("REALLIVE decimal digit index");
  auto number = decimalMagnitude(value);
  for (int i = 1; i < fromRight; ++i)
    number /= 10;
  return static_cast<int>(number % 10);
}
} // namespace RealLive
