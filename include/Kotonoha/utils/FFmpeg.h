#pragma once
#include <Kotonoha/utils/IOMonitor.h>
#include <SDL3/SDL.h>
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/hwcontext.h>
#include <libavutil/imgutils.h>
#include <libavutil/opt.h>
#include <libswscale/swscale.h>

typedef struct Kotonoha_FFmpegHwContext {
  enum AVHWDeviceType type;
  AVBufferRef *deviceRef;
  enum AVPixelFormat hwPixFmt;
} Kotonoha_FFmpegHwContext;

bool KotonohaFFmpegLoad(const char *path, AVFormatContext **formatCtx,
                        AVCodecContext **codecCtx, enum AVMediaType mediaType,
                        int *streamIndexExport, int startIndex,
                        struct Kotonoha_FFmpegHwContext *hwCtx);
bool KotonohaFFmpegLoadMonitored(const char *path, AVFormatContext **formatCtx,
                                 AVCodecContext **codecCtx,
                                 enum AVMediaType mediaType,
                                 int *streamIndexExport, int startIndex,
                                 struct Kotonoha_FFmpegHwContext *hwCtx,
                                 Kotonoha_IOMonitorOperation *ioOperation);

int KotonohaFFmpegReadFrame(AVFormatContext *formatCtx, AVPacket *packet);
int KotonohaFFmpegSeekFrame(AVFormatContext *formatCtx,
                            Kotonoha_IOMonitorOperation *ioOperation,
                            int streamIndex, int64_t timestamp, int flags);

Uint64 KotonohaFFmpegGetTime(AVStream *stream, Sint64 pts, Sint64 dts);

struct SwsContext *
KotonohaFFmpegCreateSwsContext(AVCodecContext *codecCtx,
                               enum AVPixelFormat srcPixelFmt, int w, int h);

AVFrame *KotonohaFFmpegSwsConvertFrame(struct SwsContext *convertContext,
                                       AVFrame *sourceFrame, Uint8 **buffer,
                                       int targetH, int targetW);