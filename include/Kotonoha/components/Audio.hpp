#pragma once
#include <Kotonoha/components/MediaBufferInfo.hpp>
#include <Kotonoha/components/Sound.hpp>
#include <string>
#include <tuple>
#include <vector>

#include <Kotonoha/renders/AudioRender.h>
#include <Kotonoha/utils/Time.h>
#include <SDL3/SDL.h>

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
  Kotonoha_Demuxer *demuxer = nullptr;

  // Single audio started from the debug UI; only one preview plays at a time.
  Kotonoha_AudioDecode *previewMedia = nullptr;
  std::string previewPath;

  bool IsTracked(const Kotonoha_AudioDecode *media) const;

public:
  Audio(Sound *soundCtx, Kotonoha_Time *timeCtx, Kotonoha_Demuxer *demuxer);
  Kotonoha_AudioDecode *AddMedia(const char *path, Uint64 start, Uint64 end,
                                 bool inLoop, const char *channel);
  Kotonoha_AudioDecode *AddIntroLoopMedia(const char *introPath,
                                          const char *loopPath, Uint64 start,
                                          Uint64 end, const char *channel);
  void RemoveMedia(Kotonoha_AudioDecode *mediaPtr);
  std::vector<MediaBufferInfo> GetBufferInfo() const;
  // Starts a debug preview, stopping the previous one if it is still playing.
  bool PlayNow(const std::string &path, const std::string &channel);
  void StopPreview();
  // Path of the current preview, or an empty string when none is playing.
  std::string GetPreviewPath();
  static int RenderMedia(void *data, Uint8 **target, int *size);
  ~Audio();
};
} // namespace Kotonoha
