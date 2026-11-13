#include <SnowRadish/SnowAssets.hpp>
#include <iostream>
#include <stdexcept>
#include <cstring>

static uint32_t word(const std::vector<uint8_t> &v, size_t p) {
  if (p + 4 > v.size()) throw std::runtime_error("Truncated audio header");
  return uint32_t(v[p]) | uint32_t(v[p+1]) << 8 |
         uint32_t(v[p+2]) << 16 | uint32_t(v[p+3]) << 24;
}
int main(int argc, char **argv) {
  try {
    if (argc != 2) throw std::runtime_error("Usage: PmailAudioCheck PMAILDVD");
    Kotonoha::Providers::SnowAssets assets(argv[1]);
    if (assets.sampleRate() != 44100) throw std::runtime_error("PMAIL rate is not 44100 Hz");
    size_t music = 0, effects = 0, voices = 0, empty = 0;
    for (const auto &prefix : {"sound/", "voice/"}) {
      for (const auto &id : assets.list(prefix)) {
        auto raw = assets.read(id);
        if (raw.size() >= 4 && word(raw, 0) == 0 && id.rfind("voice/", 0) == 0) {
          if (raw.size() != 4 || !assets.loadAudio(id).data.empty())
            throw std::runtime_error("Invalid empty voice: " + id);
          ++empty;
          continue;
        }
        auto audio = assets.loadAudio(id);
        if (audio.spec.freq != 44100 || audio.spec.format != SDL_AUDIO_S16 || audio.data.empty())
          throw std::runtime_error("Invalid PCM: " + id);
        if (id.rfind("sound/mm", 0) == 0) {
          SnowRadish::AdpsTrack track;
          if (!assets.Adps(id.substr(6), track) || audio.spec.channels != 2)
            throw std::runtime_error("Invalid music: " + id);
          if (audio.data.size() != 2 * (track.intro.samples.size() + track.loop.samples.size()))
            throw std::runtime_error("Wrong music size: " + id);
          ++music;
        } else {
          if (audio.spec.channels != 1 || audio.data.size() != size_t(word(raw, 0)) * 4)
            throw std::runtime_error("Wrong ADP4 size/channels: " + id);
          if (id.rfind("voice/", 0) == 0) ++voices; else ++effects;
        }
      }
    }
    if (!music || !effects || !voices) throw std::runtime_error("Missing audio category");
    std::cout << "Verified " << music << " music, " << effects << " effects, "
              << voices << " voices, " << empty << " empty voices: PCM sizes, formats, channels and 44100 Hz.\n";
    return 0;
  } catch (const std::exception &e) {
    std::cerr << e.what() << '\n'; return 1;
  }
}
