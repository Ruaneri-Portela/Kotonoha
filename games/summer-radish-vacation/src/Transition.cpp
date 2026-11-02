#include <SummerRadish/Transition.hpp>
#include <algorithm>
#include <cmath>
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
                                                   : 0;
  return base * (1000 - speed) / 500;
}
Image wipeFrame(const Image &from, const Image &to, int type, int elapsed,
                int duration) {
  if (to.rgba.empty() || from.width != to.width || from.height != to.height ||
      from.rgba.size() != to.rgba.size())
    throw std::invalid_argument("Wipe frames must have equal dimensions");
  if (duration <= 0 || elapsed >= duration)
    return to;
  const auto weight = std::clamp<int>(
      int(std::lrint(float(std::max(elapsed, 0)) * 128 / float(duration))) * 2,
      0, 256);
  Image result = to;
  result.nativePixels.clear();
  for (size_t i = 0; i < result.rgba.size(); i += 4)
    for (size_t c = 0; c < 3; ++c) {
      const int first = type == 3 ? 255 : type == 4 ? 0 : from.rgba[i + c];
      result.rgba[i + c] =
          uint8_t((first * (256 - weight) + to.rgba[i + c] * weight) >> 8);
    }
  return result;
}
} // namespace SummerRadish
