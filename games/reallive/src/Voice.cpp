#include <RealLive/Voice.hpp>
#include <algorithm>
#include <cctype>
#include <fstream>
#include <limits>
#include <regex>
#include <sstream>
#include <stdexcept>

namespace RealLive {
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
      files_.emplace(
          std::stoi(match[1].str()),
          Entry{"", entry.path(), 0, std::filesystem::file_size(entry.path())});
  }
}
VoiceIndex::VoiceIndex(const std::vector<std::string> &assets) {
  const std::regex pattern("z([0-9]{9})\\.ogg");
  for (const auto &id : assets) {
    std::smatch match;
    const auto name = lower(std::filesystem::path(id).filename().string());
    if (std::regex_match(name, match, pattern))
      files_.emplace(std::stoi(match[1].str()),
                     Entry{"", std::filesystem::u8path(id), 0, 0});
  }
}
VoiceIndex::VoiceIndex(const std::vector<std::string> &assets, Reader reader,
                       LocalPath localPath)
    : reader_(std::move(reader)) {
  const std::regex pattern("z([0-9]{9})\\.ogg");
  for (const auto &id : assets) {
    std::smatch match;
    const auto name = lower(std::filesystem::path(id).filename().string());
    if (std::regex_match(name, match, pattern))
      files_.emplace(std::stoi(match[1].str()),
                     Entry{id, std::filesystem::u8path(localPath(id)), 0, 0});
  }
  const std::regex nwkPattern("z([0-9]{4})\\.nwk");
  for (const auto &asset : assets) {
    std::smatch match;
    const auto name = lower(std::filesystem::path(asset).filename().string());
    if (!std::regex_match(name, match, nwkPattern))
      continue;
    const int archive = std::stoi(match[1].str());
    const auto local = std::filesystem::u8path(localPath(asset));
    std::vector<std::uint8_t> table;
    if (!local.empty()) {
      std::ifstream input(local, std::ios::binary);
      std::uint8_t countBytes[4]{};
      if (!input.read(reinterpret_cast<char *>(countBytes), 4))
        continue;
      const auto count = std::uint32_t(countBytes[0]) |
                         (std::uint32_t(countBytes[1]) << 8) |
                         (std::uint32_t(countBytes[2]) << 16) |
                         (std::uint32_t(countBytes[3]) << 24);
      if (count > 1000000)
        continue;
      table.resize(4 + std::size_t(count) * 12);
      std::copy(countBytes, countBytes + 4, table.begin());
      if (!input.read(reinterpret_cast<char *>(table.data() + 4),
                      std::streamsize(table.size() - 4)))
        continue;
    } else {
      table = reader_(asset);
    }
    if (table.size() < 4)
      continue;
    const auto count = read32(table, 0);
    if (count > 1000000 || table.size() < 4 + std::size_t(count) * 12)
      continue;
    for (std::uint32_t i = 0; i < count; ++i) {
      const auto p = 4 + std::size_t(i) * 12;
      const auto size = read32(table, p);
      const auto offset = read32(table, p + 4);
      const auto number = read32(table, p + 8);
      files_[archive * 100000 + int(number)] =
          Entry{asset, local, offset, size};
    }
  }
}
std::optional<std::vector<std::uint8_t>> VoiceIndex::read(int id) const {
  auto it = files_.find(id);
  if (it == files_.end())
    return std::nullopt;
  const auto &entry = it->second;
  if (!entry.local.empty()) {
    std::ifstream input(entry.local, std::ios::binary);
    if (!input.seekg(std::streamoff(entry.offset)))
      return std::nullopt;
    const auto size =
        entry.size ? entry.size : std::filesystem::file_size(entry.local);
    std::vector<std::uint8_t> bytes(size);
    if (!input.read(reinterpret_cast<char *>(bytes.data()),
                    std::streamsize(bytes.size())))
      return std::nullopt;
    return bytes;
  }
  auto bytes = reader_(entry.asset);
  if (entry.offset > bytes.size() || entry.size > bytes.size() - entry.offset)
    return std::nullopt;
  return std::vector<std::uint8_t>(bytes.begin() + entry.offset,
                                   bytes.begin() + entry.offset + entry.size);
}
std::optional<std::filesystem::path> VoiceIndex::find(int id) const {
  auto it = files_.find(id);
  if (it == files_.end() || it->second.local.empty())
    return std::nullopt;
  return it->second.local;
}
std::vector<std::uint8_t> nwaToWav(const std::vector<std::uint8_t> &b) {
  if (b.size() < 44)
    throw std::runtime_error("Truncated NWA header");
  const auto channels = b[0] | (unsigned(b[1]) << 8);
  const auto bits = b[2] | (unsigned(b[3]) << 8);
  const auto rate = read32(b, 4), size = read32(b, 20);
  const auto compression = static_cast<std::int32_t>(read32(b, 8));
  if (compression != -1) {
    const auto runLength = read32(b, 12) != 0;
    const auto blocks = read32(b, 16), dataSize = read32(b, 20);
    const auto sampleCount = read32(b, 28), blockSize = read32(b, 32),
               restSize = read32(b, 36);
    if ((channels != 1 && channels != 2) || (bits != 8 && bits != 16) ||
        rate == 0 || compression < 0 || compression > 5 || blocks == 0 ||
        blocks > 1000000 || dataSize > 512 * 1024 * 1024 ||
        sampleCount != (blocks - 1) * blockSize + restSize ||
        dataSize != sampleCount * (bits / 8) ||
        b.size() < 44 + std::size_t(blocks) * 4)
      throw std::runtime_error("Invalid compressed NWA header");
    std::vector<std::uint32_t> offsets(blocks);
    for (std::uint32_t i = 0; i < blocks; ++i) {
      offsets[i] = read32(b, 44 + std::size_t(i) * 4);
      if (offsets[i] >= b.size() || (i && offsets[i] < offsets[i - 1]))
        throw std::runtime_error("Invalid compressed NWA block offset");
    }
    std::vector<std::uint8_t> pcm;
    pcm.reserve(dataSize);
    for (std::uint32_t block = 0; block < blocks; ++block) {
      const std::size_t begin = offsets[block];
      const std::size_t end =
          block + 1 < blocks ? offsets[block + 1] : b.size();
      const std::size_t outputBytes =
          std::size_t(block + 1 == blocks ? restSize : blockSize) * (bits / 8);
      if (begin >= end || end > b.size())
        throw std::runtime_error("Truncated compressed NWA block");
      std::size_t pos = begin;
      int predictor[2]{};
      auto initial = [&]() {
        if (bits == 8) {
          if (pos >= end)
            throw std::runtime_error("Truncated NWA predictor");
          return int(static_cast<std::int8_t>(b[pos++]));
        }
        if (pos + 2 > end)
          throw std::runtime_error("Truncated NWA predictor");
        int value = static_cast<std::int16_t>(std::uint16_t(b[pos]) |
                                              (std::uint16_t(b[pos + 1]) << 8));
        pos += 2;
        return value;
      };
      predictor[0] = initial();
      if (channels == 2)
        predictor[1] = initial();
      int shift = 0, channel = 0, repeat = 0;
      auto getBits = [&](int count) {
        if (shift > 8) {
          ++pos;
          shift -= 8;
        }
        if (pos + 2 > end)
          throw std::runtime_error("Truncated NWA bitstream");
        int value = int(b[pos]) | (int(b[pos + 1]) << 8);
        value = (value >> shift) & ((1 << count) - 1);
        shift += count;
        return value;
      };
      const std::size_t samples = outputBytes / (bits / 8);
      for (std::size_t sample = 0; sample < samples; ++sample) {
        if (repeat == 0) {
          const int type = getBits(3);
          if (type == 7) {
            if (getBits(1))
              predictor[channel] = 0;
            else {
              const int count = compression >= 3 ? 8 : 8 - compression;
              const int deltaShift = compression >= 3 ? 9 : 9 + compression;
              const int value = getBits(count), sign = 1 << (count - 1);
              const int delta = (value & (sign - 1)) << deltaShift;
              predictor[channel] += value & sign ? -delta : delta;
            }
          } else if (type != 0) {
            const int count =
                compression >= 3 ? compression + 3 : 5 - compression;
            const int deltaShift =
                compression >= 3 ? 1 + type : 2 + type + compression;
            const int value = getBits(count), sign = 1 << (count - 1);
            const int delta = (value & (sign - 1)) << deltaShift;
            predictor[channel] += value & sign ? -delta : delta;
          } else if (runLength && getBits(1)) {
            repeat = getBits(2);
            if (repeat == 3)
              repeat = getBits(8);
          }
        } else {
          --repeat;
        }
        if (bits == 8)
          pcm.push_back(static_cast<std::uint8_t>(predictor[channel]));
        else {
          pcm.push_back(static_cast<std::uint8_t>(predictor[channel]));
          pcm.push_back(static_cast<std::uint8_t>(predictor[channel] >> 8));
        }
        if (channels == 2)
          channel ^= 1;
      }
    }
    if (pcm.size() != dataSize)
      throw std::runtime_error("Compressed NWA output size mismatch");
    std::vector<std::uint8_t> out(44 + pcm.size());
    std::copy_n("RIFF", 4, out.begin());
    std::copy_n("WAVEfmt ", 8, out.begin() + 8);
    std::copy_n("data", 4, out.begin() + 36);
    write(out, 4, 36 + std::uint32_t(pcm.size()), 4);
    write(out, 16, 16, 4);
    write(out, 20, 1, 2);
    write(out, 22, channels, 2);
    write(out, 24, rate, 4);
    write(out, 28, rate * channels * (bits / 8), 4);
    write(out, 32, channels * (bits / 8), 2);
    write(out, 34, bits, 2);
    write(out, 40, std::uint32_t(pcm.size()), 4);
    std::copy(pcm.begin(), pcm.end(), out.begin() + 44);
    return out;
  }
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
} // namespace RealLive
