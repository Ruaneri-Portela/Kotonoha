// Copyright 2026 Kotonoha contributors. SPDX-License-Identifier: MIT
// Regression guards for the three AIR/CLANNAD sprite-frame fixes:
//   - Key logo clip rect (1:81:1034 x,y,w,h, not x1,y1,x2,y2)
//   - SMW00C active toggle frames (group + 4 is the red "on" state)
//   - CLANNAD KCUR00 single-region animation strip (CONT=50, SIZE=28x28)
// Retail paths are supplied by REALLIVE_TEST_DATA / CLANNAD_TEST_DATA.
#include <RealLive/Images.hpp>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
using Bytes = std::vector<std::uint8_t>;
void require(bool ok, const char *message) {
  if (!ok)
    throw std::runtime_error(message);
}
Bytes readFile(const std::filesystem::path &path) {
  std::ifstream file(path, std::ios::binary);
  require(file.good(), ("open " + path.string()).c_str());
  return Bytes(std::istreambuf_iterator<char>(file),
               std::istreambuf_iterator<char>());
}
} // namespace

int main() {
  try {
    const char *airEnv = std::getenv("REALLIVE_TEST_DATA");
    const char *clannadEnv = std::getenv("CLANNAD_TEST_DATA");
    if (!airEnv || !*airEnv || !clannadEnv || !*clannadEnv) {
      std::cerr << "[SKIP] REALLIVE_TEST_DATA/CLANNAD_TEST_DATA not provided\n";
      return 77;
    }
    const std::filesystem::path air(airEnv), clannad(clannadEnv);
    if (!std::filesystem::exists(air / "g00/KEYLOGO0.g00") ||
        !std::filesystem::exists(clannad / "G00/KCUR00.g00")) {
      std::cerr << "[SKIP] retail sprite atlases not present\n";
      return 77;
    }

    const auto logo = RealLive::decodeG00(readFile(air / "g00/KEYLOGO0.g00"));
    require(logo.width == 640 && logo.height == 480,
            "KEYLOGO0 is a 640x480 atlas");
    require(logo.regions.size() == 1 && logo.regions[0].x1 == 0 &&
                logo.regions[0].y1 == 0 && logo.regions[0].x2 == 639 &&
                logo.regions[0].y2 == 479,
            "KEYLOGO0 has one full-surface region: 1:81:1034 must select a "
            "160x120 tile with x,y,w,h, not inclusive x2/y2");

    const auto buttons = RealLive::decodeG00(readFile(air / "g00/SMW00C.g00"));
    require(buttons.regions.size() >= 117,
            "SMW00C carries the button state regions");
    const auto &redAuto = buttons.regions[116];
    const auto &autoBase = buttons.regions[112];
    require(redAuto.x2 >= redAuto.x1 && redAuto.y2 >= redAuto.y1 &&
                redAuto.atlasX == autoBase.atlasX,
            "SMW00C region 116 (group 112 + 4) is the red auto-on frame");
    const auto &redSkip = buttons.regions[108];
    const auto &skipBase = buttons.regions[104];
    require(redSkip.x2 >= redSkip.x1 && redSkip.y2 >= redSkip.y1 &&
                redSkip.atlasX == skipBase.atlasX,
            "SMW00C region 108 (group 104 + 4) is the red skip-on frame");

    const auto cursor =
        RealLive::decodeG00(readFile(clannad / "G00/KCUR00.g00"));
    require(cursor.width == 1400 && cursor.height == 28 &&
                cursor.regions.size() == 1,
            "CLANNAD KCUR00 is one 1400x28 strip, not 50 G00 regions");
    const auto &strip = cursor.regions[0];
    require(strip.x1 == 0 && strip.x2 == 1399 && strip.y1 == 0 &&
                strip.y2 == 27,
            "The strip spans the whole surface: frame count comes from "
            "CURSOR.000.CONT (50) and SIZE (28x28), not regions.size()");

    std::cout << "REALLIVE sprite atlas regression PASS\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << "REALLIVE sprite atlas regression FAIL: " << error.what()
              << '\n';
    return 1;
  }
}
