#pragma once
#include <Kotonoha/components/MediaBufferInfo.hpp>
#include <Kotonoha/Kotonoha.h>
#include <Kotonoha/SceneTime.h>
#include <SDL3_ttf/SDL_ttf.h>

#include <string>
#include <vector>

namespace Kotonoha {
class Audio;
class Video;
class Image;

struct GameplayEventTarget {
  bool present = false;
  Kotonoha_SceneTick tick = 0;
};

// Generic event backend consumed by Gameplay. Script formats and authored
// resource policy belong to the game implementing this interface.
class GameplayEventRuntime {
public:
  virtual ~GameplayEventRuntime() = default;
  virtual void ActivateSubtitles() {}
  virtual bool ProcessNow(void *gameplay, Kotonoha_Game *gameCtx) = 0;
  virtual bool HasFatalError() const { return false; }
  // Rebuild the backend at the Gameplay clock.  A discontinuous seek may ask
  // the game backend not to replay authored side effects/history capture for
  // events crossed only because of that seek.  The meaning of those side
  // effects is game-owned; the engine does not know about a specific log/UI.
  virtual void Reset(void *gameplay, bool suppressReplaySideEffects = false) = 0;
  virtual GameplayEventTarget ComputeSkipToEndTarget(void *gameplay) const = 0;
  virtual bool SetSelectReached() const = 0;
  virtual bool ReachedNext(void *gameplay) const = 0;
  virtual bool CheckEnd(void *gameplay) = 0;
  virtual Uint64 LastTime() const = 0;
  virtual std::vector<MediaBufferInfo>
  GetMediaBufferInfo(const Audio *audio, const Video *video,
                     const Image *image) const = 0;
  virtual bool PlayMediaNow(Audio *audio, const std::string &path,
                            const std::string &channel) const = 0;
  virtual void StopMediaNow(Audio *audio) const = 0;
  virtual std::string GetMediaNowPath(Audio *audio) const = 0;
};

struct GameplayEventRuntimeFactory {
  using Create = GameplayEventRuntime *(*)(const char *scriptPath,
                                            void *gameplay,
                                            Kotonoha_Game *gameCtx,
                                            TTF_Font *font,
                                            void *userdata);
  using Destroy = void (*)(GameplayEventRuntime *runtime, void *userdata);

  Create create = nullptr;
  Destroy destroy = nullptr;
  void *userdata = nullptr;
};

} // namespace Kotonoha
