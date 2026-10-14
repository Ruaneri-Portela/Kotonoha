#pragma once
#include <Kotonoha/components/Sound.hpp>
#include <tuple>
#include <vector>

extern "C" {
#include <Kotonoha/renders/AudioRender.h>
#include <Kotonoha/utils/Time.h>
#include <SDL3/SDL.h>
}

namespace Kotonoha {
class Audio {
private:
  Kotonoha_AudioDecode *AddMediaInternal(const char *path, Uint64 start,
                                         Uint64 end, bool inLoop,
                                         const char *channel,
                                         Kotonoha_AudioDecode *loopMedia);
  std::vector<std::tuple<
      Sound::Channel *,
      std::vector<std::tuple<Kotonoha_AudioDecode *, Sound::Channel::Pipe *>>>>
      mediaObjects;
  Sound *soundCtx = nullptr;
  Kotonoha_Time *timeCtx = nullptr;
  Kotonoha_IOMonitor *ioMonitor = nullptr;

public:
  Audio(Sound *soundCtx, Kotonoha_Time *timeCtx, Kotonoha_IOMonitor *ioMonitor);
  Kotonoha_AudioDecode *AddMedia(const char *path, Uint64 start, Uint64 end,
                                 bool inLoop, const char *channel);
  Kotonoha_AudioDecode *AddIntroLoopMedia(const char *introPath,
                                          const char *loopPath, Uint64 start,
                                          Uint64 end, const char *channel);
  void RemoveMedia(Kotonoha_AudioDecode *mediaPtr);
  static int RenderMedia(void *data, Uint8 **target, int *size);
  ~Audio();
};
} // namespace Kotonoha