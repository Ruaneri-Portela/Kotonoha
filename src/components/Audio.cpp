#include <Kotonoha/components/Audio.hpp>
#include <Kotonoha/utils/Log.h>

namespace Kotonoha {
namespace {

void AppendAudioBufferInfo(std::vector<MediaBufferInfo> &result,
                           const std::string &channel,
                           const Kotonoha_AudioDecode *decode,
                           const char *suffix) {
  if (decode == nullptr) {
    return;
  }

  MediaBufferInfo info;
  info.id = reinterpret_cast<Uint64>(decode);
  info.type = suffix != nullptr ? "Audio Loop" : "Audio";
  info.channel = channel;
  if (decode->stream != nullptr && decode->stream->filename != nullptr) {
    info.path = decode->stream->filename;
  }
  (void)suffix;
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

Audio::Audio(Sound *soundCtx, struct Kotonoha_Time *timeCtx,
             Kotonoha_Demuxer *demuxer)
    : soundCtx(soundCtx), timeCtx(timeCtx), demuxer(demuxer) {}

Kotonoha_AudioDecode *Audio::AddMedia(const char *path, Uint64 start,
                                      Uint64 end, bool inLoop,
                                      const char *channel) {
  return AddMediaInternal(path, start, end, inLoop, channel, nullptr);
}

Kotonoha_AudioDecode *
Audio::AddMediaInternal(const char *path, Uint64 start, Uint64 end, bool inLoop,
                        const char *channel,
                        struct Kotonoha_AudioDecode *loopMedia) {
  if (soundCtx == nullptr || timeCtx == nullptr) {
    KLOG_ERROR(AUDIO, "Invalid audio context.");
    KotonohaAudioFree(loopMedia);
    return nullptr;
  }

  if (path == nullptr || *path == '\0') {
    KLOG_ERROR(AUDIO, "Invalid audio path.");
    KotonohaAudioFree(loopMedia);
    return nullptr;
  }

  if (channel == nullptr || *channel == '\0') {
    KLOG_ERROR(AUDIO, "Invalid channel name.");
    KotonohaAudioFree(loopMedia);
    return nullptr;
  }

  std::vector<Kotonoha_AudioDecode *> finished;
  for (auto &object : mediaObjects) {
    for (auto &entry : std::get<1>(object)) {
      if (std::get<0>(entry)->oneShotDone) {
        finished.push_back(std::get<0>(entry));
      }
    }
  }
  for (auto *done : finished) {
    KLOG_DEBUG(AUDIO, "One-shot audio finished; removing from playback list");
    RemoveMedia(done);
  }

  Sound::Channel *targetChannel = soundCtx->GetChannelByName(channel);
  if (targetChannel == nullptr) {
    KLOG_ERROR(AUDIO, "Channel not found: %s", channel);
    KotonohaAudioFree(loopMedia);
    return nullptr;
  }

  std::vector<std::tuple<Kotonoha_AudioDecode *, Sound::Channel::Pipe *>>
      *audioDecodesPipe = nullptr;

  for (auto &object : mediaObjects) {
    if (std::get<0>(object) == targetChannel) {
      audioDecodesPipe = &std::get<1>(object);
      break;
    }
  }

  if (audioDecodesPipe == nullptr) {
    mediaObjects.emplace_back(
        targetChannel,
        std::vector<
            std::tuple<Kotonoha_AudioDecode *, Sound::Channel::Pipe *>>{});
    audioDecodesPipe = &std::get<1>(mediaObjects.back());
  }

  Kotonoha_AudioDecode *newAudioDecode =
      KotonohaAudioInit(path, targetChannel->GetSpecs(), demuxer);
  if (newAudioDecode == nullptr) {
    KotonohaAudioFree(loopMedia);
    return nullptr;
  }

  newAudioDecode->tm = &timeCtx;
  newAudioDecode->start = start;
  newAudioDecode->end = end;
  newAudioDecode->dataGeneric = static_cast<void *>(this);
  newAudioDecode->inLoop = inLoop;
  newAudioDecode->loopMedia = loopMedia;
  if (start == 0 && end == 0) {
    KLOG_DEBUG(AUDIO, "One-shot audio on channel '%s': %s", channel, path);
  }

  Sound::Channel::Pipe *newPipe =
      targetChannel->AddPipe(RenderMedia, KotonohaAudioFree, newAudioDecode);

  if (newPipe == nullptr) {
    KLOG_ERROR(AUDIO, "Failed to create audio pipe: %s", path);
    KotonohaAudioFree(newAudioDecode);
    return nullptr;
  }

  audioDecodesPipe->emplace_back(newAudioDecode, newPipe);
  return newAudioDecode;
}

struct Kotonoha_AudioDecode *Audio::AddIntroLoopMedia(const char *introPath,
                                                      const char *loopPath,
                                                      Uint64 start, Uint64 end,
                                                      const char *channelName) {
  if (introPath == nullptr || *introPath == '\0' || loopPath == nullptr ||
      *loopPath == '\0' || channelName == nullptr || *channelName == '\0' ||
      soundCtx == nullptr || timeCtx == nullptr) {
    KLOG_ERROR(AUDIO, "Invalid intro/loop audio configuration.");
    return nullptr;
  }

  Sound::Channel *channel = soundCtx->GetChannelByName(channelName);
  if (channel == nullptr) {
    KLOG_ERROR(AUDIO, "Channel not found: %s", channelName);
    return nullptr;
  }

  Kotonoha_AudioDecode *loop =
      KotonohaAudioInit(loopPath, channel->GetSpecs(), demuxer);
  if (loop == nullptr) {
    return nullptr;
  }

  loop->tm = &timeCtx;
  loop->start = start;
  loop->end = end;

  return AddMediaInternal(introPath, start, end, false, channelName, loop);
}

void Audio::RemoveMedia(struct Kotonoha_AudioDecode *mediaPtr) {
  for (auto mediaIt = mediaObjects.begin(); mediaIt != mediaObjects.end();) {
    Sound::Channel *channel = std::get<0>(*mediaIt);
    auto &audioDecodesPipe = std::get<1>(*mediaIt);

    for (auto pipeIt = audioDecodesPipe.begin();
         pipeIt != audioDecodesPipe.end();) {
      Kotonoha_AudioDecode *decodePtr = std::get<0>(*pipeIt);
      Sound::Channel::Pipe *pipePtr = std::get<1>(*pipeIt);

      if (mediaPtr == nullptr || decodePtr == mediaPtr) {
        if (channel != nullptr && pipePtr != nullptr) {
          channel->RemovePipe(pipePtr);
        }

        pipeIt = audioDecodesPipe.erase(pipeIt);
      } else {
        ++pipeIt;
      }
    }

    if (audioDecodesPipe.empty()) {
      mediaIt = mediaObjects.erase(mediaIt);
    } else {
      ++mediaIt;
    }
  }
}

int Audio::RenderMedia(void *data, Uint8 **target, int *size) {
  if (data == nullptr || target == nullptr || size == nullptr) {
    return 0;
  }

  Kotonoha_AudioDecode *instance = static_cast<Kotonoha_AudioDecode *>(data);

  if (instance->tm == nullptr || *instance->tm == nullptr) {
    return 0;
  }

  // start/end of 0/0 means one-shot: play immediately until the end of the file
  if (instance->start == 0 && instance->end == 0) {
    if (instance->oneShotDone) {
      return 0;
    }
    const int rt = KotonohaAudioRender(instance, target, size);
    if ((rt == 1 || rt == -1) && instance->inLoop && rt == 1) {
      KotonohaAudioSeek(instance, 0);
      instance->cannotSeek = false;
      return KotonohaAudioRender(instance, target, size);
    }
    if (rt == 1 || rt == -1) {
      instance->oneShotDone = true;
      return 0;
    }
    return rt;
  }

  bool inRange = false;
  Sint64 diff = 0;

  // Obtém o tempo atual na timeline para este evento de áudio
  SDL_LockMutex(instance->stateMutex);
  instance->lastTime = KotonohaTimeGetFromEvent(*instance->tm, instance->start,
                                                instance->end, &inRange, &diff);
  SDL_UnlockMutex(instance->stateMutex);
  if (!inRange) {
    return 0;
  }

  Kotonoha_AudioDecode *current =
      instance->playingLoop ? instance->loopMedia : instance;
  int rt = KotonohaAudioRender(current, target, size);

  if (rt == 1 && instance->loopMedia != nullptr) {
    if (!instance->playingLoop) {
      instance->playingLoop = true;
    }
    KotonohaAudioSeek(instance->loopMedia, 0);
    instance->loopMedia->cannotSeek = false;
    rt = KotonohaAudioRender(instance->loopMedia, target, size);
  } else if ((rt == -1 || rt == 1) && instance->inLoop &&
             instance->loopMedia == nullptr) {
    KotonohaAudioSeek(instance, 0);
    instance->cannotSeek = false;
    rt = KotonohaAudioRender(instance, target, size);
  }

  return rt;
}

std::vector<MediaBufferInfo> Audio::GetBufferInfo() const {
  std::vector<MediaBufferInfo> result;
  for (const auto &object : mediaObjects) {
    const Sound::Channel *channel = std::get<0>(object);
    const std::string channelName = channel != nullptr ? channel->name : "";
    for (const auto &entry : std::get<1>(object)) {
      const Kotonoha_AudioDecode *decode = std::get<0>(entry);
      AppendAudioBufferInfo(result, channelName, decode, nullptr);
      if (decode != nullptr && decode->loopMedia != nullptr) {
        AppendAudioBufferInfo(result, channelName, decode->loopMedia,
                              " (loop)");
      }
    }
  }
  return result;
}

bool Audio::PlayNow(const std::string &path, const std::string &channel) {
  if (path.empty() || channel.empty()) {
    KLOG_ERROR(AUDIO, "Cannot play media now without a path and channel.");
    return false;
  }
  StopPreview();
  previewMedia = AddMedia(path.c_str(), 0, 0, false, channel.c_str());
  if (previewMedia == nullptr) {
    return false;
  }
  previewPath = path;
  return true;
}

bool Audio::IsTracked(const Kotonoha_AudioDecode *media) const {
  for (const auto &object : mediaObjects) {
    for (const auto &entry : std::get<1>(object)) {
      if (std::get<0>(entry) == media) {
        return true;
      }
    }
  }
  return false;
}

void Audio::StopPreview() {
  // The decode may already have been freed when a one-shot finished.
  if (previewMedia != nullptr && IsTracked(previewMedia)) {
    RemoveMedia(previewMedia);
  }
  previewMedia = nullptr;
  previewPath.clear();
}

std::string Audio::GetPreviewPath() {
  if (previewMedia == nullptr) {
    return {};
  }
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