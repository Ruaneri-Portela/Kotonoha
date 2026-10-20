#pragma once

#include <SDL3/SDL.h>

#include <Kotonoha/Kotonoha.h>
#include <Kotonoha/utils/Demuxer.h>
#include <Kotonoha/utils/FFmpeg.h>
#include <Kotonoha/utils/Time.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct Kotonoha_VideoData {
  AVFrame *pFrame;
  AVFrame *pFrameRGB;
  AVFrame *currentFrame; /* Quadro mantido até chegar o tempo do próximo */
  Uint64 currentPtsMs;
  Uint64 textureSerial; /* Incrementa a cada novo quadro enviado à textura */

  struct SwsContext *swsCtx;
  Uint8 *swsBuffer;
  int swsBufferWidth;
  int swsBufferHeight;

  AVCodecContext *pCodecCtx;
  int videoStreamIndex;
  AVRational videoTimeBase;
  enum AVPixelFormat sourcePixelFormat;

  Kotonoha_Time *time;
  Kotonoha_FFmpegHwContext *hwCtx;
  Kotonoha_Demuxer *demuxer;
  Kotonoha_DemuxStream *stream;

  Uint64 startTime, endTime, lastTime, videoTime;
  SDL_Texture *texture, *extraTexture;

  bool useOrsFrameTimeline;
  Uint64 orsEndFrame;
  bool firstSeek;
  bool decoderFlushSent;
  bool textureFormatInitialized;
  bool directTextureUploadFailed;
} Kotonoha_VideoData;

Kotonoha_VideoData *
KotonohaVideoRenderInit(const char *filename, Kotonoha_Time *time,
                        Uint64 startTime, Uint64 endTime,
                        bool useOrsFrameTimeline, Uint64 orsEndFrame,
                        float cacheDurationSeconds, Kotonoha_Demuxer *demuxer);

void KotonohaVideoRenderShutdown(Kotonoha_VideoData **instance);

enum Kotonoha_Scene_Status KotonohaVideoRenderProcess(void *userData,
                                                      SDL_Renderer *render);

#ifdef __cplusplus
}
#endif
