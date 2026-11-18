// Copyright 2026 Kotonoha contributors. SPDX-License-Identifier: MIT
#include "RealLive/Config.hpp"
#include <cassert>
#include <stdexcept>
#include <string>

int main() {
  // rlBabel stores western text as a reversible byte mapping. Accented
  // characters in the 0xC0..0xFE range are shifted down by 0x1F.
  assert(RealLive::decodeText("abc", 2) == "abc");
  assert(RealLive::decodeText(std::string("\xC4", 1), 2) == "\xC3\xA3"); // ã
  assert(RealLive::decodeText(std::string("\xD6", 1), 2) == "\xC3\xB5"); // õ

  // Italic glyphs are the same mapping shifted by 0x8320; decoding must be
  // recursive so 0x83E4 -> 0xC4 -> 0xE3 (ã).
  assert(RealLive::decodeText(std::string("\x83\xE4", 2), 2) == "\xC3\xA3");
  assert(RealLive::decodeText(std::string("\x83\xEB", 2), 2) ==
         "\xC3\xAA"); // ê
  assert(RealLive::decodeText(std::string("\x87\x80", 2), 2) ==
         "\xE2\x82\xAC"); // €
  assert(RealLive::decodeText(std::string("\x87\xC0", 2), 2) ==
         "\xC3\xBF"); // ÿ
  assert(RealLive::decodeText(std::string("\x89\xBF", 2), 2) ==
         "\xC2\xBF"); // ¿
  assert(RealLive::decodeText(std::string("A\x81\x68"
                                          "B",
                                          4),
                              2) ==
         "A\xE2\x80\x9D"
         "B"); // mixed rlBabel + Shift-JIS closing quote

  // CP932 name references are not western glyphs and must be rejected so the
  // translator can keep their printable expression form.
  bool threw = false;
  try {
    RealLive::decodeText(std::string("\x82\x60", 2), 2);
  } catch (const std::runtime_error &) {
    threw = true;
  }
  assert(threw);
  // Startup and title scripts differ across RealLive releases.
  const RealLive::Config frontend("#SEEN_START=9030\n"
                                  "#SEEN_MENU=9042\n"
                                  "#REGNAME=\"KEY\\CLANNAD_FV\"\n"
                                  "#SCREENSIZE_MOD=1\n");
  assert(frontend.integer("SEEN_START") == 9030);
  assert(frontend.integer("SEEN_MENU") == 9042);
  assert(frontend.text("REGNAME") == "KEY\\CLANNAD_FV");
  const auto res = frontend.detectResolution();
  assert(res.width == 800 && res.height == 600);
  const RealLive::Config vga("#SCREENSIZE_MOD=0\n");
  const auto vgaRes = vga.detectResolution();
  assert(vgaRes.width == 640 && vgaRes.height == 480);
  return 0;
}
