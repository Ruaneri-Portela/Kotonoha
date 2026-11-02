#pragma once
#include <array>
#include <cstdint>
#include <string>
#include <vector>
namespace SummerRadish {
std::string graphicSuffix(uint32_t mode);
// Native TOPLADY: highest affection wins, with original tie priority 4,5,3,6.
int topLady(const std::array<int64_t, 4> &affection);
struct CharacterPlacement {
  int32_t requested = -1;
  unsigned previous = 0;
};
// Input order is native character creation order. Updates retained slots.
void assignCharacterPositions(std::vector<CharacterPlacement> &characters,
                              unsigned slotCount = 3);
} // namespace SummerRadish
