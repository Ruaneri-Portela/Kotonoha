// SPDX-License-Identifier: MIT
#include "RealLiveNumeric.hpp"

#include <climits>
#include <cstdlib>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <vector>

namespace {
void require(bool ok, const char *message) {
  if (!ok)
    throw std::runtime_error(message);
}
template <class Fn>
void rejects(Fn operation, const char *message) {
  try {
    operation();
  } catch (const std::exception &) {
    return;
  }
  throw std::runtime_error(message);
}
}

int main() {
  try {
    using RealLive::wrapInteger32;
    using RealLive::safeNegate;
    using RealLive::safeMod;
    using RealLive::safePercent;
    using RealLive::randomInclusive;
    require(sizeof(int) == sizeof(std::int32_t),
            "RealLive uses 32-bit signed VM integers");
    require(wrapInteger32(INT64_C(2147483648)) == INT_MIN,
            "32-bit overflow wraps deterministically");
    require(wrapInteger32(-INT64_C(2147483649)) == INT_MAX,
            "32-bit underflow wraps deterministically");
    require(safeNegate(INT_MIN) == INT_MIN,
            "negating INT_MIN is defined in 32-bit VM");
    require(safeNegate(INT_MAX) == -INT_MAX,
            "negation of ordinary positive operands");
    require(safeMod(INT_MIN, -1) == 0,
            "signed modulo special case cannot trigger division UB");
    require(safeMod(-100, 7) == -2 && safeMod(100, 0) == 0,
            "signed modulo and zero divisor behavior");
    require(safePercent(1, 4) == 25 &&
                safePercent(-2, 5) == -40 &&
                safePercent(INT_MAX, 0) == 0,
            "percentage arithmetic is reliable for common operands");
    require(safePercent(INT_MIN, -1) == 0,
            "large percentage values follow 32-bit wrapping semantics");

    int calls = 0;
    auto zero = [&] { ++calls; return 0; };
    require(randomInclusive(-3, 3, RAND_MAX, zero) == -3,
            "lower endpoint inclusive");
    require(randomInclusive(9, 9, RAND_MAX, zero) == 9,
            "unit-width interval has exact answer");
    require(calls == 1, "no random draw is used for a constant interval");
    require(randomInclusive(12, -7, RAND_MAX, zero) == -7,
            "reversed interval normalizes its endpoints");

    const int maxRandom = INT_MAX;
    require(randomInclusive(INT_MIN, INT_MAX, maxRandom,
                            [] { return 0; }) == INT_MIN,
            "full 2^32 interval includes lower endpoint");
    require(randomInclusive(INT_MIN, INT_MAX, maxRandom,
                            [] { return INT_MAX; }) == INT_MAX,
            "full 2^32 interval includes upper endpoint");
    require(randomInclusive(0, INT_MAX - 1, maxRandom,
                            [] { return INT_MAX - 1; }) == INT_MAX - 1,
            "large max-exclusive interval is representable");

    // With RNG digits 0..3, the 6-value interval uses a 16-outcome
    // two-digit sample. Values 12..15 must be rejected to prevent bias.
    const std::vector<int> digits{3, 3, 1, 2};
    std::size_t pos = 0;
    const int result = randomInclusive(10, 15, 3, [&] {
      require(pos < digits.size(), "rejection sampler exhausted test stream");
      return digits[pos++];
    });
    require(result == 10 && pos == 4,
            "unbiased rejection redraws out-of-range combinations");

    rejects([] {
      (void)randomInclusive(0, 100, 0, [] { return 0; });
    }, "RNG with one possible value must be rejected");
    rejects([] {
      (void)randomInclusive(0, 100, 4, [] { return 5; });
    }, "RNG values above declared maximum are invalid");
    std::cout << "RealLive numeric and full-range random tests passed\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << "RealLive numeric regression: " << error.what() << '\n';
    return 1;
  }
}
