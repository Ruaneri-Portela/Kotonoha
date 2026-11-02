#pragma once
#include <array>
#include <cstdint>
#include <string>
namespace SummerRadish {
std::string graphicSuffix(uint32_t mode);
// Native TOPLADY: highest affection wins, with original tie priority 4,5,3,6.
int topLady(const std::array<int64_t, 4> &affection);
} // namespace SummerRadish
