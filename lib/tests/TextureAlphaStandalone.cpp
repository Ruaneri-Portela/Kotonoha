#include <Kotonoha/media/TextureAlpha.hpp>

#include <array>
#include <cstdint>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <vector>

namespace {

void require(bool result, const char *reason) {
  if (!result)
    throw std::runtime_error(reason);
}

void transparentEdges() {
  // The fixture is a transparent 3x3 square with a single visible texel.
  std::vector<std::uint8_t> pixels(3 * 3 * 4, 0);
  const std::size_t mid = (1 * 3 + 1) * 4;
  pixels[mid] = 179;
  pixels[mid + 1] = 101;
  pixels[mid + 2] = 37;
  pixels[mid + 3] = 201;
  const auto original = pixels;
  require(Kotonoha::prepareStraightAlphaForLinearFiltering(pixels.data(), 3, 3,
                                                           12) == 8,
          "Each adjacent transparent texel gets its nearest RGB");
  for (std::size_t i = 0; i < pixels.size(); i += 4) {
    require(pixels[i] == 179 && pixels[i + 1] == 101 && pixels[i + 2] == 37,
            "No dark RGB fringe remains near visible pixels");
    require(pixels[i + 3] == original[i + 3],
            "Alpha is unchanged by edge preparation");
  }
  require(Kotonoha::prepareStraightAlphaForLinearFiltering(pixels.data(), 3, 3,
                                                           12) == 8,
          "Applying the same preparation twice is deterministic");
}

void paddedPitchAndBounds() {
  std::array<std::uint8_t, 32> pixels{};
  // Two RGBA texels separated from the next row by padding.
  pixels[0] = 42;
  pixels[1] = 177;
  pixels[2] = 206;
  pixels[3] = 255;
  pixels[11] = 0x5a;
  pixels[27] = 0xa5;
  require(
      Kotonoha::prepareStraightAlphaForLinearFiltering(pixels.data(), 2, 2,
                                                       16) == 3,
      "A padded surface must only fill its three adjacent transparent texels");
  require(pixels[11] == 0x5a && pixels[27] == 0xa5,
          "No padding byte or tail guard is modified");
  require(Kotonoha::prepareStraightAlphaForLinearFiltering(nullptr, 2, 2, 8) ==
                  0 &&
              Kotonoha::prepareStraightAlphaForLinearFiltering(pixels.data(), 0,
                                                               2, 8) == 0 &&
              Kotonoha::prepareStraightAlphaForLinearFiltering(pixels.data(), 2,
                                                               0, 8) == 0 &&
              Kotonoha::prepareStraightAlphaForLinearFiltering(pixels.data(), 2,
                                                               2, 7) == 0 &&
              Kotonoha::prepareStraightAlphaForLinearFiltering(pixels.data(), 2,
                                                               2, -1) == 0 &&
              Kotonoha::prepareStraightAlphaForLinearFiltering(
                  pixels.data(), std::numeric_limits<int>::max(), 2, 8) == 0,
          "Malformed geometry must fail before reading pixel memory");
}

} // namespace

int main() {
  try {
    transparentEdges();
    paddedPitchAndBounds();
    std::cout << "TextureAlphaStandalone PASS\n";
  } catch (const std::exception &error) {
    std::cerr << "TextureAlphaStandalone FAIL: " << error.what() << '\n';
    return 1;
  }
}
