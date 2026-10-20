#include <Kotonoha/utils/Log.h>
#include <Kotonoha/components/Audio.hpp>

namespace Kotonoha {

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

  Sound::Channel *targetChannel = soundCtx->GetChannelByName(channel);
  if (targetChannel == nullptr) {
    KLOG_ERROR(AUDIO, "Channel not found: %s",
                 channel);
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
    KLOG_ERROR(AUDIO, "Channel not found: %s",
                 channelName);
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

  bool inRange = false;
  Sint64 diff = 0;

  // Obtém o tempo atual na timeline para este evento de áudio
  instance->lastTime = KotonohaTimeGetFromEvent(*instance->tm, instance->start,
                                                instance->end, &inRange, &diff);
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

Audio::~Audio() { RemoveMedia(nullptr); }

} // namespace Kotonoha