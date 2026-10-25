#pragma once

#include <Kotonoha/components/MediaBufferInfo.hpp>
#include <Kotonoha/components/Sound.hpp>
#include <Kotonoha/renders/AudioRender.h>
#include <Kotonoha/utils/Demuxer.h>
#include <Kotonoha/utils/Time.h>
#include <SDL3/SDL.h>
#include <atomic>
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
  SDL_Mutex *mediaLock = nullptr;
  SDL_Mutex *previewLock = nullptr;
  Sound *soundCtx = nullptr;
  Kotonoha_time *timeCtx = nullptr;
  Kotonoha_Demuxer *demuxer = nullptr;
  std::atomic<double> playbackRate{1.0};
  // Generic media sweeps may retire a preview from the events worker. Keep the
  // identity atomic so retirement can invalidate it before freeing the decode;
  // previewPath itself remains protected by previewLock.
  std::atomic<Kotonoha_audioDecode *> previewMedia{nullptr};
  std::string previewPath;

  Kotonoha_audioDecode *CreateDecode(const char *path,
                                     SDL_AudioSpec specification);
  void RemoveMediaUnlocked(Kotonoha_audioDecode *mediaPtr);
  bool IsTrackedUnlocked(const Kotonoha_audioDecode *media) const;
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
