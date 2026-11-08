#include "RealLive/Images.hpp"
#include <algorithm>
#include <cassert>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <vector>

namespace {
using Bytes = std::vector<std::uint8_t>;
void put16(Bytes &b, std::size_t p, std::uint16_t v) {
  b.at(p) = v;
  b.at(p + 1) = v >> 8;
}
void put32(Bytes &b, std::size_t p, std::uint32_t v) {
  for (int i = 0; i < 4; ++i)
    b.at(p + i) = v >> (i * 8);
}
Bytes pack(const Bytes &raw, std::size_t unit = 1) {
  Bytes b;
  for (std::size_t p = 0; p < raw.size();) {
    auto n = std::min<std::size_t>(8, (raw.size() - p) / unit);
    b.push_back((1u << n) - 1);
    b.insert(b.end(), raw.begin() + p, raw.begin() + p + n * unit);
    p += n * unit;
  }
  return b;
}
Bytes simple(unsigned type, unsigned w, unsigned h, const Bytes &raw) {
  Bytes b(13);
  b[0] = type;
  put16(b, 1, w);
  put16(b, 3, h);
  auto p = pack(raw, type == 0 ? 3 : 1);
  put32(b, 5, p.size() + 8);
  put32(b, 9, type == 0 ? w * h * 4 : raw.size());
  b.insert(b.end(), p.begin(), p.end());
  return b;
}
Bytes regions() { // Two overlapping patterns become distinct atlas rectangles.
  Bytes b(9 + 48 + 8);
  b[0] = 2;
  put16(b, 1, 1);
  put16(b, 3, 1);
  put32(b, 5, 2);
  put32(b, 9 + 16, 4);
  put32(b, 9 + 20, 5);
  Bytes raw(20 + 2 * (0x74 + 0x5c + 4));
  put32(raw, 0, 2);
  for (unsigned i = 0; i < 2; ++i) {
    auto start = 20 + i * (0x74 + 0x5c + 4);
    put32(raw, 4 + i * 8, start);
    put32(raw, 8 + i * 8, 0x74 + 0x5c + 4);
    auto tile = start + 0x74;
    put16(raw, tile + 6, 1);
    put16(raw, tile + 8, 1);
    raw[tile + 0x5c] = 10 + i;
    raw[tile + 0x5c + 1] = 20;
    raw[tile + 0x5c + 2] = 30;
    raw[tile + 0x5c + 3] = 40 + i;
  }
  auto compressed = pack(raw);
  put32(b, 57, compressed.size() + 8);
  put32(b, 61, raw.size());
  b.insert(b.end(), compressed.begin(), compressed.end());
  return b;
}
Bytes pdt(unsigned type, unsigned w, unsigned h, const Bytes &raw,
          const Bytes &alpha = {}) {
  Bytes b(type == 10 ? 32 : 0x460);
  b[0] = 'P';
  b[1] = 'D';
  b[2] = 'T';
  b[3] = '1';
  b[4] = type == 10 ? '0' : '1';
  put32(b, 12, w);
  put32(b, 16, h);
  if (type == 11) {
    b[32] = 1;
    b[33] = 2;
    b[34] = 3;
    b[36] = 4;
    b[37] = 5;
    b[38] = 6;
  }
  auto literals = [&](const Bytes &v, std::size_t unit) {
    for (std::size_t p = 0; p < v.size();) {
      const auto n = std::min<std::size_t>(8, (v.size() - p) / unit);
      b.push_back(static_cast<uint8_t>(0xffu << (8 - n)));
      b.insert(b.end(), v.begin() + p, v.begin() + p + n * unit);
      p += n * unit;
    }
  };
  literals(raw, type == 10 ? 3 : 1);
  if (!alpha.empty()) {
    put32(b, 28, b.size());
    literals(alpha, 1);
  }
  put32(b, 8, b.size());
  return b;
}
bool rejectsPDT(const Bytes &b) {
  try {
    RealLive::decodePDT(b);
    return false;
  } catch (const std::runtime_error &) {
    return true;
  }
}
bool rejects(const Bytes &b) {
  try {
    RealLive::decodeG00(b);
    return false;
  } catch (const std::runtime_error &) {
    return true;
  }
}
} // namespace
int main(int argc, char **argv) {
  auto p10 = pdt(10, 2, 1, {1, 2, 3, 4, 5, 6}, {9, 255});
  assert((RealLive::decodePDT(p10).rgba == Bytes{3, 2, 1, 9, 6, 5, 4, 255}));
  auto p11 = pdt(11, 2, 1, {0, 1});
  assert((RealLive::decodePDT(p11).rgba == Bytes{3, 2, 1, 255, 6, 5, 4, 255}));
  auto pMatch = pdt(10, 3, 1, {1, 2, 3, 1, 2, 3, 1, 2, 3});
  pMatch.resize(38);
  pMatch[32] = 0x80;
  pMatch[33] = 1;
  pMatch[34] = 2;
  pMatch[35] = 3;
  pMatch[36] = 1;
  pMatch[37] = 0;
  put32(pMatch, 8, pMatch.size());
  assert((RealLive::decodePDT(pMatch).rgba ==
          Bytes{3, 2, 1, 255, 3, 2, 1, 255, 3, 2, 1, 255}));
  auto p11Match = pdt(11, 3, 1, {1, 1, 1});
  p11Match.resize(0x463);
  put32(p11Match, 8, p11Match.size());
  put32(p11Match, 0x420, 1);
  p11Match[0x460] = 0x80;
  p11Match[0x461] = 1;
  p11Match[0x462] = 0;
  assert((RealLive::decodePDT(p11Match).rgba ==
          Bytes{6, 5, 4, 255, 6, 5, 4, 255, 6, 5, 4, 255}));
  for (std::size_t n = 0; n < p10.size(); ++n)
    assert(rejectsPDT(Bytes(p10.begin(), p10.begin() + n)));
  auto invalidPDT = p10;
  put32(invalidPDT, 28, 31);
  assert(rejectsPDT(invalidPDT));
  invalidPDT = pMatch;
  invalidPDT[32] = 0;
  assert(rejectsPDT(invalidPDT));
  auto rgb = RealLive::decodeG00(simple(0, 2, 1, {1, 2, 3, 4, 5, 6}));
  assert((rgb.rgba == Bytes{3, 2, 1, 255, 6, 5, 4, 255}));
  auto indexed = RealLive::decodeG00(
      simple(1, 2, 1, {2, 0, 1, 2, 3, 0, 4, 5, 6, 0, 1, 0}));
  assert((indexed.rgba == Bytes{6, 5, 4, 255, 3, 2, 1, 255}));
  auto multi = regions();
  auto atlas = RealLive::decodeG00(multi);
  assert(atlas.width == 1 && atlas.height == 2 && atlas.sourceHeight == 1);
  assert(atlas.regions[0].originX == 4 && atlas.regions[0].originY == 5);
  assert(atlas.regions[1].atlasY == 1);
  assert((atlas.rgba == Bytes{30, 20, 10, 40, 30, 20, 11, 41}));
  for (std::size_t n = 0; n < multi.size(); ++n) {
    Bytes truncated(multi.begin(), multi.begin() + n);
    assert(rejects(truncated));
  }
  // A literal followed by an overlapping RGB backreference.
  auto repeat = simple(0, 3, 1, {1, 2, 3, 1, 2, 3, 1, 2, 3});
  repeat.resize(19);
  put32(repeat, 5, 14);
  repeat[13] = 1;
  repeat[14] = 1;
  repeat[15] = 2;
  repeat[16] = 3;
  repeat[17] = 0x11;
  repeat[18] = 0;
  assert((RealLive::decodeG00(repeat).rgba ==
          Bytes{3, 2, 1, 255, 3, 2, 1, 255, 3, 2, 1, 255}));
  auto invalid = simple(0, 1, 1, {1, 2, 3});
  invalid[13] = 0;
  assert(rejects(invalid));
  auto badPalette = simple(1, 1, 1, {1, 0, 0, 0, 0, 0, 1});
  assert(rejects(badPalette));
  std::size_t count = 0;
  if (argc > 1)
    for (const auto &f : std::filesystem::directory_iterator(argv[1])) {
      if (f.path().extension() != ".g00")
        continue;
      std::ifstream stream(f.path(), std::ios::binary);
      Bytes b((std::istreambuf_iterator<char>(stream)), {});
      try {
        auto image = RealLive::decodeG00(b);
        assert(image.rgba.size() ==
               std::size_t(image.width) * image.height * 4);
        ++count;
      } catch (const std::exception &e) {
        std::cerr << f.path() << ": " << e.what() << '\n';
        return 1;
      }
    }
  if (argc > 2)
    for (const auto &f : std::filesystem::directory_iterator(argv[2])) {
      if (f.path().extension() != ".PDT")
        continue;
      std::ifstream stream(f.path(), std::ios::binary);
      Bytes b((std::istreambuf_iterator<char>(stream)), {});
      auto image = RealLive::decodePDT(b);
      assert(image.rgba.size() == std::size_t(image.width) * image.height * 4);
      ++count;
    }
  std::cout << "G00/PDT synthetic checks passed; retail images decoded: "
            << count << '\n';
}
