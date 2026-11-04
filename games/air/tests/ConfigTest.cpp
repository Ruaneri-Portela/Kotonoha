// Copyright 2026 Kotonoha contributors. SPDX-License-Identifier: MIT
#include "Air/Config.hpp"
#include <cassert>
#include <stdexcept>
#include <string>

int main() {
  // rlBabel stores western text as a reversible byte mapping. Accented
  // characters in the 0xC0..0xFE range are shifted down by 0x1F.
  assert(Air::decodeText("abc", 2) == "abc");
  assert(Air::decodeText(std::string("\xC4", 1), 2) == "\xC3\xA3"); // ã
  assert(Air::decodeText(std::string("\xD6", 1), 2) == "\xC3\xB5"); // õ

  // Italic glyphs are the same mapping shifted by 0x8320; decoding must be
  // recursive so 0x83E4 -> 0xC4 -> 0xE3 (ã).
  assert(Air::decodeText(std::string("\x83\xE4", 2), 2) == "\xC3\xA3");
  assert(Air::decodeText(std::string("\x83\xEB", 2), 2) == "\xC3\xAA");     // ê
  assert(Air::decodeText(std::string("\x87\x80", 2), 2) == "\xE2\x82\xAC"); // €
  assert(Air::decodeText(std::string("\x87\xC0", 2), 2) == "\xC3\xBF");     // ÿ
  assert(Air::decodeText(std::string("\x89\xBF", 2), 2) == "\xC2\xBF");     // ¿

  // CP932 name references are not western glyphs and must be rejected so the
  // translator can keep their printable expression form.
  bool threw = false;
  try {
    Air::decodeText(std::string("\x82\x60", 2), 2);
  } catch (const std::runtime_error &) {
    threw = true;
  }
  assert(threw);
  return 0;
}
