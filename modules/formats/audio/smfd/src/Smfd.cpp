#include <Kotonoha/Formats/Smfd.hpp>

#include "../../internal/ByteReader.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>

namespace Kotonoha::Formats::Smfd {
namespace {

using Kotonoha::Formats::readU16LE;
using Kotonoha::Formats::readU32LE;

void writeVlq(std::vector<uint8_t> &out, uint32_t value) {
  uint8_t buf[5];
  int count = 0;
  buf[count++] = static_cast<uint8_t>(value & 0x7F);
  value >>= 7;
  while (value > 0) {
    buf[count++] = static_cast<uint8_t>((value & 0x7F) | 0x80);
    value >>= 7;
  }
  for (int i = count - 1; i >= 0; --i)
    out.push_back(buf[i]);
}

struct SmfdEvent {
  uint64_t tick = 0;
  uint8_t status = 0;
  uint8_t d1 = 0;
  uint8_t d2 = 0;
  uint32_t tempoUs = 0; // non-zero for tempo meta event
};

bool parseSmfdEvents(const uint8_t *data, size_t size, uint16_t &timeDiv,
                     std::vector<SmfdEvent> &events, uint64_t &totalTicks) {
  events.clear();
  totalTicks = 0;
  if (!data || size < 4)
    return false;
  timeDiv = readU16LE(data);
  if (timeDiv == 0)
    timeDiv = 480;

  size_t pos = 2;
  uint64_t tick = 0;
  while (pos < size) {
    uint32_t dt = data[pos++];
    if (dt >= 0x80) {
      if (pos >= size)
        break;
      dt = ((dt << 8) | data[pos++]) & 0x7FFF;
    }
    tick += dt;
    if (pos >= size)
      break;
    const uint8_t st = data[pos++];
    if (st >= 0xF2) {
      totalTicks = tick;
      return true;
    }
    if (st == 0xF0) {
      // No-op delay accumulator
      continue;
    }
    if (st == 0xF1) {
      if (pos + 3 > size)
        break;
      const uint8_t hi = data[pos];
      const uint16_t lo = readU16LE(data + pos + 1);
      pos += 3;
      SmfdEvent ev;
      ev.tick = tick;
      ev.status = 0xF1;
      ev.tempoUs = (static_cast<uint32_t>(hi) << 16) | lo;
      events.push_back(ev);
      continue;
    }
    if (st >= 0xC0 && st <= 0xDF) {
      if (pos + 1 > size)
        break;
      SmfdEvent ev;
      ev.tick = tick;
      ev.status = st;
      ev.d1 = data[pos++];
      events.push_back(ev);
      continue;
    }
    if ((st >= 0x80 && st <= 0xBF) || (st >= 0xE0 && st <= 0xEF)) {
      if (pos + 2 > size)
        break;
      SmfdEvent ev;
      ev.tick = tick;
      ev.status = st;
      ev.d1 = data[pos++];
      ev.d2 = data[pos++];
      events.push_back(ev);
      continue;
    }
    // Unexpected byte
    break;
  }
  totalTicks = tick;
  return !events.empty();
}
} // namespace

std::vector<uint8_t> smfdToStandardMidi(const uint8_t *data, size_t size) {
  uint16_t timeDiv = 480;
  std::vector<SmfdEvent> events;
  uint64_t totalTicks = 0;
  if (!parseSmfdEvents(data, size, timeDiv, events, totalTicks))
    return {};

  std::vector<uint8_t> trk;
  trk.reserve(size + 64);

  // GM System On SysEx at tick 0: F0 05 7E 7F 09 01 F7
  trk.push_back(0x00);
  trk.push_back(0xF0);
  trk.push_back(0x05);
  trk.push_back(0x7E);
  trk.push_back(0x7F);
  trk.push_back(0x09);
  trk.push_back(0x01);
  trk.push_back(0xF7);

  uint64_t prevTick = 0;
  for (const auto &ev : events) {
    const uint32_t delta = static_cast<uint32_t>(ev.tick - prevTick);
    prevTick = ev.tick;
    writeVlq(trk, delta);
    if (ev.status == 0xF1) {
      trk.push_back(0xFF);
      trk.push_back(0x51);
      trk.push_back(0x03);
      trk.push_back(static_cast<uint8_t>((ev.tempoUs >> 16) & 0xFF));
      trk.push_back(static_cast<uint8_t>((ev.tempoUs >> 8) & 0xFF));
      trk.push_back(static_cast<uint8_t>(ev.tempoUs & 0xFF));
    } else if (ev.status >= 0xC0 && ev.status <= 0xDF) {
      trk.push_back(ev.status);
      trk.push_back(ev.d1);
    } else {
      trk.push_back(ev.status);
      trk.push_back(ev.d1);
      trk.push_back(ev.d2);
    }
  }

  // End of track
  writeVlq(trk, static_cast<uint32_t>(
                    totalTicks > prevTick ? totalTicks - prevTick : 0));
  trk.push_back(0xFF);
  trk.push_back(0x2F);
  trk.push_back(0x00);

  std::vector<uint8_t> smf;
  smf.reserve(14 + 8 + trk.size());
  // MThd
  smf.insert(smf.end(), {'M', 'T', 'h', 'd', 0, 0, 0, 6, 0, 0, 0, 1,
                         static_cast<uint8_t>((timeDiv >> 8) & 0xFF),
                         static_cast<uint8_t>(timeDiv & 0xFF)});
  // MTrk
  const uint32_t trkLen = static_cast<uint32_t>(trk.size());
  smf.insert(smf.end(),
             {'M', 'T', 'r', 'k', static_cast<uint8_t>((trkLen >> 24) & 0xFF),
              static_cast<uint8_t>((trkLen >> 16) & 0xFF),
              static_cast<uint8_t>((trkLen >> 8) & 0xFF),
              static_cast<uint8_t>(trkLen & 0xFF)});
  smf.insert(smf.end(), trk.begin(), trk.end());
  return smf;
}

DecodedPcm decodeSmfdToPcm(const uint8_t *data, size_t size, int sampleRate) {
  DecodedPcm clip;
  uint16_t timeDiv = 480;
  std::vector<SmfdEvent> events;
  uint64_t totalTicks = 0;
  if (!parseSmfdEvents(data, size, timeDiv, events, totalTicks) ||
      sampleRate <= 0) {
    return clip;
  }

  // Convert event ticks into sample positions using tempo map.
  struct TimedEvent {
    size_t samplePos = 0;
    SmfdEvent ev;
  };
  std::vector<TimedEvent> timed;
  timed.reserve(events.size());

  double curTempoUs = 500000.0;
  uint64_t lastTick = 0;
  double curSample = 0.0;

  for (const auto &ev : events) {
    const uint64_t dt = ev.tick - lastTick;
    curSample += (static_cast<double>(dt) * curTempoUs / 1000000.0) *
                 (static_cast<double>(sampleRate) / timeDiv);
    lastTick = ev.tick;
    if (ev.status == 0xF1 && ev.tempoUs > 0) {
      curTempoUs = static_cast<double>(ev.tempoUs);
    } else {
      timed.push_back({static_cast<size_t>(curSample), ev});
    }
  }
  if (totalTicks > lastTick) {
    curSample +=
        (static_cast<double>(totalTicks - lastTick) * curTempoUs / 1000000.0) *
        (static_cast<double>(sampleRate) / timeDiv);
  }

  // Cap single-pass synthesis at 10 minutes to guard against corrupt files.
  const size_t totalFrames =
      std::min<size_t>(static_cast<size_t>(curSample) + sampleRate / 2,
                       static_cast<size_t>(sampleRate) * 600);
  if (totalFrames == 0)
    return clip;

  struct ChannelState {
    uint8_t program = 0;
    float volume = 100.0f / 127.0f;
    float expression = 1.0f;
    float pan = 0.5f; // 0 = L, 1 = R
    float pitchBendSemis = 0.0f;
  };
  std::array<ChannelState, 16> ch{};

  struct Voice {
    bool active = false;
    bool released = false;
    bool percussion = false;
    uint8_t channel = 0;
    uint8_t note = 0;
    uint8_t program = 0;
    float vel = 0.0f;
    float phase = 0.0f;
    float env = 0.0f;
    float attackRate = 0.02f;
    float decayRate = 0.0002f;
    float sustain = 0.5f;
    float releaseRate = 0.002f;
    uint32_t rng = 0x12345678u;
  };
  constexpr size_t kMaxVoices = 32;
  std::array<Voice, kMaxVoices> voices{};

  auto noteFreq = [](float midiNote) -> float {
    return 440.0f * std::pow(2.0f, (midiNote - 69.0f) / 12.0f);
  };

  auto triggerNote = [&](uint8_t channel, uint8_t note, uint8_t vel) {
    if (vel == 0) {
      for (auto &v : voices) {
        if (v.active && v.channel == channel && v.note == note)
          v.released = true;
      }
      return;
    }
    Voice *slot = nullptr;
    for (auto &v : voices) {
      if (!v.active) {
        slot = &v;
        break;
      }
    }
    if (!slot) {
      // Steal quietest voice
      slot = &voices[0];
      for (auto &v : voices) {
        if (v.env < slot->env)
          slot = &v;
      }
    }
    Voice v{};
    v.active = true;
    v.released = false;
    v.percussion = (channel == 9);
    v.channel = channel;
    v.note = note;
    v.program = ch[channel].program;
    v.vel = static_cast<float>(vel) / 127.0f;
    v.phase = 0.0f;
    v.env = 0.001f;
    v.rng = 0x9E3779B9u ^ (static_cast<uint32_t>(note) << 16) ^
            static_cast<uint32_t>(vel);

    if (v.percussion) {
      v.attackRate = 0.5f;
      v.decayRate = 0.003f;
      v.sustain = 0.0f;
      v.releaseRate = 0.01f;
    } else {
      const uint8_t family = v.program / 8;
      switch (family) {
      case 0: // Piano
      case 1: // Chromatic Percussion
      case 3: // Guitar
        v.attackRate = 0.08f;
        v.decayRate = 0.00018f;
        v.sustain = 0.25f;
        v.releaseRate = 0.0015f;
        break;
      case 2:  // Organ
      case 5:  // Strings
      case 6:  // Ensemble
      case 11: // Synth Pad
        v.attackRate = 0.008f;
        v.decayRate = 0.00005f;
        v.sustain = 0.75f;
        v.releaseRate = 0.0008f;
        break;
      case 4: // Bass
        v.attackRate = 0.06f;
        v.decayRate = 0.00025f;
        v.sustain = 0.45f;
        v.releaseRate = 0.003f;
        break;
      default: // Brass, Reed, Pipe, Synth Lead
        v.attackRate = 0.03f;
        v.decayRate = 0.00012f;
        v.sustain = 0.6f;
        v.releaseRate = 0.002f;
        break;
      }
    }
    *slot = v;
  };

  std::vector<int16_t> pcm(totalFrames * 2, 0);
  size_t evIdx = 0;
  constexpr float kTwoPi = 6.28318530718f;
  const float invRate = 1.0f / static_cast<float>(sampleRate);

  // Process in small 64-frame blocks for speed.
  for (size_t frame = 0; frame < totalFrames; ++frame) {
    while (evIdx < timed.size() && timed[evIdx].samplePos <= frame) {
      const auto &ev = timed[evIdx++].ev;
      const uint8_t type = ev.status & 0xF0;
      const uint8_t c = ev.status & 0x0F;
      if (type == 0x90) {
        triggerNote(c, ev.d1, ev.d2);
      } else if (type == 0x80) {
        triggerNote(c, ev.d1, 0);
      } else if (type == 0xC0) {
        ch[c].program = ev.d1;
      } else if (type == 0xB0) {
        if (ev.d1 == 7)
          ch[c].volume = static_cast<float>(ev.d2) / 127.0f;
        else if (ev.d1 == 11)
          ch[c].expression = static_cast<float>(ev.d2) / 127.0f;
        else if (ev.d1 == 10)
          ch[c].pan = static_cast<float>(ev.d2) / 127.0f;
        else if (ev.d1 == 120 || ev.d1 == 123) {
          for (auto &v : voices) {
            if (v.active && v.channel == c)
              v.released = true;
          }
        }
      } else if (type == 0xE0) {
        const int bend = (static_cast<int>(ev.d2) << 7) | ev.d1;
        ch[c].pitchBendSemis =
            (static_cast<float>(bend - 8192) / 8192.0f) * 2.0f;
      }
    }

    float mixL = 0.0f;
    float mixR = 0.0f;
    for (auto &v : voices) {
      if (!v.active)
        continue;

      // Envelope update
      if (!v.released) {
        if (v.env < 1.0f && v.attackRate > 0.0f) {
          v.env += v.attackRate;
          if (v.env >= 1.0f) {
            v.env = 1.0f;
            v.attackRate = 0.0f; // switch to decay
          }
        } else {
          v.env = std::max(v.sustain, v.env - v.decayRate);
          if (v.percussion && v.env <= 0.0005f) {
            v.active = false;
            continue;
          }
        }
      } else {
        v.env -= v.releaseRate;
        if (v.env <= 0.0005f) {
          v.active = false;
          continue;
        }
      }

      float sample = 0.0f;
      if (v.percussion) {
        // Simple kick vs noise percussion
        if (v.note == 35 || v.note == 36) {
          const float f = 55.0f + 90.0f * v.env;
          v.phase += f * invRate;
          if (v.phase >= 1.0f)
            v.phase -= 1.0f;
          sample = std::sin(v.phase * kTwoPi);
        } else {
          v.rng = v.rng * 1664525u + 1013904223u;
          sample = (static_cast<float>(static_cast<int32_t>(v.rng)) /
                    2147483648.0f) *
                   0.55f;
        }
      } else {
        const float freq =
            noteFreq(static_cast<float>(v.note) + ch[v.channel].pitchBendSemis);
        v.phase += freq * invRate;
        if (v.phase >= 1.0f)
          v.phase -= 1.0f;

        const uint8_t family = v.program / 8;
        if (family == 4) {
          // Bass: warm sine + triangle
          const float tri = 4.0f * std::fabs(v.phase - 0.5f) - 1.0f;
          sample = 0.6f * std::sin(v.phase * kTwoPi) + 0.4f * tri;
        } else if (family == 5 || family == 6 || family == 7 || family == 10) {
          // Strings / Brass / Lead: soft saw + sine
          const float saw = 2.0f * v.phase - 1.0f;
          sample = 0.45f * saw + 0.55f * std::sin(v.phase * kTwoPi);
        } else {
          // Piano / Organ / General: rich harmonic sine
          sample = 0.7f * std::sin(v.phase * kTwoPi) +
                   0.22f * std::sin(v.phase * kTwoPi * 2.0f) +
                   0.08f * std::sin(v.phase * kTwoPi * 3.0f);
        }
      }

      const auto &cs = ch[v.channel];
      const float amp = v.env * v.vel * cs.volume * cs.expression * 0.18f;
      const float panR = cs.pan;
      const float panL = 1.0f - panR;
      mixL += sample * amp * (0.4f + 0.6f * panL);
      mixR += sample * amp * (0.4f + 0.6f * panR);
    }

    // Soft-clip to prevent harsh distortion
    mixL = std::tanh(mixL);
    mixR = std::tanh(mixR);
    pcm[frame * 2 + 0] = static_cast<int16_t>(mixL * 30000.0f);
    pcm[frame * 2 + 1] = static_cast<int16_t>(mixR * 30000.0f);
  }

  clip.samples = std::move(pcm);
  clip.channels = 2;
  clip.sampleRate = sampleRate;
  return clip;
}

} // namespace Kotonoha::Formats::Smfd
