// Copyright 2026 Kotonoha contributors. SPDX-License-Identifier: MIT
// Retail AIR node 170:213: the "blurry AIR sprite" reported in screenshots is
// NOT a runtime sprite. It is a white garden lamp/sign drawn into the original
// BG008.G00 background art. This test locks the G00 decode so that a future
// decoder regression cannot change that shape into an actual distortion.
#include <RealLive/Images.hpp>

#include <array>
#include <cstdint>
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
  require(file.good(), "open retail G00");
  return Bytes(std::istreambuf_iterator<char>(file),
               std::istreambuf_iterator<char>());
}
} // namespace

int main(int argc, char **argv) {
  try {
    std::string data = argc > 1 ? argv[1] : "";
    if (data.empty()) {
      if (const char *env = std::getenv("REALLIVE_TEST_DATA"))
        data = env;
    }
    if (data.empty() || !std::filesystem::exists(data)) {
      std::cout << "skip - REALLIVE_TEST_DATA not provided\n";
      return 0;
    }
    const auto path = std::filesystem::path(data) / "g00" / "BG008.g00";
    const auto image = RealLive::decodeG00(readFile(path));
    require(image.width == 640 && image.height == 480 &&
                image.rgba.size() == 640u * 480u * 4u,
            "BG008.G00 decodes to the original 640x480 canvas");
    const auto at = [&](int x, int y) {
      const auto i = (std::size_t(y) * image.width + x) * 4;
      return std::array<int, 4>{image.rgba[i], image.rgba[i + 1],
                                image.rgba[i + 2], image.rgba[i + 3]};
    };
    const auto lampTop = at(215, 100);
    const auto lampSign = at(217, 220);
    const auto garden = at(450, 250);
    require(lampTop[0] > 240 && lampTop[1] > 240 && lampTop[2] > 240 &&
                lampTop[3] == 255,
            "the pale lamp head is baked into the retail background art");
    require(lampSign[0] - lampSign[2] > 60 && lampSign[3] == 255,
            "the pink sign on the lamp is baked into the retail background art");
    require(garden[1] > garden[2] && garden[3] == 255,
            "the garden pixels decode independently of the lamp");

    // Deterministic signature of the whole decoded raster. This catches a
    // changed LZ backreference/copy even if a few anchor pixels still match.
    std::uint64_t hash = 1469598103934665603ull;
    for (auto byte : image.rgba) {
      hash ^= byte;
      hash *= 1099511628211ull;
    }
    require(hash == 0x5a3816fd3b2de8e3ull,
            "BG008.G00 decodes byte-identically to the shipped AIR art");
    std::cout << "REALLIVE BG008 retail art decode PASS\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << "REALLIVE BG008 retail art decode FAIL: " << error.what()
              << '\n';
    return 1;
  }
}
