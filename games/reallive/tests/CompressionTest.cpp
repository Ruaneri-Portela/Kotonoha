// Copyright 2026 Kotonoha contributors. SPDX-License-Identifier: MIT
// Synthetic RealLive LZ/XOR vectors; no proprietary game data is required.
#include "libreallive/compression.h"

#include <array>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace {
using libreallive::compression::Decompress;
constexpr std::array<unsigned char, 20> kMask = {
    0x8b, 0xe5, 0x5d, 0xc3, 0xa1, 0xe0, 0x30, 0x44, 0x00, 0x85,
    0xc0, 0x74, 0x09, 0x5f, 0x5e, 0x33, 0xc0, 0x5b, 0x8b, 0xe5};

void require(bool result, const char *message) {
  if (!result)
    throw std::runtime_error(message);
}

std::vector<char> literals(std::string_view value) {
  require(!value.empty() && value.size() <= 8, "literal fixture length");
  std::vector<char> encoded(9, 0);
  encoded[8] = static_cast<char>((1u << value.size()) - 1u);
  for (std::size_t i = 0; i < value.size(); ++i) {
    encoded.push_back(
        static_cast<char>(static_cast<unsigned char>(value[i]) ^ kMask[9 + i]));
  }
  return encoded;
}

std::string decode(const std::vector<char> &bytes, std::size_t size,
                   const libreallive::compression::XorKey *key = nullptr) {
  std::string result(size, '\0');
  Decompress(bytes.data(), bytes.size(), result.data(), size, key);
  return result;
}

template <typename F> void rejects(F operation, const char *message) {
  bool threw = false;
  try {
    operation();
  } catch (const libreallive::Error &) {
    threw = true;
  }
  require(threw, message);
}
} // namespace

int main() {
  try {
    const auto abc = literals("ABC");
    require(decode(abc, 3) == "ABC", "literals decode correctly");
    require(decode(abc, 0).empty(), "empty output is accepted");
    auto trailing = abc;
    trailing.push_back('X');
    require(decode(trailing, 3) == "ABC",
            "trailing archive padding stays compatible");

    // Two literals followed by a length-six back-reference at distance two.
    auto repeated = literals("AB");
    repeated[8] = 0x03; // bit 2 is a back-reference
    repeated.push_back(static_cast<char>(0x24 ^ kMask[11]));
    repeated.push_back(static_cast<char>(0x00 ^ kMask[12]));
    require(decode(repeated, 8) == "ABABABAB",
            "overlapping LZ back-references decode correctly");

    auto missingLiteral = abc;
    missingLiteral.pop_back();
    rejects([&] { (void)decode(missingLiteral, 3); },
            "incomplete literal payload must be rejected");
    rejects([&] { (void)decode(std::vector<char>(8, 0), 1); },
            "truncated archive header must be rejected");

    std::vector<char> zeroDistance(9, 0); // back-reference flag
    zeroDistance.push_back(static_cast<char>(kMask[9]));
    zeroDistance.push_back(static_cast<char>(kMask[10]));
    rejects([&] { (void)decode(zeroDistance, 2); },
            "zero-distance back-reference must be rejected");

    std::vector<char> tooFar(9, 0);
    tooFar.push_back(static_cast<char>(0x10 ^ kMask[9]));
    tooFar.push_back(static_cast<char>(kMask[10]));
    rejects([&] { (void)decode(tooFar, 2); },
            "back-reference before output must be rejected");

    auto overflowing = literals("A");
    overflowing[8] = 0x01;
    overflowing.push_back(static_cast<char>(0x13 ^ kMask[10]));
    overflowing.push_back(static_cast<char>(kMask[11]));
    rejects([&] { (void)decode(overflowing, 2); },
            "back-reference cannot exceed declared output size");

    auto cutBackref = repeated;
    cutBackref.pop_back();
    rejects([&] { (void)decode(cutBackref, 8); },
            "truncated back-reference must be rejected");

    // A second flag byte is consumed after precisely eight tokens.
    auto nine = literals("ABCDEFGH");
    nine.push_back(static_cast<char>(0x01 ^ kMask[17]));
    nine.push_back(static_cast<char>('I' ^ kMask[18]));
    require(decode(nine, 9) == "ABCDEFGHI",
            "decoder must reload flags at token boundaries");
    nine.pop_back();
    rejects([&] { (void)decode(nine, 9); },
            "truncated final token must be rejected");

    libreallive::compression::XorKey key[] = {{{0}, 256, 16}, {{0}, -1, -1}};
    require(decode(abc, 3, key) == "ABC",
            "short scripts skip per-game XOR ranges safely");

    std::cout << "RealLive compression regressions passed\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << "RealLive compression regression: " << error.what() << '\n';
    return 1;
  }
}
