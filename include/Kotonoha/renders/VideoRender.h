#pragma once

#include <Kotonoha/Kotonoha.h>
#include <Kotonoha/utils/FFmpeg.h>
#include <Kotonoha/utils/Time.h>
#include <SDL3/SDL.h>
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/hwcontext.h>
#include <libavutil/imgutils.h>
#include <libswresample/swresample.h>
#include <libswscale/swscale.h>

typedef struct Kotonoha_CachedVideoFrame {
  AVFrame *frame;
  Uint64 ptsMs;
} Kotonoha_CachedVideoFrame;

typedef struct Kotonoha_VideoData {
  AVFrame *pFrame;
  AVFrame *pFrameRGB;
  struct SwsContext *swsCtx;
  Uint8 *swsBuffer;
  int swsBufferWidth;
  int swsBufferHeight;

  AVFormatContext *pFormatCtx;
  AVCodecContext *pCodecCtx;
  int videoStreamIndex;

  Kotonoha_Time *time;
  Kotonoha_FFmpegHwContext *hwCtx;
  Kotonoha_IOMonitorOperation ioOperation;

  Uint64 startTime, endTime, lastTime, videoTime, frameTime;
  SDL_Texture *texture, *extraTexture;

  // Dedicated background decode thread and frame cache queue
  SDL_Thread *decodeThread;
  SDL_Mutex *queueMutex;
  SDL_Condition *queueCond;
  Kotonoha_CachedVideoFrame *frameQueue;
  int queueCapacity;
  int queueHead;
  int queueTail;
  int queueCount;

  SDL_AtomicInt threadRunning;
  SDL_AtomicInt seekRequested;
  Uint64 seekTargetMs;
  SDL_AtomicInt isBuffering;
  SDL_AtomicInt decodeThreadEof;
  float cacheDurationSeconds;

  // PLAY_MOVIE can opt into the original School Days 24-fps ORS timeline.
  bool useOrsFrameTimeline;
  Uint64 orsEndFrame;
  bool demuxEof;
  bool decoderEof;
} Kotonoha_VideoData;

struct Kotonoha_VideoData *KotonohaVideoRenderInit(
    const char *filename, Kotonoha_Time *time, Uint64 startTime, Uint64 endTime,
    bool useOrsFrameTimeline, Uint64 orsEndFrame, Kotonoha_IOMonitor *ioMonitor,
    float cacheDurationSeconds);

void KotonohaVideoRenderShutdown(struct Kotonoha_VideoData **instance);

enum Kotonoha_Scene_Status KotonohaVideoRenderProcess(void *userData,
                                                      SDL_Renderer *render);
