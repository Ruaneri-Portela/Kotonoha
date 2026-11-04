#pragma once

#include <Kotonoha/core/AssetProvider.hpp>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace Kotonoha {

// One mono SPU-ADPCM waveform in a Sony PS2 IECS .HD/.BD bank.
// This is a waveform index, not a MIDI program/note or game sound-effect ID.
struct Ps2SoundBankWave {
  std::uint32_t offset = 0;
  std::uint32_t byteCount = 0;
  unsigned sampleRate = 0;
  bool loop = false;
};

// Validate HD metadata and resolve bounded waveform ranges in its BD body.
// Neither function opens files or depends on a particular game's bank layout.
std::vector<Ps2SoundBankWave>
parsePs2SoundBank(const std::vector<Uint8> &header, std::size_t bodyBytes);

// Decode exactly one waveform to signed 16-bit mono PCM, preserving its bank
// sample rate. Loop metadata is available through parsePs2SoundBank; no loop,
// envelope, pan, MIDI note transposition or SPU reverb is synthesized here.
AudioClip decodePs2SoundBankWave(const std::vector<Uint8> &header,
                                 const std::vector<Uint8> &body,
                                 std::size_t waveIndex);

} // namespace Kotonoha
