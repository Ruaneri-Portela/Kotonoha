#pragma once
#include <Kotonoha/Kotonoha.h>
#include <Kotonoha/utils/FFmpeg.h>
#include <Kotonoha/utils/Time.h>
#include <Kotonoha/SchoolDaysSceneTime.h>
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/hwcontext.h>
#include <libavutil/imgutils.h>
#include <libswresample/swresample.h>
#include <libswscale/swscale.h>

struct Kotonoha_videoData {
  AVFormatContext *pFormatCtx;
  AVCodecContext *pCodecCtx;
  struct SwsContext *swsCtx;
  struct ffmpegHwContext *hwCtx;
  int videoStreamIndex;

  AVFrame *pFrame;

  struct Kotonoha_time *time;
  Uint64 startTime, endTime, lastTime, videoTime, frameTime;
  SDL_Texture *texture;

  // ORS event window and decoder EOF are distinct from Current Scene Next.
  Kotonoha_SceneTick endTick;
  bool demuxEof;
  bool decoderEof;
};

struct Kotonoha_videoData *Kotonoha_VideoRenderInit(const char *filename,
                                                    struct Kotonoha_time *time,
                                                    Uint64 startTime,
                                                    Uint64 endTime,
                                                    Kotonoha_SceneTick endTick);

void Kotonoha_VideoRenderShutdown(struct Kotonoha_videoData **instance);

enum Kotonoha_Scene_Status Kotonoha_VideoRenderProcess(void *userData,
                                                       SDL_Renderer *render);
