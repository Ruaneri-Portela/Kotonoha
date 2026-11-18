#include <SummerRadish/Transition.hpp>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <stdexcept>
namespace SummerRadish {
int wipeDuration(int type, int speed) {
  if (speed < 0 || speed > 1000)
    throw std::invalid_argument("WipeSpeed must be 0..1000");
  if (type == 32768)
    return 4000;
  int base = type == 1                             ? 300
             : type == 2 || type == 3 || type == 4 ? 1000
             : type == 5                           ? 3000
             : (type >= 0x100 && type < 0x400)     ? 1000
             : (type >= 0x400 && type < 0x700)     ? 3000
                                                   : 0;
  return base * (1000 - speed) / 500;
}
std::string wipeMaskAsset(int type) {
  if (type < 0x100 || type >= 0x700)
    return {};
  const int norm = (type >= 0x400) ? (type - 0x300) : type;
  const int maskId = norm & 0xff;
  if (maskId <= 0)
    return {};
  char path[64]{};
  std::snprintf(path, sizeof(path), "GRAPHIC/ALPMASK/%04d.ALP", maskId);
  return path;
}
Image wipeFrame(const Image &from, const Image &to, int type, int elapsed,
                int duration, const Image *mask, int option) {
  if (to.rgba.empty() || from.width != to.width || from.height != to.height ||
      from.rgba.size() != to.rgba.size())
    throw std::invalid_argument("Wipe frames must have equal dimensions");
  if (duration <= 0 || elapsed >= duration)
    return to;
  Image result = to;
  result.nativePixels.clear();
  if (type >= 0x100 && type < 0x700 && mask != nullptr && mask->width > 0 &&
      mask->height > 0 &&
      mask->rgba.size() == size_t(mask->width) * mask->height * 4) {
    const int norm = (type >= 0x400) ? (type - 0x300) : type;
    const int subMode = (norm >> 8) & 0xff;
    const int span = subMode == 1 ? 1 : subMode == 2 ? 64 : 128;
    const bool reverse = (option == 2 || option == 3);
    const float t =
        std::clamp(float(std::max(elapsed, 0)) / float(duration), 0.0f, 1.0f);
    const int threshold = int(std::lrint(t * float(255 + span)));
    for (int y = 0; y < result.height; ++y) {
      const int my =
          std::clamp((y * mask->height) / result.height, 0, mask->height - 1);
      for (int x = 0; x < result.width; ++x) {
        const int mx =
            std::clamp((x * mask->width) / result.width, 0, mask->width - 1);
        const uint8_t maskGray =
            mask->rgba[(size_t(my) * mask->width + size_t(mx)) * 4];
        const int m = reverse ? (255 - int(maskGray)) : int(maskGray);
        const int w =
            (std::clamp(threshold - m, 0, span) * 256 + span / 2) / span;
        const size_t i = (size_t(y) * result.width + size_t(x)) * 4;
        for (size_t c = 0; c < 3; ++c) {
          result.rgba[i + c] = uint8_t(
              (int(from.rgba[i + c]) * (256 - w) + int(to.rgba[i + c]) * w) >>
              8);
        }
      }
    }
    return result;
  }
  const auto weight = std::clamp<int>(
      int(std::lrint(float(std::max(elapsed, 0)) * 128 / float(duration))) * 2,
      0, 256);
  for (size_t i = 0; i < result.rgba.size(); i += 4)
    for (size_t c = 0; c < 3; ++c) {
      const int first = type == 3 ? 255 : type == 4 ? 0 : from.rgba[i + c];
      result.rgba[i + c] =
          uint8_t((first * (256 - weight) + to.rgba[i + c] * weight) >> 8);
    }
  return result;
}
} // namespace SummerRadish
