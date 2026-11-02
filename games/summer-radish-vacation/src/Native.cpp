#include <SummerRadish/Native.hpp>
#include <SummerRadish/Pack.hpp>
#include <iomanip>
#include <sstream>
#include <stdexcept>
namespace SummerRadish {
std::string normalizePlayerName(const std::string &name) {
  std::string result;
  unsigned units = 0;
  for (size_t at = 0; at < name.size();) {
    const size_t begin = at;
    unsigned lead = static_cast<unsigned char>(name[at++]), code = lead;
    unsigned extra = 0;
    if (lead >= 0xc2 && lead <= 0xdf) {
      extra = 1;
      code = lead & 31;
    } else if (lead >= 0xe0 && lead <= 0xef) {
      extra = 2;
      code = lead & 15;
    } else if (lead >= 0xf0 && lead <= 0xf4) {
      extra = 3;
      code = lead & 7;
    } else if (lead >= 0x80 || lead == 0)
      throw std::invalid_argument("Invalid name encoding");
    for (unsigned i = 0; i < extra; ++i) {
      if (at == name.size() ||
          (static_cast<unsigned char>(name[at]) & 0xc0) != 0x80)
        throw std::invalid_argument("Invalid name encoding");
      code = (code << 6) | (static_cast<unsigned char>(name[at++]) & 63);
    }
    if ((extra == 1 && code < 0x80) || (extra == 2 && code < 0x800) ||
        (extra == 3 && code < 0x10000) || code > 0x10ffff ||
        (code >= 0xd800 && code <= 0xdfff))
      throw std::invalid_argument("Invalid name encoding");
    units += code > 0xffff ? 2 : 1;
    if (code >= 0xe000 && code < 0xf900)
      result += '?';
    else
      result.append(name, begin, at - begin);
  }
  if (units == 0 || units > 6)
    throw std::invalid_argument("Each name must contain 1 to 6 characters");
  return result;
}
unsigned playerNameChangeMask(const std::array<std::string, 4> &names,
                              const std::array<std::string, 4> &defaults) {
  return ((names[0] != defaults[0] || names[1] != defaults[1]) ? 1u : 0u) |
         ((names[2] != defaults[2] || names[3] != defaults[3]) ? 2u : 0u);
}
std::string substitutePlayerNames(const std::string &text,
                                  const std::array<std::string, 4> &names) {
  std::string result;
  for (size_t i = 0; i < text.size();) {
    if (i + 2 < text.size() && static_cast<unsigned char>(text[i]) == 0xef &&
        static_cast<unsigned char>(text[i + 1]) == 0xa3 &&
        static_cast<unsigned char>(text[i + 2]) >= 0xb0 &&
        static_cast<unsigned char>(text[i + 2]) <= 0xb3) {
      result += names[static_cast<unsigned char>(text[i + 2]) - 0xb0];
      i += 3;
    } else
      result += text[i++];
  }
  return result;
}
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
