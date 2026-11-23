// Overflow ADV audio codecs (.ADPS BGM, .ADP4 voice).
#include <SnowRadish/PackAudio.hpp>

#include "../../../internal/ByteReader.hpp"

#include <algorithm>
#include <cstdint>
#include <vector>

#include "AdpsTables.inc"

namespace SnowRadish {

Pcm decodeAdps(const uint8_t *data, size_t size) {
  Pcm pcm;
  pcm.samples.reserve(size * 2);
  int state[2] = {0, 0}, pred[2] = {0, 0};
  for (size_t i = 0; i + 1 < size; i += 2) {
    int16_t frame[2][2];
    for (int ch = 0; ch < 2; ++ch) {
      const uint8_t by = data[i + ch];
      for (int n = 0; n < 2; ++n) {
        const int nib = n ? by >> 4 : by & 15;
        const int k = state[ch] + nib;
        pred[ch] = std::clamp(pred[ch] + kAdpsDelta[k], -32768, 32767);
        state[ch] = kAdpsNext[k];
        frame[ch][n] = static_cast<int16_t>(pred[ch]);
      }
    }
    for (int n = 0; n < 2; ++n) {
      pcm.samples.push_back(frame[0][n]);
      pcm.samples.push_back(frame[1][n]);
    }
  }
  return pcm;
}

bool decodeAdpsFile(const std::vector<uint8_t> &d, AdpsTrack &out) {
  out = AdpsTrack{};
  if (d.size() < 12)
    return false;
  const uint32_t introSize = rd32(&d[0]);
  auto block = [&](size_t at, Pcm &pcm) {
    if (at + 4 > d.size())
      return false;
    const size_t n = std::min<size_t>(rd32(&d[at]), d.size() - at - 4);
    pcm = decodeAdps(&d[at + 4], n);
    return true;
  };
  if (!block(8, out.intro))
    return false;
  if (size_t(8) + introSize < d.size())
    block(8 + size_t(introSize), out.loop);
  return true;
}

Pcm decodeAdp4(const std::vector<uint8_t> &d) {
  Pcm pcm;
  pcm.channels = 1;
  if (d.size() < 4)
    return pcm;
  const uint32_t units = rd32(&d[0]);
  pcm.samples.reserve(size_t(units) * 2);
  size_t p = 4;
  uint32_t done = 0, flags = 0;
  int state = 0, pred = 0;
  uint8_t key = 0;
  while (done < units && p < d.size()) {
    if (flags < 0x100)
      flags = d[p++] | 0x8000u;
    if (p >= d.size())
      break;
    uint32_t len = d[p++];
    if (len < 0x80) {
      if (p >= d.size())
        break;
      len = (len << 8) | d[p++];
    } else {
      len &= 0x7F;
    }
    const uint32_t run = len + 1;
    done += run;
    if (flags & 1) {
      state = pred = 0;
      pcm.samples.insert(pcm.samples.end(), size_t(run) * 2, 0);
    } else {
      for (uint32_t i = 0; i < run && p < d.size(); ++i) {
        const uint8_t by = d[p++] ^ key++;
        for (int n = 0; n < 2; ++n) {
          const int k = state + (n ? by >> 4 : by & 15);
          pred = std::clamp(pred + kAdpsDelta[k], -0x7FFF, 0x7FFF);
          state = kAdpsNext[k];
          pcm.samples.push_back(static_cast<int16_t>(pred));
        }
      }
    }
    flags >>= 1;
  }
  return pcm;
}

} // namespace SnowRadish
