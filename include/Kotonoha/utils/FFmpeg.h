#pragma once

#include <SDL3/SDL.h>
#include <SDL3/SDL_stdinc.h>

#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/hwcontext.h>
#include <libavutil/imgutils.h>
#include <libavutil/opt.h>
#include <libswresample/swresample.h>
#include <libswscale/swscale.h>

#include <Kotonoha/utils/IOMonitor.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct Kotonoha_FFmpegHwContext {
  enum AVHWDeviceType type;
  AVBufferRef *deviceRef;
  enum AVPixelFormat hwPixFmt;
} Kotonoha_FFmpegHwContext;

/* Carregamento de Arquivos e Mídia */
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

/* Leitura e Posicionamento */
int KotonohaFFmpegReadFrame(AVFormatContext *formatCtx, AVPacket *packet);

int KotonohaFFmpegSeekFrame(AVFormatContext *formatCtx,
                            Kotonoha_IOMonitorOperation *ioOperation,
                            int streamIndex, Sint64 timestamp, int flags);

Uint64 KotonohaFFmpegGetTime(AVStream *stream, Sint64 pts, Sint64 dts);

/* Utilitários de Inicialização do Decodificador e Resamplers */
bool KotonohaFFmpegCreateDecoderCtx(AVFormatContext *formatCtx, int streamIndex,
                                    AVCodecContext **codecCtx,
                                    struct Kotonoha_FFmpegHwContext *hwCtx);

struct SwsContext *
KotonohaFFmpegCreateSwsContext(AVCodecContext *codecCtx,
                               enum AVPixelFormat srcPixelFmt, int w, int h);

SwrContext *
KotonohaFFmpegCreateAudioResampler(AVCodecContext *codecCtx, int outChannels,
                                   int outSampleRate,
                                   enum AVSampleFormat outSampleFmt);

/* Conversão de Quadros e Buffers */
int KotonohaFFmpegConvertAudioFrame(SwrContext *swrCtx, AVFrame *pFrame,
                                    int outChannels, enum AVSampleFormat outFmt,
                                    Uint8 **target, int *size);

AVFrame *KotonohaFFmpegSwsConvertFrame(struct SwsContext *convertContext,
                                       AVFrame *sourceFrame, Uint8 **buffer,
                                       int targetH, int targetW);

#ifdef __cplusplus
}
#endif