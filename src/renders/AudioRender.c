#include <Kotonoha/renders/AudioRender.h>

static void cleanupAudioResources(AVPacket *packet, Uint8 **nonPlanarData,
                                  AVFrame *pFrame) {
  if (packet)
    av_packet_unref(packet);

  if (nonPlanarData) {
    if (nonPlanarData[0])
      av_freep(&nonPlanarData[0]);
    av_freep(&nonPlanarData);
  }

  if (pFrame)
    av_frame_free(&pFrame);
}

static int allocateAudioBuffer(struct Kotonoha_audioDecode *instance,
                               int sampleCount, Uint8 **target, int *size) {
  const int bytesPerSample = (int)sizeof(float);
  *size = sampleCount * instance->specification.channels * bytesPerSample;
  *target = (Uint8 *)SDL_malloc(*size);

  if (!*target) {
    SDL_LogError(SDL_LOG_CATEGORY_ERROR, "Error allocating target buffer");
    *size = 0;
    return 0;
  }

  return 1;
}

void genWhiteNoise(void *parms, Uint8 **target, size_t *size) {
  (void)parms;

  const size_t sampleCount = 1024;
  const size_t bufferSize = sampleCount * sizeof(float);
  float *data = (float *)SDL_malloc(bufferSize);

  if (!data) {
    SDL_LogError(SDL_LOG_CATEGORY_ERROR,
                 "Failed to allocate memory for white noise");
    *target = NULL;
    *size = 0;
    return;
  }

  for (size_t i = 0; i < sampleCount; ++i) {
    data[i] = (2.0f * ((float)rand() / (float)RAND_MAX) - 1.0f) * 0.01f;
  }

  *target = (Uint8 *)data;
  *size = bufferSize;
}

struct Kotonoha_audioDecode *Kotonoha_AudioInit(const char *path,
                                                SDL_AudioSpec specification) {
  struct Kotonoha_audioDecode *audioDecode =
      (struct Kotonoha_audioDecode *)SDL_calloc(
          1, sizeof(struct Kotonoha_audioDecode));

  if (!audioDecode) {
    SDL_LogError(SDL_LOG_CATEGORY_ERROR, "calloc");
    return NULL;
  }

  if (!Kotonoha_UtilsFFmpegLoad(path, &audioDecode->formatCtx,
                                &audioDecode->codecCtx, AVMEDIA_TYPE_AUDIO,
                                &audioDecode->audioStreamIndex, 0, NULL)) {
    SDL_free(audioDecode);
    return NULL;
  }

  audioDecode->specification = specification;
  audioDecode->executions = 1;

  AVChannelLayout outLayout;
  av_channel_layout_default(&outLayout, audioDecode->specification.channels);

  if (swr_alloc_set_opts2(&audioDecode->swrCtx, &outLayout, AV_SAMPLE_FMT_FLT,
                          audioDecode->specification.freq,
                          &audioDecode->codecCtx->ch_layout,
                          audioDecode->codecCtx->sample_fmt,
                          audioDecode->codecCtx->sample_rate, 0, NULL) < 0) {
    SDL_LogError(SDL_LOG_CATEGORY_ERROR, "Failed to set SwrContext options");
    av_channel_layout_uninit(&outLayout);
    avcodec_free_context(&audioDecode->codecCtx);
    avformat_close_input(&audioDecode->formatCtx);
    SDL_free(audioDecode);
    return NULL;
  }

  av_channel_layout_uninit(&outLayout);

  if (!audioDecode->swrCtx) {
    SDL_LogError(SDL_LOG_CATEGORY_ERROR, "Failed to allocate SwrContext");
    avcodec_free_context(&audioDecode->codecCtx);
    avformat_close_input(&audioDecode->formatCtx);
    SDL_free(audioDecode);
    return NULL;
  }

  if (swr_init(audioDecode->swrCtx) < 0) {
    SDL_LogError(SDL_LOG_CATEGORY_ERROR, "Failed to initialize SwrContext");
    swr_free(&audioDecode->swrCtx);
    avcodec_free_context(&audioDecode->codecCtx);
    avformat_close_input(&audioDecode->formatCtx);
    SDL_free(audioDecode);
    return NULL;
  }

  return audioDecode;
}

void Kotonoha_AudioSeek(struct Kotonoha_audioDecode *ctx, Sint64 time) {
  if (!ctx || !ctx->formatCtx || !ctx->codecCtx)
    return;

  if (ctx->audioTime > 0)
    time = time % ctx->audioTime;

  if (av_seek_frame(ctx->formatCtx, ctx->audioStreamIndex, time,
      AVSEEK_FLAG_ANY) < 0) {
    SDL_LogError(SDL_LOG_CATEGORY_ERROR, "SeekError time %lld",
                 (long long)time);
    return;
  }

  avcodec_flush_buffers(ctx->codecCtx);
  ctx->executions++;
}

int Kotonoha_AudioRender(void *data, Uint8 **target, int *size) {
  struct Kotonoha_audioDecode *instance = (struct Kotonoha_audioDecode *)data;
  struct Kotonoha_time *time = NULL;
  AVFrame *pFrame = NULL;
  AVPacket packet;
  int ret;

  if (!instance || !target || !size)
    return -1;

  *target = NULL;
  *size = 0;

  if (instance->tm != NULL && *instance->tm != NULL) {
    time = *instance->tm;
    if (time->paused)
      return 0;
  }

  pFrame = av_frame_alloc();
  if (!pFrame) {
    SDL_LogError(SDL_LOG_CATEGORY_ERROR, "Error allocating frame");
    return -1;
  }

  packet.data = NULL;
  packet.size = 0;

  while ((ret = av_read_frame(instance->formatCtx, &packet)) >= 0) {
    if (packet.stream_index != instance->audioStreamIndex) {
      av_packet_unref(&packet);
      continue;
    }

    instance->audioTime = Kotonoha_UtilsFFmpegGetTime(
        instance->formatCtx->streams[instance->audioStreamIndex], packet.pts,
        packet.dts);

    Sint64 diff =
        instance->lastTime - (instance->audioTime * instance->executions);
    if (time != NULL && diff > 0) {
      av_packet_unref(&packet);
      continue;
    }

    ret = avcodec_send_packet(instance->codecCtx, &packet);
    av_packet_unref(&packet);

    if (ret < 0) {
      SDL_LogError(SDL_LOG_CATEGORY_ERROR, "Error sending packet to codec: %s",
                   av_err2str(ret));
      av_frame_free(&pFrame);
      return -1;
    }

    while ((ret = avcodec_receive_frame(instance->codecCtx, pFrame)) >= 0) {
      Uint8 **nonPlanarData = NULL;
      int outSamples =
          swr_get_out_samples(instance->swrCtx, pFrame->nb_samples);

      if (outSamples <= 0) {
        SDL_LogError(SDL_LOG_CATEGORY_ERROR, "Invalid output sample count");
        av_frame_unref(pFrame);
        continue;
      }

      if (av_samples_alloc_array_and_samples(
              &nonPlanarData, NULL, instance->specification.channels,
              outSamples, AV_SAMPLE_FMT_FLT, 0) < 0) {
        SDL_LogError(SDL_LOG_CATEGORY_ERROR, "Error allocating audio samples");
        av_frame_free(&pFrame);
        return -1;
      }

      int convertedSamples = swr_convert(
          instance->swrCtx, nonPlanarData, outSamples,
          (const uint8_t **)pFrame->extended_data, pFrame->nb_samples);

      if (convertedSamples < 0) {
        SDL_LogError(SDL_LOG_CATEGORY_ERROR, "Error converting audio samples");
        cleanupAudioResources(NULL, nonPlanarData, NULL);
        av_frame_free(&pFrame);
        return -1;
      }

      if (!allocateAudioBuffer(instance, convertedSamples, target, size)) {
        cleanupAudioResources(NULL, nonPlanarData, NULL);
        av_frame_free(&pFrame);
        return -1;
      }

      SDL_memcpy(*target, nonPlanarData[0], *size);

      cleanupAudioResources(NULL, nonPlanarData, NULL);
      av_frame_unref(pFrame);
      av_frame_free(&pFrame);
      return 2;
    }

    if (ret == AVERROR(EAGAIN))
      continue;

    if (ret == AVERROR_EOF)
      break;

    if (ret < 0) {
      SDL_LogError(SDL_LOG_CATEGORY_ERROR, "Error receiving frame: %s",
                   av_err2str(ret));
      av_frame_free(&pFrame);
      return -1;
    }
  }

  av_frame_free(&pFrame);
  return (ret == AVERROR_EOF) ? 1 : -1;
}

void Kotonoha_AudioFree(void *data) {
  if (data != NULL) {
    struct Kotonoha_audioDecode *audioDecode =
        (struct Kotonoha_audioDecode *)data;
    swr_free(&audioDecode->swrCtx);
    avformat_close_input(&audioDecode->formatCtx);
    avcodec_free_context(&audioDecode->codecCtx);
    SDL_free(audioDecode);
  }
}