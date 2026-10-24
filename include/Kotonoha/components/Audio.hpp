#pragma once

#include <Kotonoha/components/MediaBufferInfo.hpp>
#include <Kotonoha/components/Sound.hpp>
#include <Kotonoha/renders/AudioRender.h>
#include <Kotonoha/utils/Demuxer.h>
#include <Kotonoha/utils/Time.h>
#include <SDL3/SDL.h>
#include <string>
#include <tuple>
#include <vector>

namespace Kotonoha {

class Audio {
private:
  Kotonoha_audioDecode *AddMediaInternal(const char *path, Uint64 start,
                                         Uint64 end, bool inLoop,
                                         const char *channel,
                                         bool enabledAtRegistration,
                                         Kotonoha_audioDecode *loopMedia);

  std::vector<std::tuple<
      Sound::Channel *,
      std::vector<std::tuple<Kotonoha_audioDecode *, Sound::Channel::Pipe *>>>>
      mediaObjects;
  Sound *soundCtx = nullptr;
  Kotonoha_time *timeCtx = nullptr;
  Kotonoha_Demuxer *demuxer = nullptr;
  double playbackRate = 1.0;
  Kotonoha_audioDecode *previewMedia = nullptr;
  std::string previewPath;

  Kotonoha_audioDecode *CreateDecode(const char *path,
                                     SDL_AudioSpec specification);
  bool IsTracked(const Kotonoha_audioDecode *media) const;

public:
  /* Legacy constructor retained for focused G2.7 tests/tools. */
  Audio(Sound *soundCtx, Kotonoha_time *timeCtx);
  Audio(Sound *soundCtx, Kotonoha_time *timeCtx, Kotonoha_Demuxer *demuxer);

  Kotonoha_audioDecode *AddMedia(const char *path, Uint64 start, Uint64 end,
                                 bool inLoop, const char *channel,
                                 bool enabledAtRegistration = true);
  Kotonoha_audioDecode *AddIntroLoopMedia(const char *introPath,
                                          const char *loopPath, Uint64 start,
                                          Uint64 end,
                                          const char *channel = "BGM");
  void SetMediaEnabled(Kotonoha_audioDecode *mediaPtr, bool enabled);
  bool SetPlaybackRate(double requestedRate);
  void RemoveMedia(Kotonoha_audioDecode *mediaPtr);
  std::vector<MediaBufferInfo> GetBufferInfo() const;
  bool PlayNow(const std::string &path, const std::string &channel);
  void StopPreview();
  std::string GetPreviewPath();
  static int RenderMedia(void *data, Uint8 **target, int *size);
  ~Audio();
};

} // namespace Kotonoha
