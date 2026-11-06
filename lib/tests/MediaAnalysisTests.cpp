#include <Kotonoha/media/MediaDecoder.hpp>
#include <Kotonoha/media/TextureAlpha.hpp>

#include <atomic>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <vector>

namespace {

void require(bool value, const char *message) {
  if (!value)
    throw std::runtime_error(message);
}

std::vector<Uint8> wave(int samples) {
  std::vector<Uint8> bytes;
  const auto u32 = [&](Uint32 value) {
    for (int i = 0; i < 4; ++i)
      bytes.push_back(Uint8(value >> (i * 8)));
  };
  const auto u16 = [&](Uint16 value) {
    bytes.push_back(Uint8(value));
    bytes.push_back(Uint8(value >> 8));
  };
  const auto tag = [&](const char *text) {
    bytes.insert(bytes.end(), text, text + 4);
  };
  tag("RIFF");
  u32(36 + Uint32(samples) * 4);
  tag("WAVE");
  tag("fmt ");
  u32(16);
  u16(1);
  u16(2);
  u32(8000);
  u32(32000);
  u16(4);
  u16(16);
  tag("data");
  u32(Uint32(samples) * 4);
  for (int i = 0; i < samples; ++i) {
    u16(Uint16(12000));
    u16(Uint16(4000));
  }
  return bytes;
}

void pcmDecode() {
  const auto source = wave(800); // 100 ms at 8 kHz.
  std::vector<std::int16_t> pcm;
  require(Kotonoha::MediaDecoder::decodeMonoS16At44100(source, pcm),
          "mono s16 analysis decode failed");
  require(pcm.size() >= 4400 && pcm.size() <= 4420,
          "analysis decode did not normalize to 44.1 kHz");

  require(
      Kotonoha::MediaDecoder::decodeMonoS16At44100(source, pcm, nullptr, 20),
      "bounded analysis decode failed");
  require(!pcm.empty() && pcm.size() <= 882,
          "authored duration cap did not bound PCM samples");

  std::atomic_bool cancelled{true};
  require(
      !Kotonoha::MediaDecoder::decodeMonoS16At44100(source, pcm, &cancelled) &&
          pcm.empty(),
      "cancelled analysis decode retained PCM");

  require(!Kotonoha::MediaDecoder::decodeMonoS16At44100({1, 2, 3}, pcm) &&
              pcm.empty(),
          "invalid analysis media did not fail closed");
}

void straightAlphaEdges() {
  std::vector<std::uint8_t> rgba(5 * 5 * 4, 0);
  const std::size_t center = (2 * 5 + 2) * 4;
  rgba[center] = 90;
  rgba[center + 1] = 120;
  rgba[center + 2] = 150;
  rgba[center + 3] = 128;
  const auto original = rgba;

  require(Kotonoha::prepareStraightAlphaForLinearFiltering(rgba.data(), 5, 5,
                                                           5 * 4) == 8,
          "straight-alpha edge prep did not fill the one-pixel margin");
  for (int y = 0; y < 5; ++y) {
    for (int x = 0; x < 5; ++x) {
      const std::size_t p = std::size_t(y * 5 + x) * 4;
      const bool neighbor =
          x >= 1 && x <= 3 && y >= 1 && y <= 3 && !(x == 2 && y == 2);
      require(rgba[p + 3] == original[p + 3],
              "straight-alpha edge prep changed alpha");
      if (x == 2 && y == 2) {
        require(rgba[p] == 90 && rgba[p + 1] == 120 && rgba[p + 2] == 150,
                "straight-alpha edge prep changed a visible texel");
      } else if (neighbor) {
        require(
            rgba[p] == 90 && rgba[p + 1] == 120 && rgba[p + 2] == 150,
            "straight-alpha edge prep missed an adjacent transparent texel");
      } else {
        require(rgba[p] == 0 && rgba[p + 1] == 0 && rgba[p + 2] == 0,
                "straight-alpha edge prep spread beyond one pixel");
      }
    }
  }
}

} // namespace

int main() {
  try {
    pcmDecode();
    straightAlphaEdges();
    std::cout << "KotonohaMediaAnalysisTests PASS\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << "KotonohaMediaAnalysisTests FAIL: " << error.what() << '\n';
    return 1;
  }
}
