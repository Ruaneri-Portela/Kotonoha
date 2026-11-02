#include <SummerRadish/Native.hpp>
#include <SummerRadish/Pack.hpp>
#include <iomanip>
#include <sstream>
#include <stdexcept>
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
void assignCharacterPositions(std::vector<CharacterPlacement> &characters,
                              unsigned slotCount) {
  if (!slotCount || slotCount > 1024)
    throw std::runtime_error("Invalid native character position count");
  std::vector<int> occupied(slotCount, -1);
  for (size_t i = 0; i < characters.size(); ++i) {
    auto &character = characters[i];
    auto requested = static_cast<uint32_t>(character.requested);
    unsigned slot = requested;
    if (character.requested < 0)
      slot = characters.size() == 1 ? 0 : character.previous;
    else if (requested & 0x40000000u)
      slot = characters.size() == 1 ? 0 : requested & 0xffffu;
    if (slot >= slotCount)
      slot = 0;
    const int previous = occupied[slot];
    if (previous < 0) {
      occupied[slot] = static_cast<int>(i);
      character.previous = slot;
    } else if (characters[size_t(previous)].requested < 0) {
      for (unsigned free = 0; free < slotCount; ++free)
        if (free != slot && occupied[free] < 0) {
          occupied[free] = previous;
          characters[size_t(previous)].previous = free;
          break;
        }
      occupied[slot] = static_cast<int>(i);
      character.previous = slot;
    }
  }
  if (slotCount > 2 && characters.size() == 2 && occupied[0] >= 0) {
    auto &center = characters[size_t(occupied[0])];
    if (center.requested < 0) {
      if (occupied[1] < 0)
        center.previous = 1;
      else if (occupied[2] < 0)
        center.previous = 2;
    }
  }
}
} // namespace SummerRadish
