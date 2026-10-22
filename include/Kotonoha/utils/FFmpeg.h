#pragma once

#include <SDL3/SDL.h>
#include <SDL3/SDL_stdinc.h>

#ifdef __cplusplus
extern "C" {
#endif
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/hwcontext.h>
#include <libavutil/imgutils.h>
#include <libavutil/opt.h>
#include <libswresample/swresample.h>
#include <libswscale/swscale.h>
#ifdef __cplusplus
}
#endif

#ifdef __cplusplus
extern "C" {
#endif

typedef struct Kotonoha_FFmpegHwContext {
  enum AVHWDeviceType type;
  AVBufferRef *deviceRef;
  enum AVPixelFormat hwPixFmt;
} Kotonoha_FFmpegHwContext;

/* Generic seekable input used by archive/memory-backed callers. Ownership of
 * opaque transfers to a successful load and is released exactly once by
 * KotonohaFFmpegCloseInput (or immediately when loading fails). */
typedef int (*Kotonoha_FFmpegInputReadFn)(void *opaque, Uint8 *buffer, int size);
typedef Sint64 (*Kotonoha_FFmpegInputSeekFn)(void *opaque, Sint64 offset,
                                            int whence);
typedef Sint64 (*Kotonoha_FFmpegInputSizeFn)(void *opaque);
typedef void (*Kotonoha_FFmpegInputCloseFn)(void *opaque);

typedef struct Kotonoha_FFmpegInput {
  const char *name;
  void *opaque;
  Kotonoha_FFmpegInputReadFn read;
  Kotonoha_FFmpegInputSeekFn seek;
  Kotonoha_FFmpegInputSizeFn size;
  Kotonoha_FFmpegInputCloseFn close;
} Kotonoha_FFmpegInput;

typedef int (*Kotonoha_FFmpegInputResolverFn)(const char *path,
                                             Kotonoha_FFmpegInput *input);
void KotonohaFFmpegSetInputResolver(Kotonoha_FFmpegInputResolverFn resolver);
int KotonohaFFmpegResolveInput(const char *path, Kotonoha_FFmpegInput *input);

typedef struct Kotonoha_FFmpegInputContext Kotonoha_FFmpegInputContext;

bool KotonohaFFmpegLoadInput(const Kotonoha_FFmpegInput *input,
                             AVFormatContext **formatCtx,
                             AVCodecContext **codecCtx,
                             enum AVMediaType mediaType,
                             int *streamIndexExport, int startIndex,
                             struct Kotonoha_FFmpegHwContext *hwCtx,
                             Kotonoha_FFmpegInputContext **inputContext);
void KotonohaFFmpegCloseInput(AVFormatContext **formatCtx,
                              Kotonoha_FFmpegInputContext **inputContext);
int KotonohaFFmpegSeekFrame(AVFormatContext *formatCtx, int streamIndex,
                            Sint64 timestamp, int flags);

/* Carregamento de Arquivos e Mídia */
bool KotonohaFFmpegLoad(const char *path, AVFormatContext **formatCtx,
                        AVCodecContext **codecCtx, enum AVMediaType mediaType,
                        int *streamIndexExport, int startIndex,
                        struct Kotonoha_FFmpegHwContext *hwCtx);

/* Leitura e Posicionamento */
int KotonohaFFmpegReadFrame(AVFormatContext *formatCtx, AVPacket *packet);

Uint64 KotonohaFFmpegGetTime(const AVStream *stream, Sint64 pts, Sint64 dts);

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