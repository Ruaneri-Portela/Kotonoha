#pragma once
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace Air {
// Indexes the edition's unpacked KOE voices without copying game assets.
class VoiceIndex {
public:
  explicit VoiceIndex(const std::filesystem::path &gameRoot);
  std::optional<std::filesystem::path> find(int id) const;
  std::size_t size() const { return files_.size(); }

private:
  std::unordered_map<int, std::filesystem::path> files_;
};
// Converts uncompressed NWA PCM (-1 compression) to a standard RIFF WAV.
// Throws for invalid data or compressed NWA, which this AIR edition does not
// use.
std::vector<std::uint8_t> nwaToWav(const std::vector<std::uint8_t> &bytes);
struct AudioTrack {
  std::string name, file;
  std::int64_t from = 0, to = 0, loop = 0; // PCM frame positions
};
struct SoundEffect {
  std::string file;
  int channel = 0;
};
struct AudioConfig {
  std::unordered_map<std::string, AudioTrack> tracks; // lowercase track name
  std::unordered_map<int, SoundEffect> soundEffects;
};
AudioConfig parseAudioConfig(std::string_view ini);
} // namespace Air
