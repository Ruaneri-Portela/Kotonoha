#include <Kotonoha/core/SDL.hpp>
#include <Kotonoha/core/Runtime.hpp>
#include <Kotonoha/media/MediaPlayer.hpp>
#include <algorithm>
#include <cmath>
#include <iostream>
#include <stdexcept>

namespace {
void require(bool ok, const char *message) {
  if (!ok) throw std::runtime_error(message);
}
}
int main(int argc, char **argv) {
  try {
    require(argc == 3, "Expected interleaved video/audio and audio-only fixtures");
    Kotonoha::Runtime runtime("Audio buffering", "1", "org.kotonoha.audio-buffer.tests");
    Kotonoha::SDLSession session(runtime);
    Kotonoha::WindowPtr window(SDL_CreateWindow("Audio buffer", 320, 180, 0));
    Kotonoha::RendererPtr renderer(SDL_CreateRenderer(window.get(), "software"));
    require(bool(renderer), "Create renderer");
    Kotonoha::MediaPlayer player;
    player.open(argv[1], false);
    player.setPaused(true);
    player.update(*renderer, 0);
    require(player.bufferedAudioSeconds() >= 0.3,
            "Video readiness must not stop audio preroll before 300 ms");
    player.setPaused(false);
    double minimum = 1;
    for (int i = 0; i < 80; ++i) {
      player.setPaused(false);
      player.update(*renderer);
      minimum = std::min(minimum, player.bufferedAudioSeconds());
      SDL_Delay(i % 10 == 0 ? 80 : 16);
    }
    require(minimum >= 0.25, "Interleaved audio buffer must stay above 250 ms");
    player.setPaused(true);
    const double paused = player.bufferedAudioSeconds();
    SDL_Delay(100);
    require(std::abs(player.bufferedAudioSeconds() - paused) < 0.02,
            "Pause preserves queued audio");
    player.update(*renderer, 0.5);
    require(player.bufferedAudioSeconds() >= 0.3, "Seek refills audio preroll");
    // A game-driven clock (millisecond ticks plus frame jitter) must not
    // insert silence or drop samples in the continuous audio stream.
    Kotonoha::MediaPlayer game;
    game.open(argv[1], false);
    const Uint64 start = SDL_GetTicksNS();
    for (int i = 0; i < 90; ++i) {
      const double real = (SDL_GetTicksNS() - start) / 1e9;
      const double jitter = ((i * 37) % 9 - 4) * 0.01; // +/- 40 ms
      game.update(*renderer, std::max(0.0, real + jitter));
      require(game.bufferedAudioSeconds() > 0.05, "Jittery clock keeps audio queued");
      SDL_Delay(i % 15 == 0 ? 45 : 16);
    }
    require(game.audioCorrections() == 0,
            "Clock jitter must not realign (chop) the audio stream");
    // Audio-only media is refilled by the audio thread: a long frame hitch
    // (no update calls) must neither starve nor resync the stream.
    Kotonoha::MediaPlayer voice;
    voice.open(argv[2], true);
    voice.update(*renderer, 0);
    const Uint64 voiceStart = SDL_GetTicksNS();
    double voiceMinimum = 1;
    for (int i = 0; i < 12; ++i) {
      SDL_Delay(100); // 1.2 s without update()
      voiceMinimum = std::min(voiceMinimum, voice.bufferedAudioSeconds());
    }
    require(voiceMinimum > 0.1, "Audio thread keeps audio-only media queued during hitches");
    voice.update(*renderer, (SDL_GetTicksNS() - voiceStart) / 1e9);
    require(voice.audioCorrections() == 0 && voice.bufferedAudioSeconds() > 0.1,
            "Timeline after a hitch matches the audible position");
    std::cout << "Audio buffering passed; minimum after refill: " << minimum * 1000 << " ms\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << "Audio buffering: " << error.what() << '\n';
    return 1;
  }
}
