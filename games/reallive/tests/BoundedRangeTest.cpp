// Copyright 2026 Kotonoha contributors. SPDX-License-Identifier: MIT
#include <RealLive/BoundedRange.hpp>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
struct BankCursor {
  std::vector<std::string> *bank;
  std::size_t index;
  bool operator!=(const BankCursor &other) const {
    return bank != other.bank || index != other.index;
  }
  BankCursor &operator++() {
    if (index >= bank->size())
      throw std::out_of_range("End of RealLive string bank");
    ++index;
    return *this;
  }
  std::string &operator*() { return bank->at(index); }
};
void require(bool condition, const char *message) {
  if (!condition)
    throw std::runtime_error(message);
}
} // namespace

int main() {
  try {
    std::vector<std::string> strings{"zero", "one", "two", "three", "four"};
    auto positions = RealLive::collectInclusiveRange(BankCursor{&strings, 1},
                                                     BankCursor{&strings, 3});
    require(positions.size() == 3, "inclusive range must include both ends");
    for (auto &ref : positions)
      *ref = "";
    require(strings == std::vector<std::string>({"zero", "", "", "", "four"}),
            "only the requested inclusive bank range is cleared");

    auto one = RealLive::collectInclusiveRange(BankCursor{&strings, 4},
                                               BankCursor{&strings, 4});
    require(one.size() == 1, "single-element interval works");

    const auto before = strings;
    bool failed = false;
    try {
      (void)RealLive::collectInclusiveRange(BankCursor{&strings, 3},
                                            BankCursor{&strings, 0});
    } catch (const std::out_of_range &) {
      failed = true;
    }
    require(failed && strings == before,
            "reverse interval must fail without modifying the bank");

    failed = false;
    try {
      (void)RealLive::collectInclusiveRange(BankCursor{&strings, 0},
                                            BankCursor{&strings, 4}, 3);
    } catch (const std::out_of_range &) {
      failed = true;
    }
    require(failed && strings == before,
            "overlimit interval must fail without modifying the bank");

    std::cout << "RealLive bounded string range tests passed\n";
  } catch (const std::exception &e) {
    std::cerr << "RealLive range tests failed: " << e.what() << '\n';
    return 1;
  }
}
