// Copyright 2026 Kotonoha contributors. SPDX-License-Identifier: MIT
// Synthetic SEEN index bytes; no commercial game data is needed.
#include "libreallive/alldefs.h"
#include <cstdlib>
#include <iostream>
#include <string>

namespace {
void expect(const char *name, long actual, long expected) {
  if (actual != expected) {
    std::cerr << name << ": expected " << expected << ", got " << actual
              << '\n';
    std::exit(EXIT_FAILURE);
  }
}
} // namespace

int main() {
  // Offset 80000 is encoded as 80 38 01 00; with signed char the old
  // read_i32() incorrectly interpreted this value as negative.
  const char index[] = {char(0x80), char(0x38), char(0x01), char(0x00),
                        char(0xff), char(0x01), char(0x00), char(0x00)};
  expect("SEEN TOC offset", libreallive::read_i32(index), 80000);
  expect("SEEN TOC size", libreallive::read_i32(index + 4), 511);
  expect("16-bit high byte", libreallive::read_i16(index), 0x3880);
  const char negative[] = {char(0xff), char(0xff), char(0xff), char(0xff)};
  expect("negative i32", libreallive::read_i32(negative), -1);
  const char minimum[] = {char(0), char(0), char(0), char(0x80)};
  expect("minimum i32", libreallive::read_i32(minimum), -2147483647L - 1);
  const char maximum[] = {char(0xff), char(0xff), char(0xff), char(0x7f)};
  expect("maximum i32", libreallive::read_i32(maximum), 2147483647L);
  const std::string stored(index, sizeof(index));
  expect("string overload i32", libreallive::read_i32(stored, 0), 80000);
  expect("string overload i16", libreallive::read_i16(stored, 4), 511);
  std::string encoded(4, '\0');
  libreallive::insert_i32(encoded, 0, -1234567);
  expect("signed roundtrip", libreallive::read_i32(encoded, 0), -1234567);
  std::cout << "libreallive integer decoding OK\n";
}
