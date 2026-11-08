// Copyright 2026 Kotonoha contributors. SPDX-License-Identifier: MIT
// Independent memory-address adapter for libreallive's public expression API.
#pragma once
#include <ostream>
#include <stdexcept>
#include <string>
namespace libreallive {
inline constexpr int INTA_LOCATION = 0, INTB_LOCATION = 1, INTC_LOCATION = 2,
                     INTD_LOCATION = 3, INTE_LOCATION = 4, INTF_LOCATION = 5,
                     INTG_LOCATION = 6, INTZ_LOCATION = 7, INTL_LOCATION = 8;
inline constexpr int INTZ_LOCATION_IN_BYTECODE = 25,
                     INTL_LOCATION_IN_BYTECODE = 11;
inline constexpr int STRK_LOCATION = 10, STRM_LOCATION = 12, STRS_LOCATION = 18;
inline bool is_string_location(int n) { return n == 10 || n == 12 || n == 18; }
class IntMemRef {
  int bank_, type_, location_;
  static int Bank(char b) {
    if (b >= 'A' && b <= 'G')
      return b - 'A';
    if (b == 'Z')
      return 7;
    if (b == 'L')
      return 8;
    throw std::out_of_range("RealLive bank");
  }

public:
  IntMemRef(int code, int location)
      : bank_(code % 26), type_(code / 26), location_(location) {
    if (bank_ == 25)
      bank_ = 7;
    if (bank_ == 11)
      bank_ = 8;
  }
  IntMemRef(int bank, int type, int location)
      : bank_(bank), type_(type), location_(location) {}
  IntMemRef(char bank, int location)
      : bank_(Bank(bank)), type_(0), location_(location) {}
  IntMemRef(char bank, const char *access, int location)
      : IntMemRef(bank, location) {
    const std::string a(access);
    if (a == "b")
      type_ = 1;
    else if (a == "2b")
      type_ = 2;
    else if (a == "4b")
      type_ = 3;
    else if (a == "8b")
      type_ = 4;
    else if (!a.empty())
      throw std::out_of_range("RealLive access");
  }
  int bank() const { return bank_; }
  int type() const { return type_; }
  int location() const { return location_; }
};
} // namespace libreallive
inline std::ostream &operator<<(std::ostream &s,
                                const libreallive::IntMemRef &r) {
  return s << "int(" << r.bank() << "," << r.type() << ")[" << r.location()
           << "]";
}
