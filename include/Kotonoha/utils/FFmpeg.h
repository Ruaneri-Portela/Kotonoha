#pragma once
#include <SDL3/SDL.h>
#include <Kotonoha/utils/IOMonitor.h>
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/hwcontext.h>
#include <libavutil/imgutils.h>
#include <libavutil/opt.h>
#include <libswscale/swscale.h>


struct ffmpegHwContext {
  enum AVHWDeviceType type;
  AVBufferRef *deviceRef;
  enum AVPixelFormat hwPixFmt;
};

bool Kotonoha_UtilsFFmpegLoad(const char *path, AVFormatContext **formatCtx,
                              AVCodecContext **codecCtx,
                              enum AVMediaType mediaType,
                              int *streamIndexExport, int startIndex,
                              struct ffmpegHwContext *hwCtx);
bool Kotonoha_UtilsFFmpegLoadMonitored(
    const char *path, AVFormatContext **formatCtx,
    AVCodecContext **codecCtx, enum AVMediaType mediaType,
    int *streamIndexExport, int startIndex, struct ffmpegHwContext *hwCtx,
    Kotonoha_IOMonitorOperation *ioOperation);

int Kotonoha_UtilsFFmpegReadFrame(AVFormatContext *formatCtx,
                                  AVPacket *packet);
int Kotonoha_UtilsFFmpegSeekFrame(AVFormatContext *formatCtx,
                                  Kotonoha_IOMonitorOperation *ioOperation,
                                  int streamIndex, int64_t timestamp,
                                  int flags);

Uint64 Kotonoha_UtilsFFmpegGetTime(AVStream *stream, Sint64 pts, Sint64 dts);

struct SwsContext *Kotonoha_UtilsFFmpegCreateSwsContext(
    AVCodecContext *codecCtx, enum AVPixelFormat srcPixelFmt, int w, int h);

AVFrame *Kotonoha_UtilsFFmpegSwsConvertFrame(struct SwsContext *convertContext,
                                             AVFrame *sourceFrame,
                                             Uint8 **buffer, int targetH,
                                             int targetW);