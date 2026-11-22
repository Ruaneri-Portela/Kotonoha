// Copyright 2026 Kotonoha contributors. SPDX-License-Identifier: MIT
// Synthetic Mem opcode range checks; no game archives are needed.
#include "RealLive/IntegerRanges.hpp"

#include <climits>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
void require(bool value, const char *message) {
  if (!value) throw std::runtime_error(message);
}
template <typename F>
void rejects(F operation, const char *message) {
  bool rejected = false;
  try {
    operation();
  } catch (const std::out_of_range &) {
    rejected = true;
  }
  require(rejected, message);
}
}  // namespace

int main() {
  try {
    RLMemory memory;
    const auto address = [&](int bank, int index) {
      return IntReferenceIterator(&memory, bank, index);
    };
    require(RealLive::checkedIntegerRange(address(0, 3), address(0, 5)) == 3,
            "Mem.setrng uses inclusive endpoints");
    require(RealLive::checkedIntegerRange(address(0, 4), address(0, 4)) == 1,
            "single address is an inclusive range");
    rejects([&] {
      (void)RealLive::checkedIntegerRange(address(0, 5), address(0, 3));
    }, "reversed ranges must fail");
    rejects([&] {
      (void)RealLive::checkedIntegerRange(address(0, 3), address(1, 5));
    }, "ranges crossing integer banks must fail");
    rejects([&] {
      (void)RealLive::checkedIntegerRange(address(0, 2), address(0, 5), 3);
    }, "too large ranges must fail before writes");

    RealLive::checkedIntegerSequence(address(0, 4), 2, 3);
    RealLive::checkedIntegerSequence(address(0, 4), -2, 3);
    RealLive::checkedIntegerSequence(address(0, INT_MAX), 1, 1);
    RealLive::checkedIntegerSequence(address(0, 0), -1, 0);
    RealLive::checkedIntegerSequence(address(0, 10), 0, 4);
    rejects([&] {
      RealLive::checkedIntegerSequence(address(0, 3), -2, 3);
    }, "negative stepped destination rejected");
    rejects([&] {
      RealLive::checkedIntegerSequence(address(0, INT_MAX), 1, 2);
    }, "overflowing stepped destination rejected");
    rejects([&] {
      RealLive::checkedIntegerSequence(address(0, 1), 1, 1000001);
    }, "excessive integer sequence rejected");

    const auto source = RealLive::checkedIntegerOffset(address(0, 4), -2);
    require(source == address(0, 2), "cpyvars signed source offsets");
    rejects([&] {
      (void)RealLive::checkedIntegerOffset(address(0, 0), -1);
    }, "cpyvars source underflow rejected");
    rejects([&] {
      (void)RealLive::checkedIntegerOffset(address(0, INT_MAX), 1);
    }, "cpyvars source overflow rejected");

    int store = 17;
    IntReferenceIterator reg(&store);
    require(RealLive::checkedIntegerRange(reg, reg) == 1,
            "store register is a single reference");
    RealLive::checkedIntegerSequence(reg, 0, 3);
    rejects([&] { RealLive::checkedIntegerSequence(reg, 1, 2); },
            "store register cannot be treated as an array");
    rejects([&] {
      (void)RealLive::checkedIntegerOffset(reg, 1);
    }, "store register cannot be read with an offset");
    rejects([&] {
      (void)RealLive::checkedIntegerRange(reg, address(0, 1));
    }, "store register cannot join a different bank");

    // Rejected range validation must not write any preceding integers.
    memory.SetIntValue(libreallive::IntMemRef(0, 3), 31);
    memory.SetIntValue(libreallive::IntMemRef(0, 4), 41);
    memory.SetIntValue(libreallive::IntMemRef(0, 5), 51);
    rejects([&] {
      auto first = address(0, 5), last = address(0, 3);
      const auto n = RealLive::checkedIntegerRange(first, last);
      for (std::size_t i = 0; i < n; ++i) {
        *first = 0;
        if (i + 1 < n) ++first;
      }
    }, "reversed Mem.setrng rejected before mutation");
    require(memory.GetIntValue(libreallive::IntMemRef(0, 3)) == 31 &&
                memory.GetIntValue(libreallive::IntMemRef(0, 4)) == 41 &&
                memory.GetIntValue(libreallive::IntMemRef(0, 5)) == 51,
            "malformed range leaves the bank unchanged");

    // The source snapshot is mandatory for overlapping Mem.cpyrng.
    auto read = address(0, 3);
    auto write = address(0, 4);
    RealLive::checkedIntegerSequence(read, 1, 3);
    RealLive::checkedIntegerSequence(write, 1, 3);
    std::vector<int> values;
    for (int i = 0; i < 3; ++i) {
      values.push_back(*read);
      if (i != 2) ++read;
    }
    for (std::size_t i = 0; i < values.size(); ++i) {
      *write = values[i];
      if (i + 1 < values.size()) ++write;
    }
    require(memory.GetIntValue(libreallive::IntMemRef(0, 4)) == 31 &&
                memory.GetIntValue(libreallive::IntMemRef(0, 5)) == 41 &&
                memory.GetIntValue(libreallive::IntMemRef(0, 6)) == 51,
            "overlapping copy uses original source values");
    std::cout << "RealLive integer range regressions passed\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << "RealLive integer range regression: " << error.what() << '\n';
    return 1;
  }
}
