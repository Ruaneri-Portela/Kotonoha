// Copyright 2026 Kotonoha contributors. SPDX-License-Identifier: MIT
#include <RealLive/DecimalDigits.hpp>
#include <cstdint>
#include <iostream>
#include <limits>
#include <stdexcept>

static void require(bool value, const char *message) {
  if (!value)
    throw std::runtime_error(message);
}
int main() {
  try {
    using RealLive::decimalDigit;
    using RealLive::decimalDigits;
    require(decimalDigits(0) == 1 && decimalDigits(7) == 1 &&
                decimalDigits(10000) == 5 && decimalDigits(-123) == 3,
            "digits excludes sign, includes zero");
    require(decimalDigits(std::numeric_limits<std::int32_t>::min()) == 10 &&
                decimalDigit(std::numeric_limits<std::int32_t>::min(), 10) ==
                    2 &&
                decimalDigit(std::numeric_limits<std::int32_t>::min(), 1) == 8,
            "INT_MIN absolute magnitude must not overflow");
    require(decimalDigit(-12059, 1) == 9 && decimalDigit(-12059, 2) == 5 &&
                decimalDigit(12059, 4) == 2 && decimalDigit(100, 2) == 0,
            "one-based decimal index from right");
    for (int n : {0, -1, 6, std::numeric_limits<int>::max()}) {
      bool rejected = false;
      try {
        (void)decimalDigit(12345, n);
      } catch (const std::out_of_range &) {
        rejected = true;
      }
      require(rejected, "out-of-range digit indexes rejected");
    }
    std::cout << "RealLive digits/digit tests passed\n";
    return 0;
  } catch (const std::exception &e) {
    std::cerr << "RealLive digits test failed: " << e.what() << '\n';
    return 1;
  }
}
