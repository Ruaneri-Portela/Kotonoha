#pragma once
#include <Kotonoha/utils/FFmpeg.h>
#include <Kotonoha/utils/Time.h>
#include <SDL3/SDL.h>
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/frame.h>
#include <libswresample/swresample.h>

typedef struct Kotonoha_AudioDecode {
  AVFormatContext *formatCtx;
  AVCodecContext *codecCtx;
  SwrContext *swrCtx;
  Kotonoha_IOMonitorOperation ioOperation;

  int audioStreamIndex;
  SDL_AudioSpec specification;

  Uint64 start, end, audioTime, lastTime;
  bool inLoop;
  int executions;
  Kotonoha_Time **tm;
  void *dataGeneric;
  struct Kotonoha_AudioDecode *loopMedia;
  bool playingLoop;
} Kotonoha_AudioDecode;

void genWhiteNoise(void *data, Uint8 **target, size_t *size);

struct Kotonoha_AudioDecode *KotonohaAudioInit(const char *path,
                                               SDL_AudioSpec specification,
                                               Kotonoha_IOMonitor *ioMonitor);

void KotonohaAudioSeek(struct Kotonoha_AudioDecode *ctx, Uint64 time);

int KotonohaAudioRender(void *data, Uint8 **target, int *size);

void KotonohaAudioFree(void *data);