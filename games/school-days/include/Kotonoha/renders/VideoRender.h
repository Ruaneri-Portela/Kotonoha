#pragma once

#include <Kotonoha/Kotonoha.h>
#include <Kotonoha/SceneTime.h>
#include <Kotonoha/utils/Demuxer.h>
#include <Kotonoha/utils/FFmpeg.h>
#include <Kotonoha/utils/Time.h>
#include <SDL3/SDL.h>
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/hwcontext.h>
#include <libavutil/imgutils.h>
#include <libswscale/swscale.h>

#ifdef __cplusplus
extern "C" {
#endif

/* One object, two public spellings: legacy G2.7 and Ruaneri API. */
typedef struct Kotonoha_videoData {
  AVFormatContext *pFormatCtx;
  AVCodecContext *pCodecCtx;
  struct SwsContext *swsCtx;
  Kotonoha_FFmpegHwContext *hwCtx;
  int videoStreamIndex;
  AVRational videoTimeBase;

  AVFrame *currentFrame;
  Uint64 currentPtsMs;
  AVFrame *pFrameRGB;
  Uint8 *swsBuffer;
  int swsBufferWidth;
  int swsBufferHeight;

  Kotonoha_time *time;
  Kotonoha_Demuxer *demuxer;
  Kotonoha_DemuxStream *stream;

  /* Non-null only for the legacy standalone C API compatibility path. */
  Kotonoha_Demuxer *ownedDemuxer;

  Uint64 startTime;
  Uint64 endTime;
  Uint64 lastTime;
  Uint64 videoTime;
  Uint64 lastTimelineTime;
  Uint64 timeDiscontinuitySerial;
  Uint64 textureSerial;
  SDL_Texture *texture;
  Uint32 textureFormat;
  enum AVPixelFormat sourcePixelFormat;
  bool textureFormatInitialized;
  bool directTextureUploadFailed;

  /* ORS END owns the physical movie lifetime; decoder EOF does not. */
  Kotonoha_SceneTick endTick;
  bool firstSeek;
  bool hasTimelineTime;
  bool demuxEof;
  bool decoderFlushSent;
  bool decoderEof;
} Kotonoha_videoData;

typedef Kotonoha_videoData Kotonoha_VideoData;

/* Shared-backend API used by the Video component. */
Kotonoha_VideoData *KotonohaVideoRenderInit(
    const char *filename, Kotonoha_Time *time, Uint64 startTime, Uint64 endTime,
    Kotonoha_SceneTick endTick, Kotonoha_Demuxer *demuxer);
bool KotonohaVideoRenderPrime(Kotonoha_VideoData *instance, Uint32 waitMs);
void KotonohaVideoRenderShutdown(Kotonoha_VideoData **instance);
enum Kotonoha_Scene_Status KotonohaVideoRenderProcess(void *userData,
                                                      SDL_Renderer *render);

/* G2.7 standalone compatibility surface, backed by the same queue decoder. */
Kotonoha_videoData *Kotonoha_VideoRenderInit(const char *filename,
                                             Kotonoha_time *time,
                                             Uint64 startTime, Uint64 endTime,
                                             Kotonoha_SceneTick endTick);
void Kotonoha_VideoRenderShutdown(Kotonoha_videoData **instance);
enum Kotonoha_Scene_Status Kotonoha_VideoRenderProcess(void *userData,
                                                       SDL_Renderer *render);

#ifdef __cplusplus
}
#endif
