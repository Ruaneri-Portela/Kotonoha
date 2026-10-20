#include <Kotonoha/utils/FFmpeg.h>
#include <Kotonoha/utils/Log.h>
#include <SDL3/SDL_stdinc.h>
#include <errno.h>
#include <libavutil/error.h>

static void KotonohaFmpegSetupHwDecode(struct Kotonoha_FFmpegHwContext *hwCtx,
                                       const AVCodec **codec) {
  for (int i = 0;; i++) {
    const AVCodecHWConfig *config = avcodec_get_hw_config(*codec, i);
    if (!config)
      break;

    if (config->methods & AV_CODEC_HW_CONFIG_METHOD_HW_DEVICE_CTX &&
        !(av_hwdevice_ctx_create(&hwCtx->deviceRef, config->device_type, NULL,
                                 NULL, 0) < 0)) {
      hwCtx->hwPixFmt = config->pix_fmt;
      hwCtx->type = config->device_type;
      return;
    }
  }

  hwCtx->hwPixFmt = AV_PIX_FMT_NONE;
  KLOG_ERROR(FFMPEG,
             "Decoder %s does not support hardware decoding here, falling back "
             "to software",
             (*codec)->name);
}

int KotonohaFFmpegReadFrame(AVFormatContext *formatCtx, AVPacket *packet) {
  if (formatCtx == NULL || packet == NULL)
    return AVERROR(EINVAL);
  return av_read_frame(formatCtx, packet);
}

bool KotonohaFFmpegLoad(const char *path, AVFormatContext **formatCtx,
                        AVCodecContext **codecCtx, enum AVMediaType mediaType,
                        int *streamIndexExport, int startIndex,
                        struct Kotonoha_FFmpegHwContext *hwCtx) {
  if (path == NULL || formatCtx == NULL || codecCtx == NULL) {
    KLOG_ERROR(FFMPEG, "Invalid FFmpeg load arguments");
    return false;
  }

  AVDictionary *opts = NULL;
  const bool isHttp = SDL_strncasecmp(path, "http://", 7) == 0 ||
                      SDL_strncasecmp(path, "https://", 8) == 0;
  if (isHttp) {
    av_dict_set(&opts, "user_agent", "KotonohaEngine/0.1", 0);
    av_dict_set(&opts, "rw_timeout", "750000", 0);
    av_dict_set(&opts, "timeout", "750000", 0);
  }

  *formatCtx = NULL;
  const int openResult = avformat_open_input(formatCtx, path, NULL, &opts);
  av_dict_free(&opts);
  if (openResult < 0) {
    if (*formatCtx != NULL) {
      avformat_close_input(formatCtx);
    }
    return false;
  }

  const int streamInfoResult = avformat_find_stream_info(*formatCtx, NULL);
  if (streamInfoResult < 0) {
    KLOG_ERROR(FFMPEG, "Could not read stream information from '%s': %s", path,
               av_err2str(streamInfoResult));
    avformat_close_input(formatCtx);
    return false;
  }

  int streamIndex = -1;
  for (unsigned int i = startIndex; i < (*formatCtx)->nb_streams; i++) {
    if ((*formatCtx)->streams[i]->codecpar->codec_type == mediaType) {
      streamIndex = (int)i;
      break;
    }
  }

  if (streamIndex == -1) {
    KLOG_ERROR(FFMPEG, "Error: no stream of type %d found", mediaType);
    avformat_close_input(formatCtx);
    return false;
  }

  if (!KotonohaFFmpegCreateDecoderCtx(*formatCtx, streamIndex, codecCtx,
                                      hwCtx)) {
    avformat_close_input(formatCtx);
    return false;
  }

  if (streamIndexExport != NULL)
    *streamIndexExport = streamIndex;
  return true;
}

/* --- REUTILIZÁVEL: Criação de Contexto do Decoder --- */
bool KotonohaFFmpegCreateDecoderCtx(AVFormatContext *formatCtx, int streamIndex,
                                    AVCodecContext **codecCtx,
                                    struct Kotonoha_FFmpegHwContext *hwCtx) {
  if (!formatCtx || streamIndex < 0 || !codecCtx)
    return false;

  const AVCodec *codec =
      avcodec_find_decoder(formatCtx->streams[streamIndex]->codecpar->codec_id);
  if (!codec) {
    KLOG_ERROR(FFMPEG, "Error: codec not found");
    return false;
  }

  if (hwCtx != NULL) {
    KotonohaFmpegSetupHwDecode(hwCtx, &codec);
  }

  *codecCtx = avcodec_alloc_context3(codec);
  if (!*codecCtx)
    return false;

  if (avcodec_parameters_to_context(
          *codecCtx, formatCtx->streams[streamIndex]->codecpar) < 0) {
    avcodec_free_context(codecCtx);
    return false;
  }

  if (hwCtx != NULL && hwCtx->hwPixFmt != AV_PIX_FMT_NONE) {
    (*codecCtx)->hw_device_ctx = av_buffer_ref(hwCtx->deviceRef);
    (*codecCtx)->pix_fmt = hwCtx->hwPixFmt;
  }

  if (avcodec_open2(*codecCtx, codec, NULL) < 0) {
    avcodec_free_context(codecCtx);
    return false;
  }

  return true;
}

/* --- REUTILIZÁVEL: Alocação e Inicialização do Resampler de Áudio --- */
SwrContext *
KotonohaFFmpegCreateAudioResampler(AVCodecContext *codecCtx, int outChannels,
                                   int outSampleRate,
                                   enum AVSampleFormat outSampleFmt) {
  if (!codecCtx)
    return NULL;

  SwrContext *swrCtx = NULL;
  AVChannelLayout outLayout;
  av_channel_layout_default(&outLayout, outChannels);

  if (swr_alloc_set_opts2(&swrCtx, &outLayout, outSampleFmt, outSampleRate,
                          &codecCtx->ch_layout, codecCtx->sample_fmt,
                          codecCtx->sample_rate, 0, NULL) < 0) {
    av_channel_layout_uninit(&outLayout);
    return NULL;
  }
  av_channel_layout_uninit(&outLayout);

  if (!swrCtx || swr_init(swrCtx) < 0) {
    swr_free(&swrCtx);
    return NULL;
  }

  return swrCtx;
}

/* --- REUTILIZÁVEL: Decodificação e Conversão de Buffer de Áudio --- */
int KotonohaFFmpegConvertAudioFrame(SwrContext *swrCtx, AVFrame *pFrame,
                                    int outChannels, enum AVSampleFormat outFmt,
                                    Uint8 **target, int *size) {
  if (!swrCtx || !pFrame || !target || !size)
    return -1;

  int outSamples = swr_get_out_samples(swrCtx, pFrame->nb_samples);
  if (outSamples <= 0)
    return 0;

  Uint8 **nonPlanarData = NULL;
  if (av_samples_alloc_array_and_samples(&nonPlanarData, NULL, outChannels,
                                         outSamples, outFmt, 0) < 0) {
    return -1;
  }

  int convertedSamples =
      swr_convert(swrCtx, nonPlanarData, outSamples,
                  (const uint8_t **)pFrame->extended_data, pFrame->nb_samples);

  if (convertedSamples <= 0) {
    av_freep(&nonPlanarData[0]);
    av_freep(&nonPlanarData);
    return convertedSamples < 0 ? -1 : 0;
  }

  const int bytesPerSample = av_get_bytes_per_sample(outFmt);
  *size = convertedSamples * outChannels * bytesPerSample;
  *target = (Uint8 *)SDL_malloc(*size);

  if (!*target) {
    av_freep(&nonPlanarData[0]);
    av_freep(&nonPlanarData);
    *size = 0;
    return -1;
  }

  SDL_memcpy(*target, nonPlanarData[0], *size);
  av_freep(&nonPlanarData[0]);
  av_freep(&nonPlanarData);

  return convertedSamples;
}

AVFrame *KotonohaFFmpegSwsConvertFrame(struct SwsContext *convertContext,
                                       AVFrame *sourceFrame, Uint8 **buffer,
                                       int targetH, int targetW) {
  if (convertContext == NULL || sourceFrame == NULL || buffer == NULL ||
      targetW <= 0 || targetH <= 0) {
    KLOG_ERROR(FFMPEG, "Invalid arguments for RGBA frame conversion");
    return NULL;
  }

  AVFrame *pFrameRGB = av_frame_alloc();
  if (!pFrameRGB) {
    KLOG_ERROR(FFMPEG, "Error allocating frame");
    return NULL;
  }

  const int bufferSize =
      av_image_get_buffer_size(AV_PIX_FMT_RGBA, targetW, targetH, 1);
  if (bufferSize <= 0) {
    KLOG_ERROR(FFMPEG, "Invalid target dimensions for RGBA conversion");
    av_frame_free(&pFrameRGB);
    return NULL;
  }

  Uint8 *frameBuffer = (Uint8 *)av_malloc((size_t)bufferSize);

  if (frameBuffer == NULL) {
    KLOG_ERROR(FFMPEG, "Error allocating buffer");
    av_frame_free(&pFrameRGB);
    return NULL;
  }

  if (av_image_fill_arrays(pFrameRGB->data, pFrameRGB->linesize, frameBuffer,
                           AV_PIX_FMT_RGBA, targetW, targetH, 1) < 0) {
    KLOG_ERROR(FFMPEG, "Error filling frame arrays");
    av_frame_free(&pFrameRGB);
    av_free(frameBuffer);
    return NULL;
  }

  const int scaledHeight =
      sws_scale(convertContext, (const uint8_t *const *)sourceFrame->data,
                sourceFrame->linesize, 0, sourceFrame->height, pFrameRGB->data,
                pFrameRGB->linesize);
  if (scaledHeight != targetH) {
    KLOG_ERROR(FFMPEG, "Incomplete RGBA frame conversion (%d/%d rows)",
               scaledHeight, targetH);
    av_frame_free(&pFrameRGB);
    av_free(frameBuffer);
    return NULL;
  }

  *buffer = frameBuffer;
  pFrameRGB->format = AV_PIX_FMT_RGBA;
  pFrameRGB->height = targetH;
  pFrameRGB->width = targetW;
  return pFrameRGB;
};

Uint64 KotonohaFFmpegGetTime(AVStream *stream, Sint64 pts, Sint64 dts) {
  if (pts != AV_NOPTS_VALUE) {
    return (Uint64)(pts * av_q2d(stream->time_base) * 1000);
  }
  if (dts != AV_NOPTS_VALUE) {
    return (Uint64)(dts * av_q2d(stream->time_base) * 1000);
  }
  KLOG_ERROR(FFMPEG, "Ambos PTS e DTS sao invalidos");
  return 0;
}

struct SwsContext *
KotonohaFFmpegCreateSwsContext(AVCodecContext *codecCtx,
                               enum AVPixelFormat srcPixelFmt, int w, int h) {
  struct SwsContext *swsCtx =
      sws_getContext(codecCtx->width, codecCtx->height, srcPixelFmt, w, h,
                     AV_PIX_FMT_RGBA, SWS_LANCZOS, NULL, NULL, NULL);
  if (!swsCtx) {
    KLOG_ERROR(FFMPEG, "Error: failed to create the sws context");
  }
  return swsCtx;
}