#pragma once

#include <Kotonoha/core/AssetProvider.hpp>
#include <cstddef>
#include <vector>

namespace Kotonoha::Providers::MeoAudio {
struct Bgm {
  AudioClip clip;
  std::size_t loopStart = 0;
};

Bgm decodeBgm(const std::vector<Uint8> &bytes);
AudioClip decodeSnd(const std::vector<Uint8> &bytes);
} // namespace Kotonoha::Providers::MeoAudio
