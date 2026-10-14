#pragma once

#include <SDL3/SDL.h>

#include <Kotonoha/utils/Demuxer.h>
#include <Kotonoha/utils/FFmpeg.h>
#include <Kotonoha/utils/IOMonitor.h>
#include <Kotonoha/utils/Time.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct Kotonoha_AudioDecode {
  AVFormatContext *formatCtx;
  AVCodecContext *codecCtx;
  SwrContext *swrCtx;
  Kotonoha_IOMonitorOperation ioOperation;
  Kotonoha_Demuxer *demuxer;
  Kotonoha_DemuxStream *stream;

  int audioStreamIndex;
  SDL_AudioSpec specification;

  Uint64 start, end, audioTime, lastTime;
  float cacheDurationSeconds;

  bool inLoop;
  int executions;
  Kotonoha_Time **tm;
  void *dataGeneric;
  struct Kotonoha_AudioDecode *loopMedia;
  bool playingLoop;
  bool cannotSeek;
  bool firstSeek;
} Kotonoha_AudioDecode;

void genWhiteNoise(void *data, Uint8 **target, size_t *size);

Kotonoha_AudioDecode *KotonohaAudioInit(const char *path,
                                        SDL_AudioSpec specification,
                                        Kotonoha_IOMonitor *ioMonitor,
                                        Kotonoha_Demuxer *demuxer);

void KotonohaAudioSeek(struct Kotonoha_AudioDecode *ctx, Uint64 time);

int KotonohaAudioRender(void *data, Uint8 **target, int *size);

void KotonohaAudioFree(void *data);

#ifdef __cplusplus
}
#endif
