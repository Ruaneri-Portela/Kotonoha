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
    : soundCtx(soundCtx), timeCtx(timeCtx) {}

Audio::Audio(Sound *soundCtx, Kotonoha_time *timeCtx,
             Kotonoha_Demuxer *demuxer)
    : soundCtx(soundCtx), timeCtx(timeCtx), demuxer(demuxer) {}

Kotonoha_audioDecode *Audio::CreateDecode(const char *path,
                                          SDL_AudioSpec specification) {
  Kotonoha_audioDecode *decode = demuxer != nullptr
                                     ? KotonohaAudioInit(path, specification,
                                                         demuxer)
                                     : Kotonoha_AudioInit(path, specification);
  if (decode != nullptr &&
      !KotonohaAudioSetPlaybackRate(decode,
                                              playbackRate)) {
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

  std::vector<Kotonoha_audioDecode *> finished;
  for (auto &object : mediaObjects) {
    for (auto &entry : std::get<1>(object)) {
      if (std::get<0>(entry) != nullptr && std::get<0>(entry)->oneShotDone)
        finished.push_back(std::get<0>(entry));
    }
  }
  for (auto *done : finished) RemoveMedia(done);

  Sound::Channel *targetChannel = soundCtx->GetChannelByName(channel);
  if (targetChannel == nullptr) {
    SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Channel not found: %s", channel);
    KotonohaAudioFree(loopMedia);
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
    return nullptr;
  }

  pipes->emplace_back(decode, pipe);
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
  if (mediaPtr != nullptr)
    SDL_SetAtomicInt(&mediaPtr->enabled, enabled ? 1 : 0);
}

bool Audio::SetPlaybackRate(double requestedRate) {
  if (!KotonohaAudioRateIsSupported(requestedRate))
    return false;
  if (playbackRate == requestedRate)
    return true;

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
        return false;
      }
      if (decode->loopMedia != nullptr &&
          !KotonohaAudioSetPlaybackRate(decode->loopMedia,
                                                  requestedRate)) {
        SDL_UnlockMutex(channel->lockPipes);
        return false;
      }
    }
    SDL_UnlockMutex(channel->lockPipes);
  }

  playbackRate = requestedRate;
  return true;
}

void Audio::RemoveMedia(Kotonoha_audioDecode *mediaPtr) {
  for (auto mediaIt = mediaObjects.begin(); mediaIt != mediaObjects.end();) {
    Sound::Channel *channel = std::get<0>(*mediaIt);
    auto &pipes = std::get<1>(*mediaIt);

    for (auto pipeIt = pipes.begin(); pipeIt != pipes.end();) {
      Kotonoha_audioDecode *decode = std::get<0>(*pipeIt);
      Sound::Channel::Pipe *pipe = std::get<1>(*pipeIt);

      if (mediaPtr == nullptr || decode == mediaPtr) {
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

int Audio::RenderMedia(void *data, Uint8 **target, int *size) {
  if (data == nullptr || target == nullptr || size == nullptr)
    return 0;

  auto *instance = static_cast<Kotonoha_audioDecode *>(data);
  if (SDL_GetAtomicInt(&instance->enabled) == 0)
    return 0;
  if (instance->tm == nullptr || *instance->tm == nullptr)
    return 0;

  if (instance->start == 0 && instance->end == 0) {
    if (instance->oneShotDone) return 0;
    int result = KotonohaAudioRender(instance, target, size);
    if (result == 1 && instance->inLoop) {
      KotonohaAudioSeek(instance, 0);
      instance->cannotSeek = false;
      return KotonohaAudioRender(instance, target, size);
    }
    if (result == 1 || result == -1) {
      instance->oneShotDone = true;
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
  return result;
}

bool Audio::PlayNow(const std::string &path, const std::string &channel) {
  if (path.empty() || channel.empty()) return false;
  StopPreview();
  previewMedia = AddMedia(path.c_str(), 0, 0, false, channel.c_str());
  if (previewMedia == nullptr) return false;
  previewPath = path;
  return true;
}

bool Audio::IsTracked(const Kotonoha_audioDecode *media) const {
  for (const auto &object : mediaObjects)
    for (const auto &entry : std::get<1>(object))
      if (std::get<0>(entry) == media) return true;
  return false;
}

void Audio::StopPreview() {
  if (previewMedia != nullptr && IsTracked(previewMedia))
    RemoveMedia(previewMedia);
  previewMedia = nullptr;
  previewPath.clear();
}

std::string Audio::GetPreviewPath() {
  if (previewMedia == nullptr) return {};
  if (!IsTracked(previewMedia) || previewMedia->oneShotDone) {
    previewMedia = nullptr;
    previewPath.clear();
  }
  return previewPath;
}

Audio::~Audio() {
  previewMedia = nullptr;
  RemoveMedia(nullptr);
}

} // namespace Kotonoha
