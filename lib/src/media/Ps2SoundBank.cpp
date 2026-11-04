#include <Kotonoha/media/Ps2SoundBank.hpp>

#include <algorithm>
#include <cstring>
#include <limits>
#include <stdexcept>

namespace Kotonoha {
namespace {
std::uint32_t word(const std::vector<Uint8> &bytes, std::size_t at) {
  if (at > bytes.size() || bytes.size() - at < 4)
    throw std::runtime_error("Truncated PS2 sound bank header");
  return std::uint32_t(bytes[at]) | (std::uint32_t(bytes[at + 1]) << 8) |
         (std::uint32_t(bytes[at + 2]) << 16) |
         (std::uint32_t(bytes[at + 3]) << 24);
}
void tag(const std::vector<Uint8> &bytes, std::size_t at,
         const char *expected) {
  if (at > bytes.size() || bytes.size() - at < 8 ||
      std::memcmp(bytes.data() + at, expected, 8) != 0)
    throw std::runtime_error("Invalid PS2 sound bank section");
}
// Arithmetic right shift, expressed without implementation-defined signed
// shifts.
int divide64(int value) {
  return value >= 0 ? value / 64 : -int((unsigned(-value) + 63) / 64);
}
} // namespace

std::vector<Ps2SoundBankWave>
parsePs2SoundBank(const std::vector<Uint8> &header, std::size_t bodyBytes) {
  // Sony IECS chunk structure: Vers, Head, Vagi (waveform records).
  // Format observations: vgmstream's original HD/BD format investigation,
  // https://github.com/vgmstream/vgmstream/blob/master/src/meta/hd_bd.c
  tag(header, 0, "IECSsreV");
  tag(header, 0x10, "IECSdaeH");
  if (word(header, 8) != 0x10 || word(header, 0x18) < 0x24 ||
      word(header, 0x1c) != header.size() || word(header, 0x20) != bodyBytes)
    throw std::runtime_error("PS2 sound bank HD/BD size mismatch");
  const std::size_t vagi = word(header, 0x30);
  tag(header, vagi, "IECSigaV");
  const std::size_t sectionBytes = word(header, vagi + 8);
  if (sectionBytes < 0x10 || vagi > header.size() ||
      sectionBytes > header.size() - vagi)
    throw std::runtime_error("Invalid PS2 waveform section length");
  const std::size_t end = vagi + sectionBytes;
  std::size_t count = word(header, vagi + 0xc);
  if (!count || count > (sectionBytes - 0x10) / 4)
    throw std::runtime_error("Invalid PS2 waveform count");
  // Some IECS banks store one additional valid table slot beyond the count.
  if (vagi + 0x10 + (count + 1) * 4 <= end &&
      word(header, vagi + 0x10 + count * 4))
    ++count;
  std::vector<Ps2SoundBankWave> waves;
  waves.reserve(count);
  for (std::size_t i = 0; i < count; ++i) {
    const std::size_t relative = word(header, vagi + 0x10 + i * 4);
    if (relative < 0x10 + count * 4 || relative > sectionBytes ||
        sectionBytes - relative < 8)
      throw std::runtime_error("Invalid PS2 waveform record offset");
    const std::size_t at = vagi + relative;
    const auto offset = word(header, at);
    const unsigned rate =
        unsigned(header[at + 4]) | (unsigned(header[at + 5]) << 8);
    const unsigned flags = header[at + 6], reserved = header[at + 7];
    if (offset == bodyBytes && i + 1 == count)
      break; // Native terminal dummy record is not an audio waveform.
    if (offset >= bodyBytes || offset % 16 || rate < 1000 || flags > 1 ||
        (reserved != 0 && reserved != 0xff))
      throw std::runtime_error("Invalid PS2 waveform metadata");
    waves.push_back({offset, 0, rate, bool(flags & 1)});
  }
  for (auto &wave : waves) {
    std::size_t next = bodyBytes;
    for (const auto &other : waves)
      if (other.offset > wave.offset)
        next = std::min(next, std::size_t(other.offset));
    const std::size_t length = next - wave.offset;
    if (!length || length % 16 ||
        length > std::numeric_limits<std::uint32_t>::max())
      throw std::runtime_error("Invalid PS2 ADPCM waveform range");
    wave.byteCount = std::uint32_t(length);
  }
  return waves;
}

AudioClip decodePs2SoundBankWave(const std::vector<Uint8> &header,
                                 const std::vector<Uint8> &body,
                                 std::size_t waveIndex) {
  const auto waves = parsePs2SoundBank(header, body.size());
  if (waveIndex >= waves.size())
    throw std::out_of_range("PS2 sound bank waveform index");
  const auto &wave = waves[waveIndex];
  AudioClip clip;
  clip.spec = {SDL_AUDIO_S16LE, 1, int(wave.sampleRate)};
  clip.data.reserve(std::size_t(wave.byteCount / 16) * 28 * 2);
  // Independent integer implementation of the SPU's predictive reconstruction.
  // Packet/nibble order and filters are documented in PSX-SPX's SPU research:
  // https://psx-spx.consoledev.net/soundprocessingunitspu/#sample-data-brrspu-adpcm
  constexpr int filters[5][2]{
      {0, 0}, {60, 0}, {115, -52}, {98, -55}, {122, -60}};
  int previous = 0, earlier = 0;
  for (std::size_t at = wave.offset;
       at < std::size_t(wave.offset) + wave.byteCount; at += 16) {
    if (body[at + 1] == 7) {
      // The standard silent terminal block does not feed ADPCM nibbles into
      // the output. Preserve its 28 sample positions as silence.
      clip.data.insert(clip.data.end(), 28 * 2, 0);
      continue;
    }
    const unsigned filter = body[at] >> 4;
    unsigned shift = body[at] & 15;
    if (filter >= 5)
      throw std::runtime_error("Unsupported PS2 ADPCM predictor");
    if (shift > 12)
      shift = 9; // SPU reserved shift values use the documented hardware value.
    for (unsigned i = 0; i < 28; ++i) {
      const unsigned packed = body[at + 2 + i / 2];
      int nibble = int((packed >> ((i & 1) * 4)) & 15);
      if (nibble >= 8)
        nibble -= 16;
      const int scaled = nibble * (1 << 12);
      const int residual =
          scaled >= 0
              ? scaled / (1 << shift)
              : -int((unsigned(-scaled) + (1u << shift) - 1) / (1u << shift));
      const int predicted = divide64(previous * filters[filter][0] +
                                     earlier * filters[filter][1]);
      const int sample = std::clamp(residual + predicted, -32768, 32767);
      earlier = previous;
      previous = sample;
      const auto bits = std::uint16_t(std::int16_t(sample));
      clip.data.push_back(Uint8(bits));
      clip.data.push_back(Uint8(bits >> 8));
    }
  }
  return clip;
}
} // namespace Kotonoha
