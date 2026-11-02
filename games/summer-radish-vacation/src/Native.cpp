#include <SummerRadish/Native.hpp>
#include <SummerRadish/Pack.hpp>
#include <iomanip>
#include <sstream>
namespace SummerRadish {
std::string graphicSuffix(uint32_t mode) {
  const auto index = mode & 0xfff, digits = (mode >> 12) & 7;
  std::string result;
  if (digits) {
    std::ostringstream out;
    out << std::setw(int(digits)) << std::setfill('0') << index;
    result = out.str();
  } else if (index) {
    Bytes character{uint8_t((64 + index) & 255), uint8_t((64 + index) >> 8)};
    result = utf16(character, 0, 1);
  }
  if (mode & 0x10000)
    result += "_H";
  return result;
}
int topLady(const std::array<int64_t, 4> &affection) {
  static constexpr int actors[4] = {4, 5, 3, 6};
  size_t selected = 0;
  for (size_t i = 1; i < affection.size(); ++i)
    if (affection[i] > affection[selected])
      selected = i;
  return actors[selected];
}
} // namespace SummerRadish
