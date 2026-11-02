#pragma once
#include <array>
#include <cstdint>
#include <string>
#include <vector>
namespace SummerRadish {
std::string graphicSuffix(uint32_t mode);
// Native TOPLADY: highest affection wins, with original tie priority 4,5,3,6.
// Native NameEntry.sdll: four fields, 1..6 UTF-16 units; private-use chars
// become '?'.
std::string normalizePlayerName(const std::string &name);
unsigned playerNameChangeMask(const std::array<std::string, 4> &names,
                              const std::array<std::string, 4> &defaults);
std::string substitutePlayerNames(const std::string &text,
                                  const std::array<std::string, 4> &names);
int topLady(const std::array<int64_t, 4> &affection);
struct CharacterPlacement {
  int32_t requested = -1;
  unsigned previous = 0;
};
// Input order is native character creation order. Updates retained slots.
void assignCharacterPositions(std::vector<CharacterPlacement> &characters,
                              unsigned slotCount = 3);
} // namespace SummerRadish
