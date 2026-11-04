#include <Kotonoha/media/Ps2SoundBank.hpp>
#include <algorithm>
#include <cstring>
#include <iostream>
#include <stdexcept>

namespace {
void require(bool value, const char *message) {
  if (!value)
    throw std::runtime_error(message);
}
void word(std::vector<Uint8> &bytes, size_t at, unsigned value) {
  for (unsigned i = 0; i < 4; ++i)
    bytes.at(at + i) = Uint8(value >> (i * 8));
}
std::vector<Uint8> header(size_t bodyBytes) {
  std::vector<Uint8> bytes(0x78);
  std::memcpy(bytes.data(), "IECSsreV", 8);
  word(bytes, 8, 0x10);
  word(bytes, 12, 0x20000);
  std::memcpy(bytes.data() + 0x10, "IECSdaeH", 8);
  word(bytes, 0x18, 0x40);
  word(bytes, 0x1c, bytes.size());
  word(bytes, 0x20, bodyBytes);
  word(bytes, 0x30, 0x50);
  std::memcpy(bytes.data() + 0x50, "IECSigaV", 8);
  word(bytes, 0x58, 0x28);
  word(bytes, 0x5c, 1);
  word(bytes, 0x60, 0x20);
  word(bytes, 0x70, 0);
  bytes[0x74] = 0x22;
  bytes[0x75] = 0x56; // 22050 Hz
  bytes[0x77] = 0xff;
  return bytes;
}
int sample(const Kotonoha::AudioClip &clip, size_t index) {
  const auto value = unsigned(clip.data.at(index * 2)) |
                     (unsigned(clip.data.at(index * 2 + 1)) << 8);
  return value >= 0x8000 ? int(value) - 0x10000 : int(value);
}
template <class F> void rejects(F callback) {
  bool caught = false;
  try {
    callback();
  } catch (const std::exception &) {
    caught = true;
  }
  require(caught, "Malformed sound bank must fail explicitly");
}
} // namespace
int main() {
  try {
    auto hd = header(32);
    std::vector<Uint8> bd(32);
    bd[0] = 12; // predictor 0, shift 12: signed 4-bit samples directly
    bd[2] = 0x78;
    bd[3] = 0x1f;
    bd[16] = 0xff;
    bd[17] = 7; // terminal silence ignores encoded payload/header predictor
    std::fill(bd.begin() + 18, bd.end(), 0xff);
    const auto waves = Kotonoha::parsePs2SoundBank(hd, bd.size());
    require(waves.size() == 1 && waves[0].sampleRate == 22050 &&
                waves[0].byteCount == 32,
            "HD wave record and BD range");
    const auto clip = Kotonoha::decodePs2SoundBankWave(hd, bd, 0);
    require(clip.spec.format == SDL_AUDIO_S16LE && clip.spec.channels == 1 &&
                clip.spec.freq == 22050 && clip.data.size() == 56 * 2,
            "Wave decodes to original-rate mono PCM");
    require(sample(clip, 0) == -8 && sample(clip, 1) == 7 &&
                sample(clip, 2) == -1 && sample(clip, 3) == 1,
            "SPU nibble order and signed residual");
    require(sample(clip, 28) == 0 && sample(clip, 55) == 0,
            "Standard terminal block preserves silent sample positions");
    rejects([&] { Kotonoha::decodePs2SoundBankWave(hd, bd, 1); });
    auto damaged = hd;
    damaged[0] = 0;
    rejects([&] { Kotonoha::parsePs2SoundBank(damaged, bd.size()); });
    damaged = hd;
    word(damaged, 0x60, 0xfffffff0);
    rejects([&] { Kotonoha::parsePs2SoundBank(damaged, bd.size()); });
    damaged = hd;
    word(damaged, 0x70, 33);
    rejects([&] { Kotonoha::parsePs2SoundBank(damaged, bd.size()); });
    rejects([&] { Kotonoha::parsePs2SoundBank(hd, 31); });
    bd[0] = 0x50;
    rejects([&] { Kotonoha::decodePs2SoundBankWave(hd, bd, 0); });
    std::cout << "PS2 sound bank bounds and SPU ADPCM passed\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
