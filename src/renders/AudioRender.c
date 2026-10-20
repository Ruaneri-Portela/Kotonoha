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
                                               Kotonoha_Demuxer *demuxer) {
  struct Kotonoha_AudioDecode *audioDecode =
      (struct Kotonoha_AudioDecode *)SDL_calloc(
          1, sizeof(struct Kotonoha_AudioDecode));
  if (!audioDecode)
    return NULL;

  float cacheSecs = (audioDecode->cacheDurationSeconds > 0.1f)
                        ? audioDecode->cacheDurationSeconds
                        : 2.0f;

  audioDecode->demuxer = demuxer;
  if (demuxer != NULL) {
    audioDecode->stream = KotonohaDemuxerAddStream(
        demuxer, path, cacheSecs, audioDecode, AVMEDIA_TYPE_AUDIO);
  }

  if (!audioDecode->stream || audioDecode->stream->audioStreamIdx < 0) {
    KotonohaAudioFree(audioDecode);
    return NULL;
  }

  audioDecode->audioStreamIndex = audioDecode->stream->audioStreamIdx;
  audioDecode->audioTimeBase = audioDecode->stream->audioTimeBase;

  if (!KotonohaFFmpegCreateDecoderCtx(audioDecode->stream->formatCtx,
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

  if (!KotonohaDemuxerActivateStream(demuxer, audioDecode->stream)) {
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
  ctx->cannotSeek = false;
  ctx->decoderFlushSent = false;
}

void KotonohaAudioEnsureSync(Kotonoha_AudioDecode *ctx, Uint64 currentTimeMs,
                             bool forceOnOpen) {
  if (!ctx)
    return;
  if (forceOnOpen || u64_abs_diff(currentTimeMs, ctx->audioTime) > 1000) {
    KotonohaAudioSeek(ctx, currentTimeMs);
  }
}

// Em AudioRender.c - KotonohaAudioRender ajustado
int KotonohaAudioRender(void *data, Uint8 **target, int *size) {
  Kotonoha_AudioDecode *instance = (Kotonoha_AudioDecode *)data;
  if (!instance || !instance->stream || !target || !size)
    return -1; //[cite: 1]

  *target = NULL;
  *size = 0;

  // One-shot audio (start == end == 0) keeps playing while the game is paused
  const bool oneShot = instance->start == 0 && instance->end == 0;
  if (!oneShot && instance->tm != NULL && *instance->tm != NULL) {
    if (KotonohaTimeIsPaused(*instance->tm))
      return 0;
  }

  if (!instance->firstSeek) {
    if (instance->lastTime > 0) {
      KotonohaAudioSeek(instance, instance->lastTime);
    }
    instance->firstSeek = true;
  }

  AVFrame *pFrame = av_frame_alloc();
  if (!pFrame)
    return -1;

  while (true) {
    // 1. Tenta receber um frame decodificado do codec
    int ret = avcodec_receive_frame(instance->codecCtx, pFrame); //[cite: 1]
    if (ret == 0) {
      Uint64 framePtsMs = 0;
      if (pFrame->pts != AV_NOPTS_VALUE) {
        framePtsMs = (Uint64)(pFrame->pts *
                              av_q2d(instance->audioTimeBase) * 1000.0);
      } else if (pFrame->pkt_dts != AV_NOPTS_VALUE) {
        framePtsMs = (Uint64)(pFrame->pkt_dts *
                              av_q2d(instance->audioTimeBase) * 1000.0);
      }

      int converted = KotonohaFFmpegConvertAudioFrame(
          instance->swrCtx, pFrame, instance->specification.channels,
          AV_SAMPLE_FMT_FLT, target, size); //[cite: 1, 4]

      av_frame_free(&pFrame); //[cite: 1]
      if (converted <= 0)
        return converted < 0 ? -1 : 0; //[cite: 1]

      instance->audioTime = framePtsMs; //[cite: 1]
      instance->lastTime = framePtsMs;  //[cite: 1]
      return 2; // Frame decodificado e convertido com sucesso!
    }

    // 2. Se o decodificador precisar de mais pacotes de entrada
    if (ret == AVERROR(EAGAIN)) {
      Uint64 pktPts = 0;
      AVPacket *pkt = KotonohaPacketQueuePop(&instance->stream->audioQueue,
                                             &pktPts); //[cite: 1]

      if (pkt != NULL) {
        KotonohaDemuxStreamSetWaiting(instance->stream, false);
        const int sendResult = avcodec_send_packet(instance->codecCtx, pkt);
        av_packet_free(&pkt);                         //[cite: 1]
        if (sendResult < 0 && sendResult != AVERROR(EAGAIN)) {
          av_frame_free(&pFrame);
          return -1;
        }
        continue; // Tenta extrair o frame novamente
      } else {
        if (KotonohaPacketQueueIsEof(&instance->stream->audioQueue)) {
          if (!instance->decoderFlushSent) {
            avcodec_send_packet(instance->codecCtx, NULL);
            instance->decoderFlushSent = true;
            continue;
          }
          av_frame_free(&pFrame);
          return 1;
        } else {
          KotonohaDemuxStreamSetWaiting(instance->stream, true);
          av_frame_free(&pFrame);
          return 0;
        }
      }
    } else if (ret == AVERROR_EOF) {
      av_frame_free(&pFrame); //[cite: 1]
      return 1;               // Fim do fluxo de áudio
    } else {
      av_frame_free(&pFrame); //[cite: 1]
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

  SDL_free(audioDecode);
}