#include <Kotonoha/renders/AudioRender.h>
#include <Kotonoha/utils/Demuxer.h>

static inline Uint64 u64_abs_diff(Uint64 a, Uint64 b) {
  return (a > b) ? (a - b) : (b - a);
}

void genWhiteNoise(void *parms, Uint8 **target, size_t *size) {
  (void)parms;
  const size_t sampleCount = 1024;
  const size_t bufferSize = sampleCount * sizeof(float);
  float *data = (float *)SDL_malloc(bufferSize);
  if (!data) {
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

struct Kotonoha_AudioDecode *KotonohaAudioInit(const char *path,
                                               SDL_AudioSpec specification,
                                               Kotonoha_IOMonitor *ioMonitor,
                                               Kotonoha_Demuxer *demuxer) {
  struct Kotonoha_AudioDecode *audioDecode =
      (struct Kotonoha_AudioDecode *)SDL_calloc(
          1, sizeof(struct Kotonoha_AudioDecode));
  if (!audioDecode)
    return NULL;

  KotonohaIOMonitorOperationInit(&audioDecode->ioOperation, ioMonitor);

  float cacheSecs = (audioDecode->cacheDurationSeconds > 0.1f)
                        ? audioDecode->cacheDurationSeconds
                        : 2.0f;

  audioDecode->demuxer = demuxer;
  if (demuxer != NULL) {
    audioDecode->stream = KotonohaDemuxerAddStream(demuxer, path, cacheSecs, audioDecode);
  }

  if (!audioDecode->stream || audioDecode->stream->audioStreamIdx < 0) {
    KotonohaAudioFree(audioDecode);
    return NULL;
  }

  audioDecode->formatCtx = audioDecode->stream->formatCtx;
  audioDecode->audioStreamIndex = audioDecode->stream->audioStreamIdx;

  if (!KotonohaFFmpegCreateDecoderCtx(audioDecode->formatCtx,
                                      audioDecode->audioStreamIndex,
                                      &audioDecode->codecCtx, NULL)) {
    KotonohaAudioFree(audioDecode);
    return NULL;
  }

  audioDecode->specification = specification;
  audioDecode->executions = 1;

  audioDecode->swrCtx = KotonohaFFmpegCreateAudioResampler(
      audioDecode->codecCtx, audioDecode->specification.channels,
      audioDecode->specification.freq, AV_SAMPLE_FMT_FLT);

  if (!audioDecode->swrCtx) {
    KotonohaAudioFree(audioDecode);
    return NULL;
  }

  return audioDecode;
}

void KotonohaAudioSeek(Kotonoha_AudioDecode *ctx, Uint64 timeMs) {
  if (!ctx || !ctx->stream || !ctx->codecCtx)
    return;

  KotonohaDemuxStreamSeek(ctx->stream, timeMs);
  avcodec_flush_buffers(ctx->codecCtx);

  if (ctx->swrCtx) {
    swr_convert(ctx->swrCtx, NULL, 0, NULL, 0);
    swr_close(ctx->swrCtx);
    swr_init(ctx->swrCtx);
  }

  ctx->executions++;
  ctx->audioTime = timeMs;
  ctx->lastTime = timeMs;
}

void KotonohaAudioEnsureSync(Kotonoha_AudioDecode *ctx, Uint64 currentTimeMs,
                             bool forceOnOpen) {
  if (!ctx)
    return;
  if (forceOnOpen || u64_abs_diff(currentTimeMs, ctx->audioTime) > 1000) {
    KotonohaAudioSeek(ctx, currentTimeMs);
  }
}

int KotonohaAudioRender(void *data, Uint8 **target, int *size) {
  Kotonoha_AudioDecode *instance = (Kotonoha_AudioDecode *)data;
  if (!instance || !instance->stream || !target || !size)
    return -1;

  *target = NULL;
  *size = 0;

  if (instance->tm != NULL && *instance->tm != NULL) {
    if (KotonohaTimeIsPaused(*instance->tm) && !instance->ioOperation.stalled)
      return 0;
  }

  AVFrame *pFrame = av_frame_alloc();
  if (!pFrame)
    return -1;

  AVStream *stream = instance->formatCtx->streams[instance->audioStreamIndex];

  while (true) {
    int ret = avcodec_receive_frame(instance->codecCtx, pFrame);
    if (ret == 0) {
      Uint64 framePtsMs =
          KotonohaFFmpegGetTime(stream, pFrame->pts, pFrame->pkt_dts);

      if (instance->tm != NULL && *instance->tm != NULL) {
        bool inRange;
        Sint64 diff;
        Uint64 currentTimeMs = KotonohaTimeGetFromEvent(
            *instance->tm, instance->start, instance->end, &inRange, &diff);

        if (framePtsMs + 300 < currentTimeMs &&
            instance->stream->audioQueue.count > 0) {
          av_frame_unref(pFrame);
          continue;
        }
      }

      int converted = KotonohaFFmpegConvertAudioFrame(
          instance->swrCtx, pFrame, instance->specification.channels,
          AV_SAMPLE_FMT_FLT, target, size);

      av_frame_free(&pFrame);
      if (converted <= 0)
        return converted < 0 ? -1 : 0;

      instance->audioTime = framePtsMs;
      instance->lastTime = framePtsMs;
      return 2;
    }

    if (ret == AVERROR(EAGAIN)) {
      Uint64 pktPts = 0;
      AVPacket *pkt =
          KotonohaPacketQueuePop(&instance->stream->audioQueue, &pktPts);

      if (pkt != NULL) {
        avcodec_send_packet(instance->codecCtx, pkt);
        av_packet_free(&pkt);
        continue;
      } else {
        SDL_LockMutex(instance->stream->audioQueue.mutex);
        bool isEof = instance->stream->audioQueue.eof;
        SDL_UnlockMutex(instance->stream->audioQueue.mutex);

        if (isEof) {
          avcodec_send_packet(instance->codecCtx, NULL);
        } else {
          av_frame_free(&pFrame);
          return 0;
        }
      }
    } else if (ret == AVERROR_EOF) {
      av_frame_free(&pFrame);
      return 1;
    } else {
      av_frame_free(&pFrame);
      return -1;
    }
  }
}

void KotonohaAudioFree(void *data) {
  if (data == NULL)
    return;
  struct Kotonoha_AudioDecode *audioDecode =
      (struct Kotonoha_AudioDecode *)data;

  if (audioDecode->loopMedia)
    KotonohaAudioFree(audioDecode->loopMedia);
  if (audioDecode->demuxer && audioDecode->stream)
    KotonohaDemuxerRemoveStream(audioDecode->demuxer, &audioDecode->stream);
  if (audioDecode->swrCtx)
    swr_free(&audioDecode->swrCtx);
  if (audioDecode->codecCtx)
    avcodec_free_context(&audioDecode->codecCtx);

  KotonohaIOMonitorOperationReset(&audioDecode->ioOperation);
  SDL_free(audioDecode);
}
