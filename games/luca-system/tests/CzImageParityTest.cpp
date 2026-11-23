// Copyright 2026 Kotonoha contributors. SPDX-License-Identifier: MIT
// Exact synthetic CZ3/CZ4 byte-layout regressions against LuckSystem's
// czimage/cz3.go, czimage/cz4.go and czimage/imagefix.go.
#include <LucaSystem/CzImage.hpp>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <vector>

namespace {
void check(bool ok, const char *message) {
  if (!ok)
    throw std::runtime_error(message);
}
void write16(std::vector<std::uint8_t> &d, size_t pos, unsigned value) {
  d.at(pos) = std::uint8_t(value);
  d.at(pos + 1) = std::uint8_t(value >> 8);
}
void write32(std::vector<std::uint8_t> &d, size_t pos, unsigned value) {
  for (unsigned shift = 0; shift < 32; shift += 8)
    d.at(pos + shift / 8) = std::uint8_t(value >> shift);
}
// Like LuckSystem's decompressLZW, the first code initializes the previous
// dictionary entry AND contributes its literal pixel to the output. A
// literal-only stream avoids proprietary artwork and isolates format parity.
std::vector<std::uint8_t> sampleCz(char kind, unsigned bits, unsigned width,
                                   unsigned height,
                                   const std::vector<std::uint8_t> &bytes) {
  const unsigned header = 28;
  std::vector<std::uint8_t> raw(header + 4 + 8 + bytes.size() * 2, 0);
  raw[0] = 'C';
  raw[1] = 'Z';
  raw[2] = std::uint8_t(kind);
  write32(raw, 4, header);
  write16(raw, 8, width);
  write16(raw, 10, height);
  write16(raw, 12, bits);
  raw[15] = 1; // CZ3 extended header flag
  write16(raw, 16, 7);
  write16(raw, 18, 9);
  write16(raw, 20, width);
  write16(raw, 22, height);
  write16(raw, 24, 1280);
  write16(raw, 26, 720);
  write32(raw, header, 1);
  write32(raw, header + 4, unsigned(bytes.size()));
  write32(raw, header + 8, unsigned(bytes.size()));
  for (size_t i = 0; i < bytes.size(); ++i)
    write16(raw, header + 12 + 2 * i, bytes[i]);
  return raw;
}
bool rejects(const std::vector<std::uint8_t> &bytes) {
  try {
    (void)LucaSystem::DecodeCzImage(bytes);
    return false;
  } catch (const std::runtime_error &) {
    return true;
  }
}
void test() {
  // 1x4 RGB: delta lines restart every ceil(4/3)=2 rows.
  const auto cz3 =
      sampleCz('3', 24, 1, 4, {10, 20, 30, 5, 6, 7, 1, 2, 3, 2, 3, 4});
  const auto image = LucaSystem::DecodeCzImage(cz3);
  check(image.width == 1 && image.height == 4 && image.cropX == 7 &&
            image.cropY == 9 && image.canvasWidth == 1280 &&
            image.canvasHeight == 720 &&
            image.rgba ==
                std::vector<std::uint8_t>({10, 20, 30, 255, 15, 26, 37, 255, 1,
                                           2, 3, 255, 3, 5, 7, 255}),
        "CZ3 24-bit RGB delta rows expand to opaque RGBA like LuckSystem");
  const auto cz3Rgba =
      sampleCz('3', 32, 1, 4,
               {20, 40, 60, 220, 10, 20, 30, 125, 1, 2, 3, 4, 2, 3, 4, 5});
  const auto image32 = LucaSystem::DecodeCzImage(cz3Rgba);
  check(image32.rgba == std::vector<std::uint8_t>({20, 40, 60, 220, 30, 60, 90,
                                                   89, 1, 2, 3, 4, 3, 5, 7, 9}),
        "CZ3 32-bit retains four independent delta channels");
  const auto cz4 = sampleCz(
      '4', 32, 1, 4, {10, 20, 30, 7, 8, 9, 1, 2, 3, 5, 6, 7, 200, 10, 100, 20});
  const auto image4 = LucaSystem::DecodeCzImage(cz4);
  check(image4.rgba ==
            std::vector<std::uint8_t>({10, 20, 30, 200, 17, 28, 39, 210, 1, 2,
                                       3, 100, 6, 8, 10, 120}),
        "CZ4 separate RGB and alpha planes match LuckSystem LineDiff4");
  auto truncated = cz3;
  truncated.pop_back();
  check(rejects(truncated),
        "Incomplete 24-bit CZ3 compressed payload rejected");
  check(rejects(sampleCz('4', 24, 1, 1, {20, 30, 40})),
        "CZ4 incorrectly marked as 24-bit must be rejected");
}
} // namespace
int main() {
  try {
    test();
    std::cout << "LuckSystem CZ3/CZ4 parity PASS\n";
    return 0;
  } catch (const std::exception &e) {
    std::cerr << "LuckSystem CZ3/CZ4 parity FAIL: " << e.what() << '\n';
    return 1;
  }
}
