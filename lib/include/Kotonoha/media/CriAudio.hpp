#pragma once
#include <Kotonoha/core/AssetProvider.hpp>
#include <cstdint>
namespace Kotonoha {
// CRI type-8 cipher parameters derived by the game's own key initialization.
// Keep game-specific keys in its AssetProvider, not in the engine.
struct CriKey8 {
  uint16_t seed, multiplier, increment;
};
// Return a clear ADX/AHX container for MediaDecoder. No source file is changed.
std::vector<Uint8> decryptCriAudio(const std::vector<Uint8> &bytes,
                                   CriKey8 key);
AudioClip decodeCriAudio(const std::vector<Uint8> &bytes, CriKey8 key);
} // namespace Kotonoha
