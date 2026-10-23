#include <Kotonoha/renders/AudioRender.h>
#include <Kotonoha/utils/Log.h>
#include <math.h>

static const double school_days_audio_rates[] = {1.0, 2.0, 4.0, 12.0, 24.0};

int KotonohaSchoolDaysAudioRateIndex(double requestedRate) {
  for (int i = 0; i < 5; ++i) {
    if (fabs(requestedRate - school_days_audio_rates[i]) < 0.000001)
      return i;
  }
  return -1;
}

bool KotonohaSchoolDaysAudioRateNeedsSourceResync(int oldIndex, int newIndex) {
  return oldIndex >= 3 && oldIndex <= 4 && newIndex >= 0 && newIndex <= 2;
}

bool KotonohaSchoolDaysAudioRatePlanInit(
    int sourceSampleRate, int processingSampleRate, double requestedRate,
    Kotonoha_SchoolDaysAudioRatePlan *plan) {
  if (plan == NULL || sourceSampleRate <= 0 || processingSampleRate <= 0 ||
      KotonohaSchoolDaysAudioRateIndex(requestedRate) < 0)
    return false;

  const double softwareFactor = requestedRate < 2.0 ? requestedRate : 2.0;
  const int windowFrames = (processingSampleRate * 50 + 999) / 1000;
  const int overlapLimit = (processingSampleRate * 8 + 999) / 1000;
  const int inputFrames = (int)ceil((double)windowFrames * softwareFactor);
  const int overlapFrames =
      SDL_min(overlapLimit, inputFrames - windowFrames);
  const double residualRate =
      ((double)windowFrames * requestedRate) / (double)inputFrames;
  const double requestedFrequency = (double)sourceSampleRate * residualRate;
  const double hardwareFrequency = SDL_min(requestedFrequency, 100000.0);

  plan->sourceSampleRate = sourceSampleRate;
  plan->processingSampleRate = processingSampleRate;
  plan->requestedRate = requestedRate;
  plan->softwareFactor = softwareFactor;
  plan->windowFrames = windowFrames;
  plan->overlapFrames = overlapFrames;
  plan->inputFrames = inputFrames;
  plan->skipFrames = inputFrames - windowFrames - overlapFrames;
  plan->prefixFrames = windowFrames - overlapFrames;
  plan->residualRate = residualRate;
  plan->hardwareFrequency = hardwareFrequency;
  plan->hardwareRatio = hardwareFrequency / (double)sourceSampleRate;
  return true;
}

bool KotonohaSchoolDaysAudioOlaBlock(
    const float *source, int sourceFrames, int channels,
    const Kotonoha_SchoolDaysAudioRatePlan *plan, float *output,
    int outputFrames) {
  if (source == NULL || output == NULL || plan == NULL || channels <= 0 ||
      sourceFrames < plan->inputFrames || outputFrames < plan->windowFrames)
    return false;

  const int prefix = plan->prefixFrames;
  const int overlap = plan->overlapFrames;
  const int input = plan->inputFrames;
  if (prefix > 0) {
    SDL_memcpy(output, source,
               (size_t)prefix * (size_t)channels * sizeof(float));
  }

  for (int i = 0; i < overlap; ++i) {
    const double leftWeight = (double)(overlap - i) / (double)(overlap + 1);
    const double rightWeight = (double)(i + 1) / (double)(overlap + 1);
    for (int channel = 0; channel < channels; ++channel) {
      const float left = source[(prefix + i) * channels + channel];
      const float right = source[(input - overlap + i) * channels + channel];
      output[(prefix + i) * channels + channel] =
          (float)((double)left * leftWeight + (double)right * rightWeight);
    }
  }
  return true;
}

static void audio_rate_processor_reset(Kotonoha_AudioDecode *decode) {
  if (decode == NULL)
    return;
  if (decode->schoolDaysRateStream != NULL) {
    SDL_DestroyAudioStream(decode->schoolDaysRateStream);
    decode->schoolDaysRateStream = NULL;
  }
  decode->schoolDaysOlaFrames = 0;
}

static void audio_seek_internal(Kotonoha_AudioDecode *ctx, Uint64 timeMs,
                                bool countExecution);

bool KotonohaAudioSetSchoolDaysPlaybackRate(Kotonoha_AudioDecode *ctx,
                                            double requestedRate) {
  if (ctx == NULL)
    return false;
  const int newIndex = KotonohaSchoolDaysAudioRateIndex(requestedRate);
  if (newIndex < 0)
    return false;

  const int oldIndex = ctx->schoolDaysPlaybackRateIndex;
  if (oldIndex == newIndex)
    return true;

  const bool resyncSource =
      KotonohaSchoolDaysAudioRateNeedsSourceResync(oldIndex, newIndex);
  ctx->schoolDaysPlaybackRateIndex = newIndex;
  ctx->schoolDaysPlaybackRate = requestedRate;
  audio_rate_processor_reset(ctx);

  if (resyncSource &&
      (ctx->firstSeek || ctx->hasTargetTime || ctx->audioTime != 0)) {
    Uint64 resyncTime = ctx->targetTime;
    if (resyncTime == 0 && ctx->audioTime != 0)
      resyncTime = ctx->audioTime;
    audio_seek_internal(ctx, resyncTime, false);
  }
  return true;
}

static Uint64 u64_abs_diff(Uint64 a, Uint64 b) {
  return (a > b) ? (a - b) : (b - a);
}

static bool audio_queue_state(Kotonoha_PacketQueue *queue, int *count,
                              bool *eof) {
  if (queue == NULL || queue->mutex == NULL)
    return false;
  SDL_LockMutex(queue->mutex);
  if (count != NULL)
    *count = queue->count;
  if (eof != NULL)
    *eof = queue->eof;
  SDL_UnlockMutex(queue->mutex);
  return true;
}

static void wait_for_legacy_audio_cache(Kotonoha_AudioDecode *decode) {
  if (decode == NULL || decode->stream == NULL || decode->ownedDemuxer == NULL)
    return;

  const Uint64 deadline = SDL_GetTicks() + 250;
  while (SDL_GetTicks() < deadline) {
    int count = 0;
    bool eof = false;
    audio_queue_state(&decode->stream->audioQueue, &count, &eof);
    if (count > 0 || eof)
      return;
    SDL_Delay(1);
  }
}

static bool audio_rate_plan_for_decode(
    Kotonoha_AudioDecode *decode, Kotonoha_SchoolDaysAudioRatePlan *plan) {
  if (decode == NULL || decode->codecCtx == NULL || plan == NULL)
    return false;
  const int sourceSampleRate = decode->codecCtx->sample_rate > 0
                                   ? decode->codecCtx->sample_rate
                                   : decode->specification.freq;
  return KotonohaSchoolDaysAudioRatePlanInit(
      sourceSampleRate, decode->specification.freq,
      decode->schoolDaysPlaybackRate, plan);
}

static bool audio_rate_ensure_ola_capacity(Kotonoha_AudioDecode *decode,
                                           size_t requiredFrames) {
  if (requiredFrames <= decode->schoolDaysOlaCapacityFrames)
    return true;

  size_t capacity = decode->schoolDaysOlaCapacityFrames != 0
                        ? decode->schoolDaysOlaCapacityFrames
                        : 4096;
  while (capacity < requiredFrames) {
    if (capacity > SIZE_MAX / 2)
      return false;
    capacity *= 2;
  }

  const size_t channels = (size_t)decode->specification.channels;
  if (channels == 0 || capacity > SIZE_MAX / channels ||
      capacity * channels > SIZE_MAX / sizeof(float))
    return false;
  float *buffer = (float *)SDL_realloc(
      decode->schoolDaysOlaBuffer, capacity * channels * sizeof(float));
  if (buffer == NULL)
    return false;
  decode->schoolDaysOlaBuffer = buffer;
  decode->schoolDaysOlaCapacityFrames = capacity;
  return true;
}

static int audio_rate_stream_output(
    Kotonoha_AudioDecode *decode,
    const Kotonoha_SchoolDaysAudioRatePlan *plan, float *softwareOutput,
    size_t softwareFrames, bool final, Uint8 **target, int *size) {
  const int channels = decode->specification.channels;
  if (fabs(plan->hardwareRatio - 1.0) < 0.000001) {
    if (softwareFrames == 0) {
      SDL_free(softwareOutput);
      return 0;
    }
    *target = (Uint8 *)softwareOutput;
    *size = (int)(softwareFrames * (size_t)channels * sizeof(float));
    return 1;
  }

  if (decode->schoolDaysRateStream == NULL) {
    SDL_AudioSpec spec = decode->specification;
    decode->schoolDaysRateStream = SDL_CreateAudioStream(&spec, &spec);
    if (decode->schoolDaysRateStream == NULL ||
        !SDL_SetAudioStreamFrequencyRatio(
            decode->schoolDaysRateStream, (float)plan->hardwareRatio)) {
      SDL_LogError(SDL_LOG_CATEGORY_AUDIO,
                   "School Days playback-rate stream init failed: %s",
                   SDL_GetError());
      SDL_free(softwareOutput);
      audio_rate_processor_reset(decode);
      return -1;
    }
  }

  if (softwareFrames != 0) {
    const int softwareBytes =
        (int)(softwareFrames * (size_t)channels * sizeof(float));
    if (!SDL_PutAudioStreamData(decode->schoolDaysRateStream, softwareOutput,
                                softwareBytes)) {
      SDL_LogError(SDL_LOG_CATEGORY_AUDIO,
                   "School Days playback-rate stream write failed: %s",
                   SDL_GetError());
      SDL_free(softwareOutput);
      return -1;
    }
  }
  SDL_free(softwareOutput);

  if (final && !SDL_FlushAudioStream(decode->schoolDaysRateStream)) {
    SDL_LogError(SDL_LOG_CATEGORY_AUDIO,
                 "School Days playback-rate stream flush failed: %s",
                 SDL_GetError());
    return -1;
  }

  const int available =
      SDL_GetAudioStreamAvailable(decode->schoolDaysRateStream);
  if (available < 0) {
    SDL_LogError(SDL_LOG_CATEGORY_AUDIO,
                 "School Days playback-rate stream query failed: %s",
                 SDL_GetError());
    return -1;
  }
  if (available == 0)
    return 0;

  Uint8 *output = (Uint8 *)SDL_malloc((size_t)available);
  if (output == NULL)
    return -1;
  const int received = SDL_GetAudioStreamData(
      decode->schoolDaysRateStream, output, available);
  if (received <= 0) {
    SDL_free(output);
    if (received < 0) {
      SDL_LogError(SDL_LOG_CATEGORY_AUDIO,
                   "School Days playback-rate stream read failed: %s",
                   SDL_GetError());
      return -1;
    }
    return 0;
  }
  *target = output;
  *size = received;
  return 1;
}

static int audio_rate_process(Kotonoha_AudioDecode *decode, Uint8 **target,
                              int *size, bool final) {
  if (decode == NULL || target == NULL || size == NULL)
    return -1;
  if (decode->schoolDaysPlaybackRateIndex == 0)
    return final ? 0 : ((*target != NULL && *size > 0) ? 1 : 0);
  if (decode->specification.format != SDL_AUDIO_F32 ||
      decode->specification.channels <= 0)
    return -1;

  Kotonoha_SchoolDaysAudioRatePlan plan;
  if (!audio_rate_plan_for_decode(decode, &plan))
    return -1;

  const int channels = decode->specification.channels;
  if (*target != NULL && *size > 0) {
    const size_t frameBytes = (size_t)channels * sizeof(float);
    if ((size_t)*size % frameBytes != 0)
      return -1;
    const size_t incomingFrames = (size_t)*size / frameBytes;
    const size_t required = decode->schoolDaysOlaFrames + incomingFrames;
    if (!audio_rate_ensure_ola_capacity(decode, required))
      return -1;
    SDL_memcpy(decode->schoolDaysOlaBuffer +
                   decode->schoolDaysOlaFrames * (size_t)channels,
               *target, incomingFrames * frameBytes);
    decode->schoolDaysOlaFrames = required;
    SDL_free(*target);
    *target = NULL;
    *size = 0;
  }

  const size_t inputFrames = (size_t)plan.inputFrames;
  if (final && decode->schoolDaysOlaFrames != 0) {
    const size_t remainder = decode->schoolDaysOlaFrames % inputFrames;
    if (remainder != 0) {
      const size_t padded = decode->schoolDaysOlaFrames +
                            (inputFrames - remainder);
      if (!audio_rate_ensure_ola_capacity(decode, padded))
        return -1;
      SDL_memset(decode->schoolDaysOlaBuffer +
                     decode->schoolDaysOlaFrames * (size_t)channels,
                 0, (padded - decode->schoolDaysOlaFrames) *
                        (size_t)channels * sizeof(float));
      decode->schoolDaysOlaFrames = padded;
    }
  }

  const size_t blocks = decode->schoolDaysOlaFrames / inputFrames;
  const size_t outputFrames = blocks * (size_t)plan.windowFrames;
  float *softwareOutput = NULL;
  if (outputFrames != 0) {
    softwareOutput = (float *)SDL_malloc(
        outputFrames * (size_t)channels * sizeof(float));
    if (softwareOutput == NULL)
      return -1;
    for (size_t block = 0; block < blocks; ++block) {
      const float *source = decode->schoolDaysOlaBuffer +
                            block * inputFrames * (size_t)channels;
      float *output = softwareOutput +
                      block * (size_t)plan.windowFrames * (size_t)channels;
      if (!KotonohaSchoolDaysAudioOlaBlock(
              source, plan.inputFrames, channels, &plan, output,
              plan.windowFrames)) {
        SDL_free(softwareOutput);
        return -1;
      }
    }

    const size_t consumed = blocks * inputFrames;
    const size_t remaining = decode->schoolDaysOlaFrames - consumed;
    if (remaining != 0) {
      SDL_memmove(decode->schoolDaysOlaBuffer,
                  decode->schoolDaysOlaBuffer +
                      consumed * (size_t)channels,
                  remaining * (size_t)channels * sizeof(float));
    }
    decode->schoolDaysOlaFrames = remaining;
  }

  return audio_rate_stream_output(decode, &plan, softwareOutput, outputFrames,
                                  final, target, size);
}

void genWhiteNoise(void *parms, Uint8 **target, size_t *size) {
  (void)parms;
  if (target == NULL || size == NULL)
    return;

  const size_t sampleCount = 1024;
  const size_t bufferSize = sampleCount * sizeof(float);
  float *data = (float *)SDL_malloc(bufferSize);
  if (data == NULL) {
    *target = NULL;
    *size = 0;
    return;
  }

  for (size_t i = 0; i < sampleCount; ++i)
    data[i] = (2.0f * ((float)rand() / (float)RAND_MAX) - 1.0f) * 0.01f;

  *target = (Uint8 *)data;
  *size = bufferSize;
}

Kotonoha_AudioDecode *KotonohaAudioInit(const char *path,
                                        SDL_AudioSpec specification,
                                        Kotonoha_Demuxer *demuxer) {
  if (path == NULL || *path == '\0' || demuxer == NULL)
    return NULL;

  Kotonoha_AudioDecode *audioDecode =
      (Kotonoha_AudioDecode *)SDL_calloc(1, sizeof(Kotonoha_AudioDecode));
  if (audioDecode == NULL)
    return NULL;

  audioDecode->stateMutex = SDL_CreateMutex();
  if (audioDecode->stateMutex == NULL) {
    SDL_free(audioDecode);
    return NULL;
  }

  audioDecode->demuxer = demuxer;
  audioDecode->cacheDurationSeconds = 2.0f;
  audioDecode->specification = specification;
  audioDecode->executions = 1;
  audioDecode->schoolDaysPlaybackRateIndex = 0;
  audioDecode->schoolDaysPlaybackRate = 1.0;
  SDL_SetAtomicInt(&audioDecode->enabled, 1);

  audioDecode->stream =
      KotonohaDemuxerAddStream(demuxer, path, audioDecode, AVMEDIA_TYPE_AUDIO);
  if (audioDecode->stream == NULL || audioDecode->stream->audioStreamIdx < 0) {
    KotonohaAudioFree(audioDecode);
    return NULL;
  }

  audioDecode->formatCtx = audioDecode->stream->formatCtx;
  audioDecode->audioStreamIndex = audioDecode->stream->audioStreamIdx;
  audioDecode->audioTimeBase = audioDecode->stream->audioTimeBase;

  if (!KotonohaFFmpegCreateDecoderCtx(audioDecode->formatCtx,
                                      audioDecode->audioStreamIndex,
                                      &audioDecode->codecCtx, NULL)) {
    KotonohaAudioFree(audioDecode);
    return NULL;
  }

  audioDecode->swrCtx = KotonohaFFmpegCreateAudioResampler(
      audioDecode->codecCtx, audioDecode->specification.channels,
      audioDecode->specification.freq, AV_SAMPLE_FMT_FLT);
  if (audioDecode->swrCtx == NULL) {
    KotonohaAudioFree(audioDecode);
    return NULL;
  }

  if (!KotonohaDemuxerActivateStream(demuxer, audioDecode->stream)) {
    KotonohaAudioFree(audioDecode);
    return NULL;
  }
  return audioDecode;
}

Kotonoha_audioDecode *Kotonoha_AudioInit(const char *path,
                                         SDL_AudioSpec specification) {
  static Kotonoha_Config legacyConfig = {0};
  Kotonoha_Demuxer *demuxer = KotonohaDemuxerCreate(false, &legacyConfig);
  if (demuxer == NULL) return NULL;

  Kotonoha_AudioDecode *decode =
      KotonohaAudioInit(path, specification, demuxer);
  if (decode == NULL) {
    KotonohaDemuxerDestroy(&demuxer);
    return NULL;
  }

  decode->ownedDemuxer = demuxer;
  KotonohaDemuxerStart(demuxer);
  wait_for_legacy_audio_cache(decode);
  return decode;
}

static void audio_seek_internal(Kotonoha_AudioDecode *ctx, Uint64 timeMs,
                                bool countExecution) {
  if (ctx == NULL)
    return;

  audio_rate_processor_reset(ctx);
  if (ctx->stream == NULL || ctx->codecCtx == NULL)
    return;

  KotonohaDemuxStreamSeek(ctx->stream, timeMs);
  avcodec_flush_buffers(ctx->codecCtx);

  if (ctx->swrCtx != NULL) {
    swr_close(ctx->swrCtx);
    swr_init(ctx->swrCtx);
  }

  if (countExecution)
    ctx->executions++;
  if (ctx->stateMutex != NULL) SDL_LockMutex(ctx->stateMutex);
  ctx->audioTime = timeMs;
  ctx->lastTime = timeMs;
  ctx->targetTime = timeMs;
  ctx->lastTargetTime = timeMs;
  if (ctx->stateMutex != NULL) SDL_UnlockMutex(ctx->stateMutex);
  ctx->hasTargetTime = true;
  ctx->firstSeek = true;
  ctx->cannotSeek = false;
  ctx->decoderFlushSent = false;
  ctx->decoderEof = false;
}

void KotonohaAudioSeek(Kotonoha_AudioDecode *ctx, Uint64 timeMs) {
  audio_seek_internal(ctx, timeMs, true);
}

void Kotonoha_AudioSeek(Kotonoha_audioDecode *ctx, Uint64 timeMs) {
  KotonohaAudioSeek(ctx, timeMs);
}

static int audio_send_flush_if_needed(Kotonoha_AudioDecode *instance) {
  if (instance->decoderFlushSent)
    return 0;

  const int sent = avcodec_send_packet(instance->codecCtx, NULL);
  if (sent == 0 || sent == AVERROR_EOF) {
    instance->decoderFlushSent = true;
    return 1;
  }
  if (sent == AVERROR(EAGAIN))
    return 0;
  return -1;
}

int KotonohaAudioRender(void *data, Uint8 **target, int *size) {
  Kotonoha_AudioDecode *instance = (Kotonoha_AudioDecode *)data;
  if (instance == NULL || instance->stream == NULL || instance->codecCtx == NULL ||
      target == NULL || size == NULL)
    return -1;

  *target = NULL;
  *size = 0;

  if (instance->decoderEof)
    return 1;

  if (instance->tm != NULL && *instance->tm != NULL &&
      !(instance->start == 0 && instance->end == 0) &&
      KotonohaTimeIsPaused(*instance->tm))
    return 0;

  const Uint64 targetTime = instance->targetTime;
  if (!instance->firstSeek) {
    /* PREPARE has already filled the shared demux queue from the beginning of
     * the event.  The first audio callback normally arrives slightly after
     * START; seeking for that small scheduler offset throws the prepared cache
     * away and can make short SE/voice windows go silent while it refills.
     * Treat only an initial displacement above the existing 1 s jump tolerance
     * as a seek; this is a cache/scheduling policy, not a recovered retail rule. */
    if (targetTime > 1000)
      audio_seek_internal(instance, targetTime, false);
    else
      instance->firstSeek = true;
    instance->lastTargetTime = targetTime;
    instance->hasTargetTime = true;
  } else if (instance->hasTargetTime && !instance->decoderEof &&
             u64_abs_diff(targetTime, instance->lastTargetTime) > 1000) {
    audio_seek_internal(instance, targetTime, false);
  }
  instance->lastTargetTime = targetTime;
  instance->hasTargetTime = true;

  AVFrame *frame = av_frame_alloc();
  if (frame == NULL)
    return -1;

  AVStream *stream = instance->formatCtx->streams[instance->audioStreamIndex];
  int legacyStarvationWaits = 0;

  for (;;) {
    const int received = avcodec_receive_frame(instance->codecCtx, frame);
    if (received == 0) {
      const Uint64 framePtsMs =
          KotonohaFFmpegGetTime(stream, frame->pts, frame->pkt_dts);

      if (framePtsMs + 1000 < targetTime) {
        av_frame_unref(frame);
        continue;
      }

      const int converted = KotonohaFFmpegConvertAudioFrame(
          instance->swrCtx, frame, instance->specification.channels,
          AV_SAMPLE_FMT_FLT, target, size);
      instance->audioTime = framePtsMs;
      instance->lastTime = framePtsMs;
      av_frame_unref(frame);
      if (converted < 0) {
        av_frame_free(&frame);
        return -1;
      }
      if (converted == 0)
        continue;

      const int processed = audio_rate_process(instance, target, size, false);
      if (processed < 0) {
        av_frame_free(&frame);
        return -1;
      }
      if (processed == 0)
        continue;

      av_frame_free(&frame);
      return 2;
    }

    if (received == AVERROR_EOF) {
      instance->decoderEof = true;
      const int drained = audio_rate_process(instance, target, size, true);
      av_frame_free(&frame);
      if (drained < 0)
        return -1;
      return drained > 0 ? 2 : 1;
    }

    if (received != AVERROR(EAGAIN)) {
      SDL_LogError(SDL_LOG_CATEGORY_AUDIO, "Audio decoder error: %s",
                   av_err2str(received));
      av_frame_free(&frame);
      return -1;
    }

    Uint64 packetPts = 0;
    AVPacket *packet =
        KotonohaPacketQueuePop(&instance->stream->audioQueue, &packetPts);
    if (packet != NULL) {
      KotonohaDemuxStreamSetWaiting(instance->stream, false);
      const int sent = avcodec_send_packet(instance->codecCtx, packet);
      av_packet_free(&packet);
      if (sent < 0 && sent != AVERROR(EAGAIN)) {
        SDL_LogError(SDL_LOG_CATEGORY_AUDIO, "Audio packet send failed: %s",
                     av_err2str(sent));
        av_frame_free(&frame);
        return -1;
      }
      continue;
    }

    bool queueEof = false;
    audio_queue_state(&instance->stream->audioQueue, NULL, &queueEof);
    if (!queueEof) {
      if (instance->ownedDemuxer != NULL && legacyStarvationWaits < 10) {
        ++legacyStarvationWaits;
        SDL_Delay(1);
        continue;
      }
      /* Cache starvation is a wait, never decoder EOF. */
      KotonohaDemuxStreamSetWaiting(instance->stream, true);
      av_frame_free(&frame);
      return 0;
    }
    KotonohaDemuxStreamSetWaiting(instance->stream, false);

    const int flush = audio_send_flush_if_needed(instance);
    if (flush < 0) {
      av_frame_free(&frame);
      return -1;
    }
    if (flush == 0) {
      av_frame_free(&frame);
      return 0;
    }
    /* Flush accepted: loop once more. AVERROR_EOF is the sole EOF signal. */
  }
}

int Kotonoha_AudioRender(void *data, Uint8 **target, int *size) {
  return KotonohaAudioRender(data, target, size);
}

void KotonohaAudioFree(void *data) {
  Kotonoha_AudioDecode *decode = (Kotonoha_AudioDecode *)data;
  if (decode == NULL)
    return;

  if (decode->loopMedia != NULL) {
    Kotonoha_AudioDecode *loop = decode->loopMedia;
    decode->loopMedia = NULL;
    KotonohaAudioFree(loop);
  }

  if (decode->demuxer != NULL && decode->stream != NULL)
    KotonohaDemuxerRemoveStream(decode->demuxer, &decode->stream);

  if (decode->swrCtx != NULL)
    swr_free(&decode->swrCtx);
  if (decode->codecCtx != NULL)
    avcodec_free_context(&decode->codecCtx);
  if (decode->stateMutex != NULL)
    SDL_DestroyMutex(decode->stateMutex);

  audio_rate_processor_reset(decode);
  SDL_free(decode->schoolDaysOlaBuffer);
  decode->schoolDaysOlaBuffer = NULL;
  decode->schoolDaysOlaCapacityFrames = 0;

  if (decode->ownedDemuxer != NULL) {
    Kotonoha_Demuxer *owned = decode->ownedDemuxer;
    decode->ownedDemuxer = NULL;
    decode->demuxer = NULL;
    KotonohaDemuxerDestroy(&owned);
  }
  SDL_free(decode);
}

void Kotonoha_AudioFree(void *data) { KotonohaAudioFree(data); }
