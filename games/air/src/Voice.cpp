#include <Air/Voice.hpp>
#include <algorithm>
#include <cctype>
#include <limits>
#include <regex>
#include <sstream>
#include <stdexcept>

namespace Air {
namespace {
std::string lower(std::string s) {
  for (auto &c : s)
    c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  return s;
}
std::uint32_t read32(const std::vector<std::uint8_t> &b, std::size_t p) {
  return b.at(p) | (std::uint32_t(b.at(p + 1)) << 8) |
         (std::uint32_t(b.at(p + 2)) << 16) |
         (std::uint32_t(b.at(p + 3)) << 24);
}
void write(std::vector<std::uint8_t> &out, std::size_t p, std::uint32_t n,
           int count) {
  for (int i = 0; i < count; ++i)
    out.at(p + i) = static_cast<std::uint8_t>(n >> (8 * i));
}
} // namespace
VoiceIndex::VoiceIndex(const std::filesystem::path &root) {
  auto dir = root / "KOE";
  if (!std::filesystem::is_directory(dir))
    dir = root / "koe";
  if (!std::filesystem::is_directory(dir))
    return;
  const std::regex pattern("z([0-9]{9})\\.ogg");
  for (const auto &entry : std::filesystem::recursive_directory_iterator(dir)) {
    if (!entry.is_regular_file())
      continue;
    std::smatch match;
    const auto name = lower(entry.path().filename().string());
    if (std::regex_match(name, match, pattern))
      files_.emplace(std::stoi(match[1].str()), entry.path());
  }
}
VoiceIndex::VoiceIndex(const std::vector<std::string> &assets) {
  const std::regex pattern("z([0-9]{9})\\.ogg");
  for (const auto &id : assets) {
    std::smatch match;
    const auto name = lower(std::filesystem::path(id).filename().string());
    if (std::regex_match(name, match, pattern))
      files_.emplace(std::stoi(match[1].str()), std::filesystem::u8path(id));
  }
}
std::optional<std::filesystem::path> VoiceIndex::find(int id) const {
  auto it = files_.find(id);
  if (it == files_.end())
    return std::nullopt;
  return it->second;
}
std::vector<std::uint8_t> nwaToWav(const std::vector<std::uint8_t> &b) {
  if (b.size() < 44)
    throw std::runtime_error("Truncated NWA header");
  const auto channels = b[0] | (unsigned(b[1]) << 8);
  const auto bits = b[2] | (unsigned(b[3]) << 8);
  const auto rate = read32(b, 4), size = read32(b, 20);
  if (read32(b, 8) != 0xffffffffu)
    throw std::runtime_error(
        "Compressed NWA is not supported by this edition adapter");
  if ((channels != 1 && channels != 2) || (bits != 8 && bits != 16) ||
      rate == 0 || rate > 384000 || size > b.size() - 44 ||
      size > std::numeric_limits<std::uint32_t>::max() - 36 ||
      size % (channels * (bits / 8)))
    throw std::runtime_error("Invalid NWA PCM header");
  std::vector<std::uint8_t> out(44 + size);
  std::copy_n("RIFF", 4, out.begin());
  std::copy_n("WAVEfmt ", 8, out.begin() + 8);
  std::copy_n("data", 4, out.begin() + 36);
  write(out, 4, 36 + size, 4);
  write(out, 16, 16, 4);
  write(out, 20, 1, 2);
  write(out, 22, channels, 2);
  write(out, 24, rate, 4);
  write(out, 28, rate * channels * (bits / 8), 4);
  write(out, 32, channels * (bits / 8), 2);
  write(out, 34, bits, 2);
  write(out, 40, size, 4);
  std::copy_n(b.begin() + 44, size, out.begin() + 44);
  return out;
}
AudioConfig parseAudioConfig(std::string_view ini) {
  AudioConfig out;
  const std::regex track(
      R"rx(^\s*#DSTRACK\s*=\s*([0-9]+)\s*-\s*([0-9]+)\s*-\s*([0-9]+)\s*=\s*"([^"]*)"\s*=\s*"([^"]*)")rx",
      std::regex::icase);
  const std::regex effect(
      R"rx(^\s*#SE\.([0-9]+)\s*=\s*"([^"]*)"\s*[,=]\s*([0-9]+))rx",
      std::regex::icase);
  std::istringstream stream{std::string(ini)};
  std::string line;
  while (std::getline(stream, line)) {
    std::smatch m;
    if (std::regex_search(line, m, track)) {
      AudioTrack t{lower(m[5].str()), m[4].str(), std::stoll(m[1].str()),
                   std::stoll(m[2].str()), std::stoll(m[3].str())};
      out.tracks[t.name] = std::move(t);
    } else if (std::regex_search(line, m, effect))
      out.soundEffects[std::stoi(m[1].str())] = {m[2].str(),
                                                 std::stoi(m[3].str())};
  }
  return out;
}
} // namespace Air
