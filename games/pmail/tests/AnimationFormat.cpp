#include <SnowRadish/Pack.hpp>
#include <iostream>
#include <stdexcept>

namespace {
void require(bool value, const char *message) {
  if (!value)
    throw std::runtime_error(message);
}
void write32(std::vector<uint8_t> &bytes, size_t offset, uint32_t value) {
  for (int i = 0; i < 4; ++i)
    bytes.at(offset + size_t(i)) = uint8_t(value >> (8 * i));
}
std::vector<uint8_t> character(uint8_t red) {
  std::vector<uint8_t> bytes;
  for (int row = 0; row < 600; ++row) {
    if (row == 579)
      bytes.insert(bytes.end(), {0xa8, 0x7f, 1, 2, red});
    bytes.push_back(0xff);
  }
  return bytes;
}
} // namespace
int main() {
  try {
    std::vector<uint8_t> sequential(0x2084, 0);
    write32(sequential, 0, 80);
    for (uint32_t i = 0; i < 80; ++i) {
      const auto frame = character(uint8_t(i + 3));
      write32(sequential, 4 + i * 8, uint32_t(sequential.size()));
      write32(sequential, 8 + i * 8, uint32_t(frame.size()));
      sequential.insert(sequential.end(), frame.begin(), frame.end());
    }
    SnowRadish::Animation decoded;
    require(SnowRadish::decodeAnm(sequential, decoded) &&
                decoded.frames.size() == 80,
            "Sequential animations were limited to lip-region counts");
    for (uint32_t i = 0; i < 80; ++i) {
      const auto &frame = decoded.frames[i];
      require(frame.x == 10 && frame.y == 20 && frame.width == 1 &&
                  frame.height == 1,
              "CHAR frame placement or bottom-up orientation was lost");
      require(frame.rgba == std::vector<uint8_t>({uint8_t(i + 3), 2, 1, 255}),
              "CHAR frame colors or alpha differ");
    }
    std::vector<uint8_t> lips(0x684, 0);
    write32(lips, 0, 1);
    write32(lips, 4, 6);
    write32(lips, 76, 10);
    write32(lips, 80, 20);
    write32(lips, 92, 1);
    write32(lips, 96, 1);
    for (uint32_t i = 0; i < 3; ++i) {
      const auto frame = character(uint8_t(i + 30));
      write32(lips, 12 + i * 4, uint32_t(lips.size()));
      write32(lips, 44 + i * 4, uint32_t(frame.size()));
      lips.insert(lips.end(), frame.begin(), frame.end());
    }
    require(SnowRadish::decodeAnm(lips, decoded) &&
                decoded.frames.size() == 3 && decoded.regions.size() == 1 &&
                decoded.regions[0].actor == 6 &&
                decoded.frames[2].rgba[0] == 32,
            "Headerless CHAR mouth frames or actor mapping were lost");
    write32(lips, 16, uint32_t(lips.size() + 1));
    require(!SnowRadish::decodeAnm(lips, decoded) && decoded.frames.empty(),
            "Malformed mouth offset exposed partial frames");
    sequential.resize(0x2083);
    require(!SnowRadish::decodeAnm(sequential, decoded),
            "Truncated sequential table accepted");
    std::cout << "Synthetic 80-frame ANIM, headerless mouth regions, "
                 "placement/colors and malformed offsets passed.\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
