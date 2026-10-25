#include <Kotonoha/components/Audio.hpp>
#include <Kotonoha/utils/Log.h>

namespace Kotonoha {
namespace {

void AppendAudioBufferInfo(std::vector<MediaBufferInfo> &result,
                           const std::string &channel,
                           const Kotonoha_audioDecode *decode,
                           const char *suffix) {
  if (decode == nullptr) return;
  MediaBufferInfo info;
  info.id = reinterpret_cast<Uint64>(decode);
  info.type = suffix != nullptr ? "Audio Loop" : "Audio";
  info.channel = channel;
  if (decode->stream != nullptr && decode->stream->filename != nullptr)
    info.path = decode->stream->filename;
  if (decode->stateMutex != nullptr) {
    SDL_LockMutex(decode->stateMutex);
    info.playedMs = decode->lastTime;
    SDL_UnlockMutex(decode->stateMutex);
  }
  if (decode->stream != nullptr) {
    info.totalMs = static_cast<Uint64>(SDL_GetAtomicInt(
        const_cast<SDL_AtomicInt *>(&decode->stream->totalDurationMs)));
    KotonohaPacketQueueGetBufferStats(&decode->stream->audioQueue,
                                      &info.cachedMs, &info.eof);
  }
  result.push_back(std::move(info));
}

} // namespace

Audio::Audio(Sound *soundCtx, Kotonoha_time *timeCtx)
    : soundCtx(soundCtx), timeCtx(timeCtx) {
  mediaLock = SDL_CreateMutex();
  if (mediaLock == nullptr)
    KLOG_ERROR(AUDIO, "Failed to create Audio media mutex: %s", SDL_GetError());
  previewLock = SDL_CreateMutex();
  if (previewLock == nullptr)
    KLOG_ERROR(AUDIO, "Failed to create Audio preview mutex: %s", SDL_GetError());
}

Audio::Audio(Sound *soundCtx, Kotonoha_time *timeCtx,
             Kotonoha_Demuxer *demuxer)
    : Audio(soundCtx, timeCtx) {
  this->demuxer = demuxer;
}

Kotonoha_audioDecode *Audio::CreateDecode(const char *path,
                                          SDL_AudioSpec specification) {
  Kotonoha_audioDecode *decode = demuxer != nullptr
                                     ? KotonohaAudioInit(path, specification,
                                                         demuxer)
                                     : Kotonoha_AudioInit(path, specification);
  if (decode != nullptr &&
      !KotonohaAudioSetPlaybackRate(
          decode, playbackRate.load(std::memory_order_acquire))) {
    KotonohaAudioFree(decode);
    return nullptr;
  }
  return decode;
}

Kotonoha_audioDecode *Audio::AddMediaInternal(
    const char *path, Uint64 start, Uint64 end, bool inLoop,
    const char *channel, bool enabledAtRegistration,
    Kotonoha_audioDecode *loopMedia) {
  if (soundCtx == nullptr || timeCtx == nullptr || path == nullptr ||
      *path == '\0' || channel == nullptr || *channel == '\0') {
    KotonohaAudioFree(loopMedia);
    return nullptr;
  }

  if (mediaLock == nullptr) {
    KotonohaAudioFree(loopMedia);
    return nullptr;
  }
  SDL_LockMutex(mediaLock);

  std::vector<Kotonoha_audioDecode *> finished;
  for (auto &object : mediaObjects) {
    for (auto &entry : std::get<1>(object)) {
      if (std::get<0>(entry) != nullptr &&
          SDL_GetAtomicInt(&std::get<0>(entry)->oneShotDone) != 0)
        finished.push_back(std::get<0>(entry));
    }
  }
  for (auto *done : finished) RemoveMediaUnlocked(done);

  Sound::Channel *targetChannel = soundCtx->GetChannelByName(channel);
  if (targetChannel == nullptr) {
    SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Channel not found: %s", channel);
    KotonohaAudioFree(loopMedia);
    SDL_UnlockMutex(mediaLock);
    return nullptr;
  }

  std::vector<std::tuple<Kotonoha_audioDecode *, Sound::Channel::Pipe *>>
      *pipes = nullptr;
  for (auto &object : mediaObjects) {
    if (std::get<0>(object) == targetChannel) {
      pipes = &std::get<1>(object);
      break;
    }
  }
  if (pipes == nullptr) {
    mediaObjects.emplace_back(
        targetChannel,
        std::vector<
            std::tuple<Kotonoha_audioDecode *, Sound::Channel::Pipe *>>{});
    pipes = &std::get<1>(mediaObjects.back());
  }

  Kotonoha_audioDecode *decode = CreateDecode(path, targetChannel->GetSpecs());
  if (decode == nullptr) {
    KotonohaAudioFree(loopMedia);
    SDL_UnlockMutex(mediaLock);
    return nullptr;
  }
  if (loopMedia != nullptr &&
      !KotonohaAudioSetPlaybackRate(
          loopMedia, playbackRate.load(std::memory_order_acquire))) {
    KotonohaAudioFree(loopMedia);
    KotonohaAudioFree(decode);
    SDL_UnlockMutex(mediaLock);
    return nullptr;
  }

  decode->tm = &timeCtx;
  decode->start = start;
  decode->end = end;
  decode->dataGeneric = this;
  decode->inLoop = inLoop;
  decode->loopMedia = loopMedia;
  SDL_SetAtomicInt(&decode->enabled, enabledAtRegistration ? 1 : 0);

  if (loopMedia != nullptr) {
    loopMedia->tm = &timeCtx;
    loopMedia->start = start;
    loopMedia->end = end;
  }

  /* Pipe construction invokes RenderMedia once. enabled must already reflect
   * PREPARE state so preloaded profile media is guaranteed silent. */
  Sound::Channel::Pipe *pipe =
      targetChannel->AddPipe(RenderMedia, KotonohaAudioFree, decode);
  if (pipe == nullptr) {
    KotonohaAudioFree(decode);
    SDL_UnlockMutex(mediaLock);
    return nullptr;
  }

  pipes->emplace_back(decode, pipe);
  SDL_UnlockMutex(mediaLock);
  return decode;
}

Kotonoha_audioDecode *Audio::AddMedia(const char *path, Uint64 start,
                                      Uint64 end, bool inLoop,
                                      const char *channel,
                                      bool enabledAtRegistration) {
  return AddMediaInternal(path, start, end, inLoop, channel,
                          enabledAtRegistration, nullptr);
}

Kotonoha_audioDecode *Audio::AddIntroLoopMedia(const char *introPath,
                                               const char *loopPath,
                                               Uint64 start, Uint64 end,
                                               const char *channelName) {
  if (introPath == nullptr || *introPath == '\0' || loopPath == nullptr ||
      *loopPath == '\0' || channelName == nullptr || *channelName == '\0' ||
      soundCtx == nullptr)
    return nullptr;

  Sound::Channel *channel = soundCtx->GetChannelByName(channelName);
  if (channel == nullptr)
    return nullptr;

  /* Resolve both halves before exposing a pipe. There is deliberately no
   * loop-only fallback when the intro/loop pair is incomplete. */
  Kotonoha_audioDecode *loop = CreateDecode(loopPath, channel->GetSpecs());
  if (loop == nullptr)
    return nullptr;

  return AddMediaInternal(introPath, start, end, false, channelName, false, loop);
}

void Audio::SetMediaEnabled(Kotonoha_audioDecode *mediaPtr, bool enabled) {
  if (mediaPtr == nullptr || mediaLock == nullptr) return;

  // Event-state tables retain raw decode identities.  A completed one-shot can
  // be reaped by AddMediaInternal on another thread; validate membership while
  // holding the same lock that owns mediaObjects before dereferencing it.
  SDL_LockMutex(mediaLock);
  if (IsTrackedUnlocked(mediaPtr)) {
    SDL_SetAtomicInt(&mediaPtr->enabled, enabled ? 1 : 0);
  }
  SDL_UnlockMutex(mediaLock);
}

bool Audio::SetPlaybackRate(double requestedRate) {
  if (!KotonohaAudioRateIsSupported(requestedRate))
    return false;
  const double previousRate = playbackRate.load(std::memory_order_acquire);
  if (previousRate == requestedRate)
    return true;
  if (mediaLock == nullptr) return false;
  SDL_LockMutex(mediaLock);

  const auto restorePreviousRate = [&]() {
    // A rate change spans every decode owned by this Gameplay.  If any one
    // update fails, restore the complete set before releasing mediaLock so the
    // audio graph is never externally observable in a mixed-rate state.
    for (auto &rollbackObject : mediaObjects) {
      Sound::Channel *rollbackChannel = std::get<0>(rollbackObject);
      if (rollbackChannel == nullptr || rollbackChannel->lockPipes == nullptr)
        continue;
      SDL_LockMutex(rollbackChannel->lockPipes);
      for (auto &rollbackEntry : std::get<1>(rollbackObject)) {
        Kotonoha_audioDecode *rollbackDecode = std::get<0>(rollbackEntry);
        if (rollbackDecode == nullptr) continue;
        (void)KotonohaAudioSetPlaybackRate(rollbackDecode, previousRate);
        if (rollbackDecode->loopMedia != nullptr) {
          (void)KotonohaAudioSetPlaybackRate(rollbackDecode->loopMedia,
                                             previousRate);
        }
      }
      SDL_UnlockMutex(rollbackChannel->lockPipes);
    }
  };

  for (auto &object : mediaObjects) {
    Sound::Channel *channel = std::get<0>(object);
    if (channel == nullptr || channel->lockPipes == nullptr)
      continue;

    SDL_LockMutex(channel->lockPipes);
    for (auto &entry : std::get<1>(object)) {
      Kotonoha_audioDecode *decode = std::get<0>(entry);
      if (decode == nullptr)
        continue;
      if (!KotonohaAudioSetPlaybackRate(decode, requestedRate)) {
        SDL_UnlockMutex(channel->lockPipes);
        restorePreviousRate();
        SDL_UnlockMutex(mediaLock);
        return false;
      }
      if (decode->loopMedia != nullptr &&
          !KotonohaAudioSetPlaybackRate(decode->loopMedia,
                                                  requestedRate)) {
        SDL_UnlockMutex(channel->lockPipes);
        restorePreviousRate();
        SDL_UnlockMutex(mediaLock);
        return false;
      }
    }
    SDL_UnlockMutex(channel->lockPipes);
  }

  playbackRate.store(requestedRate, std::memory_order_release);
  SDL_UnlockMutex(mediaLock);
  return true;
}

void Audio::RemoveMediaUnlocked(Kotonoha_audioDecode *mediaPtr) {
  for (auto mediaIt = mediaObjects.begin(); mediaIt != mediaObjects.end();) {
    Sound::Channel *channel = std::get<0>(*mediaIt);
    auto &pipes = std::get<1>(*mediaIt);

    for (auto pipeIt = pipes.begin(); pipeIt != pipes.end();) {
      Kotonoha_audioDecode *decode = std::get<0>(*pipeIt);
      Sound::Channel::Pipe *pipe = std::get<1>(*pipeIt);

      if (mediaPtr == nullptr || decode == mediaPtr) {
        // Invalidate preview identity before Channel::RemovePipe frees decode.
        // Without this, a later allocation can reuse the address and turn the
        // stale preview pointer into an ABA match for unrelated media.
        Kotonoha_audioDecode *expected = decode;
        (void)previewMedia.compare_exchange_strong(
            expected, nullptr, std::memory_order_acq_rel,
            std::memory_order_acquire);
        if (channel != nullptr && pipe != nullptr)
          channel->RemovePipe(pipe); /* owns/frees decode */
        pipeIt = pipes.erase(pipeIt);
      } else {
        ++pipeIt;
      }
    }

    if (pipes.empty())
      mediaIt = mediaObjects.erase(mediaIt);
    else
      ++mediaIt;
  }
}

void Audio::RemoveMedia(Kotonoha_audioDecode *mediaPtr) {
  if (mediaLock == nullptr) return;
  SDL_LockMutex(mediaLock);
  RemoveMediaUnlocked(mediaPtr);
  SDL_UnlockMutex(mediaLock);
}

int Audio::RenderMedia(void *data, Uint8 **target, int *size) {
  if (data == nullptr || target == nullptr || size == nullptr)
    return 0;

  auto *instance = static_cast<Kotonoha_audioDecode *>(data);
  if (SDL_GetAtomicInt(&instance->enabled) == 0)
    return 0;
  if (instance->tm == nullptr || *instance->tm == nullptr)
    return 0;

  if (instance->start == 0 && instance->end == 0) {
    if (SDL_GetAtomicInt(&instance->oneShotDone) != 0) return 0;
    int result = KotonohaAudioRender(instance, target, size);
    if (result == 1 && instance->inLoop) {
      KotonohaAudioSeek(instance, 0);
      instance->cannotSeek = false;
      return KotonohaAudioRender(instance, target, size);
    }
    if (result == 1 || result == -1) {
      SDL_SetAtomicInt(&instance->oneShotDone, 1);
      return 0;
    }
    return result;
  }

  bool inRange = false;
  Sint64 diff = 0;
  instance->targetTime = KotonohaTimeGetFromEvent(
      *instance->tm, instance->start, instance->end, &inRange, &diff);
  if (!inRange)
    return 0;

  Kotonoha_audioDecode *current =
      instance->playingLoop ? instance->loopMedia : instance;
  if (current == nullptr)
    return 0;

  int result = KotonohaAudioRender(current, target, size);

  if (result == 1 && instance->loopMedia != nullptr) {
    /* Intro->loop and loop repetition are driven only by decoder EOF. */
    instance->playingLoop = true;
    KotonohaAudioSeek(instance->loopMedia, 0);
    instance->loopMedia->targetTime = 0;
    result = KotonohaAudioRender(instance->loopMedia, target, size);
  } else if (result == 1 && instance->inLoop && instance->loopMedia == nullptr) {
    /* Looping SE likewise restarts only at decoder EOF, never on starvation or
     * decode errors. */
    KotonohaAudioSeek(instance, 0);
    instance->targetTime = 0;
    result = KotonohaAudioRender(instance, target, size);
  }

  return result;
}

std::vector<MediaBufferInfo> Audio::GetBufferInfo() const {
  std::vector<MediaBufferInfo> result;
  if (mediaLock == nullptr) return result;
  SDL_LockMutex(mediaLock);
  for (const auto &object : mediaObjects) {
    const Sound::Channel *channel = std::get<0>(object);
    const std::string channelName = channel != nullptr ? channel->name : "";
    for (const auto &entry : std::get<1>(object)) {
      const auto *decode = std::get<0>(entry);
      AppendAudioBufferInfo(result, channelName, decode, nullptr);
      if (decode != nullptr && decode->loopMedia != nullptr)
        AppendAudioBufferInfo(result, channelName, decode->loopMedia, " (loop)");
    }
  }
  SDL_UnlockMutex(mediaLock);
  return result;
}

bool Audio::PlayNow(const std::string &path, const std::string &channel) {
  if (path.empty() || channel.empty() || previewLock == nullptr ||
      mediaLock == nullptr)
    return false;
  SDL_LockMutex(previewLock);
  SDL_LockMutex(mediaLock);
  Kotonoha_audioDecode *previous =
      previewMedia.load(std::memory_order_acquire);
  if (previous != nullptr && IsTrackedUnlocked(previous)) {
    RemoveMediaUnlocked(previous);
  }
  SDL_UnlockMutex(mediaLock);
  previewMedia.store(nullptr, std::memory_order_release);
  previewPath.clear();
  Kotonoha_audioDecode *created =
      AddMedia(path.c_str(), 0, 0, false, channel.c_str());
  if (created == nullptr) {
    SDL_UnlockMutex(previewLock);
    return false;
  }
  previewMedia.store(created, std::memory_order_release);
  previewPath = path;
  SDL_UnlockMutex(previewLock);
  return true;
}

bool Audio::IsTrackedUnlocked(const Kotonoha_audioDecode *media) const {
  for (const auto &object : mediaObjects)
    for (const auto &entry : std::get<1>(object))
      if (std::get<0>(entry) == media) return true;
  return false;
}

bool Audio::IsTracked(const Kotonoha_audioDecode *media) const {
  if (mediaLock == nullptr) return false;
  SDL_LockMutex(mediaLock);
  const bool tracked = IsTrackedUnlocked(media);
  SDL_UnlockMutex(mediaLock);
  return tracked;
}

void Audio::StopPreview() {
  if (previewLock == nullptr || mediaLock == nullptr) return;
  SDL_LockMutex(previewLock);
  SDL_LockMutex(mediaLock);
  Kotonoha_audioDecode *current = previewMedia.load(std::memory_order_acquire);
  if (current != nullptr && IsTrackedUnlocked(current))
    RemoveMediaUnlocked(current);
  SDL_UnlockMutex(mediaLock);
  previewMedia.store(nullptr, std::memory_order_release);
  previewPath.clear();
  SDL_UnlockMutex(previewLock);
}

std::string Audio::GetPreviewPath() {
  if (previewLock == nullptr || mediaLock == nullptr) return {};
  SDL_LockMutex(previewLock);
  Kotonoha_audioDecode *current = previewMedia.load(std::memory_order_acquire);
  if (current == nullptr) {
    previewPath.clear();
    SDL_UnlockMutex(previewLock);
    return {};
  }

  // previewLock protects the handle/path pair, but the events worker may reap
  // a completed one-shot under mediaLock.  Keep mediaLock held across both the
  // membership check and the oneShotDone dereference so a successful lookup
  // cannot turn into a use-after-free before the flag is read.
  SDL_LockMutex(mediaLock);
  current = previewMedia.load(std::memory_order_acquire);
  const bool tracked = current != nullptr && IsTrackedUnlocked(current);
  const bool completed =
      tracked && SDL_GetAtomicInt(&current->oneShotDone) != 0;
  if (!tracked || completed) {
    previewMedia.store(nullptr, std::memory_order_release);
    previewPath.clear();
  }
  SDL_UnlockMutex(mediaLock);

  const std::string result = previewPath;
  SDL_UnlockMutex(previewLock);
  return result;
}

Audio::~Audio() {
  StopPreview();
  RemoveMedia(nullptr);
  if (mediaLock != nullptr) {
    SDL_DestroyMutex(mediaLock);
    mediaLock = nullptr;
  }
  if (previewLock != nullptr) {
    SDL_DestroyMutex(previewLock);
    previewLock = nullptr;
  }
}

} // namespace Kotonoha
