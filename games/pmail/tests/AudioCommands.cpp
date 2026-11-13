#include "PmailAudio.hpp"
#include <SDL3/SDL.h>
#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>

using Kotonoha::PmailPort::PmailAudio;
static void require(bool value, const char *message) {
  if (!value)
    throw std::runtime_error(message);
}
int main(int argc, char **argv) {
  try {
    require(argc == 2, "Usage: PmailAudioCommandsCheck PMAILDVD");
    require(SDL_Init(SDL_INIT_AUDIO), SDL_GetError());
    {
      auto assets = std::make_shared<Kotonoha::Providers::SnowAssets>(argv[1]);
      Kotonoha::AudioMixer mixer;
      PmailAudio audio(mixer, assets);
      require(PmailAudio::FadeInterval(0) == 43 &&
                  PmailAudio::FadeInterval(1) == 21 &&
                  PmailAudio::FadeInterval(2) == 86 &&
                  PmailAudio::FadeInterval(3) == 10 &&
                  PmailAudio::FadeInterval(4) == 172 &&
                  PmailAudio::FadeInterval(99) == 43,
              "Original fade mode interval mapping");
      audio.Handle(112, {27, 0}, {});
      require(!audio.Snapshot()[0].loop, "SYS112(27,0) must not loop");
      audio.Handle(112, {9, 1}, {});
      require(audio.Snapshot()[0].loop,
              "SYS112(9,1) must repeat the loop block");
      audio.Handle(96, {1}, {"V35030001"});
      require(audio.SePlaying() && !audio.VoicePlaying(),
              "VOICE.DAT sound must use SE bus");
      audio.Handle(128, {2, 0}, {"VA0078000", ""});
      require(audio.VoicePlaying(), "SYS128 must start independent voice");
      audio.Handle(99, {}, {});
      require(!audio.SePlaying() && audio.VoicePlaying(),
              "SYS99 must not stop voice");
      audio.Handle(97, {1, 4}, {"V35030001"});
      auto fadeIn = audio.Snapshot()[1];
      require(fadeIn.playing && fadeIn.gain == 0 && fadeIn.fadeTarget == 1 &&
                  fadeIn.fadeSeconds > .9,
              "SYS97 must fade in, not start at full gain");
      SDL_Delay(70);
      audio.Update();
      require(audio.Snapshot()[1].gain > 0 && audio.Snapshot()[1].gain < 1,
              "Fade-in must advance between endpoints");
      audio.Handle(98, {1, 4}, {"V35030001"});
      auto crossfade = audio.Snapshot();
      require(crossfade[1].playing && crossfade[3].playing &&
                  crossfade[3].fadeTarget == 0,
              "SYS98 must retain outgoing SE while new SE starts");
      audio.SetPaused(true);
      audio.Restore(crossfade);
      auto restored = audio.Snapshot();
      require(restored[1].id == crossfade[1].id &&
                  restored[3].id == crossfade[3].id && restored[3].playing &&
                  restored[3].fadeSeconds > 0,
              "Save/restore must preserve the outgoing crossfade stream");
      const auto pausedFade = restored[3].fadeSeconds;
      SDL_Delay(30);
      audio.Update();
      require(std::abs(audio.Snapshot()[3].fadeSeconds - pausedFade) < .001,
              "Paused audio must freeze fade clock");
      audio.SetPaused(false);
      audio.Handle(99, {}, {});
      require(!audio.SePlaying(), "SYS99 stops both SE crossfade streams");
      audio.Handle(116, {3}, {});
      require(audio.Snapshot()[0].playing &&
                  audio.Snapshot()[0].fadeTarget == 0,
              "SYS116 must fade BGM without stopping immediately");
      SDL_Delay(100);
      audio.Update();
      require(!audio.Snapshot()[0].playing,
              "Short BGM fade must reach silence");
      audio.Handle(132, {}, {});
      require(!audio.VoicePlaying(), "SYS132 must stop voice");
      auto invalid = audio.Snapshot();
      invalid[0].seconds = std::numeric_limits<double>::quiet_NaN();
      bool rejected = false;
      try {
        audio.Restore(invalid);
      } catch (const std::runtime_error &) {
        rejected = true;
      }
      require(rejected, "Invalid saved audio position must be rejected");
      audio.StopAll();
      require(mixer.channel("bgm")->activeStreams() == 0 &&
                  mixer.channel("se")->activeStreams() == 0 &&
                  mixer.channel("voice")->activeStreams() == 0,
              "StopAll must detach SDL streams");
    }
    SDL_Quit();
    std::cout << "Verified audio buses, one-shot BGM, fades, crossfade "
                 "save/restore and pause.\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    SDL_Quit();
    return 1;
  }
}
