#include <ZeroNoTsukaima/Vag.hpp>

#include <algorithm>

namespace ZeroNoTsukaima {

std::vector<int16_t> DecodeVag(const uint8_t *data, size_t size) {
  static const int kCoef[5][2] = {{0, 0}, {60, 0}, {115, -52}, {98, -55}, {122, -60}};
  std::vector<int16_t> out;
  out.reserve(size / 16 * 28);
  int h1 = 0, h2 = 0;
  for (size_t i = 0; i + 16 <= size; i += 16) {
    const int shift = data[i] & 15;
    const int pred = std::min(data[i] >> 4, 4);
    const int flags = data[i + 1];
    for (int j = 0; j < 14; ++j) {
      for (int n = 0; n < 2; ++n) {
        int v = n ? data[i + 2 + j] >> 4 : data[i + 2 + j] & 15;
        if (v > 7) v -= 16;
        int s = (v << (12 - shift)) + ((h1 * kCoef[pred][0] + h2 * kCoef[pred][1]) >> 6);
        s = std::clamp(s, -32768, 32767);
        h2 = h1;
        h1 = s;
        out.push_back(static_cast<int16_t>(s));
      }
    }
    if (flags == 1 || flags == 7) break;
  }
  return out;
}

} // namespace ZeroNoTsukaima
