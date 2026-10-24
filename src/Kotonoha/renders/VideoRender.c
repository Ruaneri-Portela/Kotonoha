#include <Kotonoha/renders/VideoRender.h>
#include <Kotonoha/utils/FFmpegSDL.h>

static bool video_queue_state(Kotonoha_PacketQueue *queue, int *count,
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

static void wait_for_legacy_video_cache(Kotonoha_VideoData *decode) {
  if (decode == NULL || decode->stream == NULL || decode->ownedDemuxer == NULL)
    return;

  const Uint64 deadline = SDL_GetTicks() + 250;
  while (SDL_GetTicks() < deadline) {
    int count = 0;
    bool eof = false;
    video_queue_state(&decode->stream->videoQueue, &count, &eof);
    if (count > 0 || eof)
      return;
    SDL_Delay(1);
  }
}

static bool initialize_hw_context(Kotonoha_VideoData *instance) {
  instance->hwCtx = (Kotonoha_FFmpegHwContext *)SDL_calloc(
      1, sizeof(Kotonoha_FFmpegHwContext));
  return instance->hwCtx != NULL;
}

static AVFrame *get_cpu_readable_frame(AVFrame *src) {
  if (src == NULL)
    return NULL;
  if (src->hw_frames_ctx == NULL)
    return src;

  AVFrame *swFrame = av_frame_alloc();
  if (swFrame == NULL)
    return NULL;

  if (av_hwframe_transfer_data(swFrame, src, 0) < 0 ||
      av_frame_copy_props(swFrame, src) < 0) {
    av_frame_free(&swFrame);
    return NULL;
  }
  return swFrame;
}

static bool update_texture_from_frame(Kotonoha_VideoData *instance,
                                      SDL_Renderer *render, AVFrame *frame) {
  if (instance == NULL || render == NULL || frame == NULL)
    return false;

  AVFrame *cpuFrame = get_cpu_readable_frame(frame);
  if (cpuFrame == NULL)
    return false;

  if (instance->texture != NULL) {
    float textureWidth = 0.0f;
    float textureHeight = 0.0f;
    if (!SDL_GetTextureSize(instance->texture, &textureWidth, &textureHeight)) {
      if (cpuFrame != frame)
        av_frame_free(&cpuFrame);
      return false;
    }
    if ((int)textureWidth != cpuFrame->width ||
        (int)textureHeight != cpuFrame->height ||
        !instance->textureFormatInitialized ||
        instance->sourcePixelFormat != (enum AVPixelFormat)cpuFrame->format) {
      SDL_DestroyTexture(instance->texture);
      instance->texture = NULL;
      instance->textureFormat = 0;
      instance->textureFormatInitialized = false;
      instance->directTextureUploadFailed = false;
    }
  }

  bool directUpload = false;
  const Uint32 desiredFormat =
      KotonohaSDLFormatForAVFrame(cpuFrame, &directUpload);
  Uint32 textureFormat =
      directUpload && !instance->directTextureUploadFailed
          ? desiredFormat
          : SDL_PIXELFORMAT_RGBA32;

  if (instance->texture == NULL) {
    instance->texture = SDL_CreateTexture(
        render, textureFormat, SDL_TEXTUREACCESS_STREAMING, cpuFrame->width,
        cpuFrame->height);
    if (instance->texture == NULL && textureFormat != SDL_PIXELFORMAT_RGBA32) {
      textureFormat = SDL_PIXELFORMAT_RGBA32;
      instance->texture = SDL_CreateTexture(
          render, textureFormat, SDL_TEXTUREACCESS_STREAMING, cpuFrame->width,
          cpuFrame->height);
      instance->directTextureUploadFailed = directUpload;
    }
    if (instance->texture == NULL) {
      if (cpuFrame != frame)
        av_frame_free(&cpuFrame);
      return false;
    }
    instance->textureFormat = textureFormat;
    instance->sourcePixelFormat = (enum AVPixelFormat)cpuFrame->format;
    instance->textureFormatInitialized = true;
    if (!SDL_SetTextureBlendMode(
            instance->texture,
            KotonohaAVPixelFormatHasAlpha((enum AVPixelFormat)cpuFrame->format)
                ? SDL_BLENDMODE_BLEND
                : SDL_BLENDMODE_NONE)) {
      SDL_DestroyTexture(instance->texture);
      instance->texture = NULL;
      instance->textureFormat = 0;
      instance->textureFormatInitialized = false;
      if (cpuFrame != frame)
        av_frame_free(&cpuFrame);
      return false;
    }
  }

  if (directUpload && !instance->directTextureUploadFailed &&
      KotonohaSDLUpdateTextureFromAVFrame(instance->texture, cpuFrame)) {
    instance->textureSerial++;
    if (cpuFrame != frame)
      av_frame_free(&cpuFrame);
    return true;
  }

  if (directUpload && !instance->directTextureUploadFailed) {
    instance->directTextureUploadFailed = true;
  }

  if (instance->textureFormat != SDL_PIXELFORMAT_RGBA32) {
    SDL_DestroyTexture(instance->texture);
    instance->texture = SDL_CreateTexture(
        render, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STREAMING,
        cpuFrame->width, cpuFrame->height);
    if (instance->texture == NULL) {
      instance->textureFormat = 0;
      if (cpuFrame != frame)
        av_frame_free(&cpuFrame);
      return false;
    }
    instance->textureFormat = SDL_PIXELFORMAT_RGBA32;
    if (!SDL_SetTextureBlendMode(
            instance->texture,
            KotonohaAVPixelFormatHasAlpha((enum AVPixelFormat)cpuFrame->format)
                ? SDL_BLENDMODE_BLEND
                : SDL_BLENDMODE_NONE)) {
      SDL_DestroyTexture(instance->texture);
      instance->texture = NULL;
      instance->textureFormat = 0;
      instance->textureFormatInitialized = false;
      if (cpuFrame != frame)
        av_frame_free(&cpuFrame);
      return false;
    }
  }

  instance->swsCtx = sws_getCachedContext(
      instance->swsCtx, cpuFrame->width, cpuFrame->height,
      (enum AVPixelFormat)cpuFrame->format, cpuFrame->width, cpuFrame->height,
      AV_PIX_FMT_RGBA, SWS_LANCZOS, NULL, NULL, NULL);
  if (instance->swsCtx == NULL) {
    if (cpuFrame != frame)
      av_frame_free(&cpuFrame);
    return false;
  }

  if (instance->pFrameRGB == NULL)
    instance->pFrameRGB = av_frame_alloc();
  if (instance->pFrameRGB == NULL) {
    if (cpuFrame != frame)
      av_frame_free(&cpuFrame);
    return false;
  }

  if (instance->swsBuffer == NULL ||
      instance->swsBufferWidth != cpuFrame->width ||
      instance->swsBufferHeight != cpuFrame->height) {
    if (instance->swsBuffer != NULL)
      av_free(instance->swsBuffer);

    const int bufferSize = av_image_get_buffer_size(
        AV_PIX_FMT_RGBA, cpuFrame->width, cpuFrame->height, 1);
    instance->swsBuffer = (Uint8 *)av_malloc(bufferSize);
    if (instance->swsBuffer == NULL) {
      if (cpuFrame != frame)
        av_frame_free(&cpuFrame);
      return false;
    }

    instance->swsBufferWidth = cpuFrame->width;
    instance->swsBufferHeight = cpuFrame->height;
    if (av_image_fill_arrays(instance->pFrameRGB->data,
                             instance->pFrameRGB->linesize,
                             instance->swsBuffer, AV_PIX_FMT_RGBA,
                             cpuFrame->width, cpuFrame->height, 1) < 0) {
      if (cpuFrame != frame)
        av_frame_free(&cpuFrame);
      return false;
    }
  }

  const int scaledHeight =
      sws_scale(instance->swsCtx, (const uint8_t *const *)cpuFrame->data,
                cpuFrame->linesize, 0, cpuFrame->height,
                instance->pFrameRGB->data, instance->pFrameRGB->linesize);
  if (scaledHeight != cpuFrame->height) {
    if (cpuFrame != frame)
      av_frame_free(&cpuFrame);
    return false;
  }

  const bool updated =
      SDL_UpdateTexture(instance->texture, NULL, instance->pFrameRGB->data[0],
                        instance->pFrameRGB->linesize[0]);
  if (updated)
    instance->textureSerial++;

  if (cpuFrame != frame)
    av_frame_free(&cpuFrame);
  return updated;
}

static void reset_decoder_for_seek(Kotonoha_VideoData *instance,
                                   Uint64 targetMs) {
  if (instance == NULL || instance->stream == NULL ||
      instance->pCodecCtx == NULL)
    return;

  KotonohaDemuxStreamSeek(instance->stream, targetMs);
  avcodec_flush_buffers(instance->pCodecCtx);
  if (instance->currentFrame != NULL)
    av_frame_free(&instance->currentFrame);

  instance->videoTime = targetMs;
  instance->decoderFlushSent = false;
  instance->decoderEof = false;
  instance->demuxEof = false;
}

Kotonoha_VideoData *KotonohaVideoRenderInit(
    const char *filename, Kotonoha_Time *time, Uint64 startTime, Uint64 endTime,
    Kotonoha_SceneTick endTick, Kotonoha_Demuxer *demuxer) {
  if (filename == NULL || *filename == '\0' || time == NULL || demuxer == NULL)
    return NULL;

  Kotonoha_VideoData *video =
      (Kotonoha_VideoData *)SDL_calloc(1, sizeof(Kotonoha_VideoData));
  if (video == NULL)
    return NULL;

  video->demuxer = demuxer;
  video->stream =
      KotonohaDemuxerAddStream(demuxer, filename, video, AVMEDIA_TYPE_VIDEO);
  if (video->stream == NULL || video->stream->videoStreamIdx < 0) {
    KotonohaVideoRenderShutdown(&video);
    return NULL;
  }

  video->pFormatCtx = video->stream->formatCtx;
  video->videoStreamIndex = video->stream->videoStreamIdx;
  video->videoTimeBase = video->stream->videoTimeBase;
  if (!initialize_hw_context(video) ||
      !KotonohaFFmpegCreateDecoderCtx(video->pFormatCtx,
                                      video->videoStreamIndex,
                                      &video->pCodecCtx, video->hwCtx)) {
    KotonohaVideoRenderShutdown(&video);
    return NULL;
  }

  video->time = time;
  video->startTime = startTime;
  video->endTime = endTime;
  video->endTick = endTick;
  video->timeDiscontinuitySerial =
      KotonohaTimeGetDiscontinuitySerial(time);
  if (!KotonohaDemuxerActivateStream(demuxer, video->stream)) {
    KotonohaVideoRenderShutdown(&video);
    return NULL;
  }
  return video;
}

Kotonoha_videoData *Kotonoha_VideoRenderInit(const char *filename,
                                             Kotonoha_time *time,
                                             Uint64 startTime, Uint64 endTime,
                                             Kotonoha_SceneTick endTick) {
  static Kotonoha_Config legacyConfig = {0};
  Kotonoha_Demuxer *demuxer = KotonohaDemuxerCreate(false, &legacyConfig);
  if (demuxer == NULL) return NULL;

  Kotonoha_VideoData *video = KotonohaVideoRenderInit(
      filename, time, startTime, endTime, endTick, demuxer);
  if (video == NULL) {
    KotonohaDemuxerDestroy(&demuxer);
    return NULL;
  }

  video->ownedDemuxer = demuxer;
  KotonohaDemuxerStart(demuxer);
  wait_for_legacy_video_cache(video);
  return video;
}

void KotonohaVideoRenderShutdown(Kotonoha_VideoData **instance) {
  if (instance == NULL || *instance == NULL)
    return;

  Kotonoha_VideoData *video = *instance;
  if (video->demuxer != NULL && video->stream != NULL)
    KotonohaDemuxerRemoveStream(video->demuxer, &video->stream);

  if (video->currentFrame != NULL)
    av_frame_free(&video->currentFrame);
  if (video->pFrameRGB != NULL)
    av_frame_free(&video->pFrameRGB);
  if (video->swsBuffer != NULL)
    av_free(video->swsBuffer);
  if (video->swsCtx != NULL)
    sws_freeContext(video->swsCtx);
  if (video->pCodecCtx != NULL)
    avcodec_free_context(&video->pCodecCtx);
  if (video->hwCtx != NULL) {
    if (video->hwCtx->deviceRef != NULL)
      av_buffer_unref(&video->hwCtx->deviceRef);
    SDL_free(video->hwCtx);
  }
  if (video->texture != NULL)
    SDL_DestroyTexture(video->texture);

  if (video->ownedDemuxer != NULL) {
    Kotonoha_Demuxer *owned = video->ownedDemuxer;
    video->ownedDemuxer = NULL;
    video->demuxer = NULL;
    KotonohaDemuxerDestroy(&owned);
  }
  SDL_free(video);
  *instance = NULL;
}

void Kotonoha_VideoRenderShutdown(Kotonoha_videoData **instance) {
  KotonohaVideoRenderShutdown(instance);
}

static enum Kotonoha_Scene_Status
retained_or_waiting(Kotonoha_VideoData *instance) {
  return instance->texture != NULL ? KOTONOHA_SCENE_DRAW
                                   : KOTONOHA_SCENE_WAITING;
}

static enum Kotonoha_Scene_Status
present_latest_due_frame(Kotonoha_VideoData *instance, SDL_Renderer *render,
                         AVFrame **latestDue, Uint64 latestPtsMs) {
  if (latestDue == NULL || *latestDue == NULL)
    return retained_or_waiting(instance);
  if (!update_texture_from_frame(instance, render, *latestDue)) {
    av_frame_free(latestDue);
    return KOTONOHA_SCENE_FATAL_ERROR;
  }
  av_frame_free(latestDue);
  instance->videoTime = latestPtsMs;
  return KOTONOHA_SCENE_DRAW;
}

bool KotonohaVideoRenderPrime(Kotonoha_VideoData *instance, Uint32 waitMs) {
  if (instance == NULL || instance->stream == NULL ||
      instance->pCodecCtx == NULL)
    return false;
  if (instance->currentFrame != NULL || instance->texture != NULL)
    return true;

  AVStream *stream = instance->pFormatCtx->streams[instance->videoStreamIndex];
  AVFrame *frame = av_frame_alloc();
  if (frame == NULL)
    return false;

  const Uint64 deadline = SDL_GetTicks() + waitMs;
  for (;;) {
    const int received = avcodec_receive_frame(instance->pCodecCtx, frame);
    if (received == 0) {
      instance->currentPtsMs =
          KotonohaFFmpegGetTime(stream, frame->pts, frame->pkt_dts);
      instance->currentFrame = frame;
      return true;
    }

    if (received == AVERROR_EOF) {
      instance->decoderEof = true;
      av_frame_free(&frame);
      return false;
    }
    if (received != AVERROR(EAGAIN)) {
      av_frame_free(&frame);
      return false;
    }

    AVPacket *packet =
        KotonohaPacketQueuePop(&instance->stream->videoQueue, NULL);
    if (packet != NULL) {
      KotonohaDemuxStreamSetWaiting(instance->stream, false);
      const int sent = avcodec_send_packet(instance->pCodecCtx, packet);
      av_packet_free(&packet);
      if (sent < 0 && sent != AVERROR(EAGAIN)) {
        av_frame_free(&frame);
        return false;
      }
      continue;
    }

    bool queueEof = false;
    video_queue_state(&instance->stream->videoQueue, NULL, &queueEof);
    if (queueEof) {
      KotonohaDemuxStreamSetWaiting(instance->stream, false);
      if (!instance->decoderFlushSent) {
        const int sent = avcodec_send_packet(instance->pCodecCtx, NULL);
        if (sent == 0 || sent == AVERROR_EOF) {
          instance->decoderFlushSent = true;
          continue;
        }
        if (sent != AVERROR(EAGAIN)) {
          av_frame_free(&frame);
          return false;
        }
      }
      av_frame_free(&frame);
      return false;
    }

    if (SDL_GetTicks() >= deadline) {
      KotonohaDemuxStreamSetWaiting(instance->stream, true);
      av_frame_free(&frame);
      return false;
    }
    SDL_Delay(1);
  }
}

enum Kotonoha_Scene_Status KotonohaVideoRenderProcess(void *userData,
                                                      SDL_Renderer *render) {
  Kotonoha_VideoData *instance = (Kotonoha_VideoData *)userData;
  if (instance == NULL || instance->stream == NULL || instance->time == NULL ||
      instance->pCodecCtx == NULL)
    return KOTONOHA_SCENE_NULL;

  /* The authored event END, in the exact 24 Hz domain, owns lifetime. */
  if (Kotonoha_MillisecondsToSceneTick(KotonohaTimeGet(instance->time)) >=
      instance->endTick)
    return KOTONOHA_SCENE_COMPLETE;

  bool inRange = false;
  Sint64 diff = 0;
  const Uint64 currentTime = KotonohaTimeGetFromEvent(
      instance->time, instance->startTime, instance->endTime, &inRange, &diff);
  if (!inRange)
    return diff > 0 ? KOTONOHA_SCENE_COMPLETE : KOTONOHA_SCENE_NULL;

  if (instance->decoderEof) {
    /* EOF is terminal for this decoder instance. TitleView deliberately drives
     * its owner clock with absolute Set() calls and may rewind that clock after
     * EOF; a clock rewind must not implicitly restart/loop the movie. */
    return retained_or_waiting(instance);
  }

  const Uint64 discontinuitySerial =
      KotonohaTimeGetDiscontinuitySerial(instance->time);

  if (!instance->firstSeek) {
    /* PREPARE has already cached the opening packets.  A normal first render
     * happens slightly after the exact ORS START; do not flush that cache just
     * to chase ordinary scheduler jitter.  Reuse the existing 1 s timeline-jump
     * tolerance here; this threshold is an engine policy, not a recovered
     * retail rule. */
    if (currentTime > 1000)
      reset_decoder_for_seek(instance, currentTime);
    instance->firstSeek = true;
    instance->lastTimelineTime = currentTime;
    instance->hasTimelineTime = true;
    instance->timeDiscontinuitySerial = discontinuitySerial;
  } else if (discontinuitySerial != instance->timeDiscontinuitySerial) {
    /* Time owns discontinuity detection. Normal 12x/24x clock progression does
     * not change the serial. Absolute clock stepping is also used by TitleView,
     * so small monotonic forward steps are intentionally drained rather than
     * converted into demux seeks. Backward jumps cannot be decoded forward, and
     * large forward jumps should not synchronously drain an unbounded interval. */
    const bool backward = currentTime < instance->lastTimelineTime;
    const Uint64 forwardDelta = backward ? 0 : currentTime - instance->lastTimelineTime;
    instance->timeDiscontinuitySerial = discontinuitySerial;
    if (backward || forwardDelta > 1000) {
      reset_decoder_for_seek(instance, currentTime);
      instance->lastTimelineTime = currentTime;
      return retained_or_waiting(instance);
    }
  }
  instance->lastTimelineTime = currentTime;
  instance->hasTimelineTime = true;
  instance->lastTime = currentTime;

  if (instance->currentFrame != NULL && instance->currentPtsMs > currentTime)
    return retained_or_waiting(instance);

  /*
   * At accelerated playback rates the logical clock can cross many source
   * frames between two renderer callbacks.  Keep only the newest frame at or
   * before the logical target and retain the first future frame.  Returning on
   * the first overdue frame makes 12x/24x playback fall farther behind every
   * display frame even though the shared clock is correct.
   */
  AVFrame *latestDue = NULL;
  Uint64 latestDuePtsMs = instance->videoTime;
  if (instance->currentFrame != NULL) {
    latestDue = instance->currentFrame;
    latestDuePtsMs = instance->currentPtsMs;
    instance->currentFrame = NULL;
  }

  AVStream *stream = instance->pFormatCtx->streams[instance->videoStreamIndex];
  AVFrame *frame = av_frame_alloc();
  if (frame == NULL) {
    av_frame_free(&latestDue);
    return KOTONOHA_SCENE_FATAL_ERROR;
  }
  int legacyStarvationWaits = 0;

  for (;;) {
    const int received = avcodec_receive_frame(instance->pCodecCtx, frame);
    if (received == 0) {
      const Uint64 ptsMs =
          KotonohaFFmpegGetTime(stream, frame->pts, frame->pkt_dts);
      if (ptsMs <= currentTime) {
        av_frame_free(&latestDue);
        latestDue = frame;
        latestDuePtsMs = ptsMs;
        frame = av_frame_alloc();
        if (frame == NULL) {
          return present_latest_due_frame(instance, render, &latestDue,
                                          latestDuePtsMs);
        }
        continue;
      }

      instance->currentFrame = frame;
      instance->currentPtsMs = ptsMs;
      return present_latest_due_frame(instance, render, &latestDue,
                                      latestDuePtsMs);
    }

    if (received == AVERROR_EOF) {
      instance->decoderEof = true;
      av_frame_free(&frame);
      return present_latest_due_frame(instance, render, &latestDue,
                                      latestDuePtsMs);
    }

    if (received != AVERROR(EAGAIN)) {
      SDL_LogError(SDL_LOG_CATEGORY_RENDER, "Video decoder error: %s",
                   av_err2str(received));
      av_frame_free(&frame);
      av_frame_free(&latestDue);
      return KOTONOHA_SCENE_FATAL_ERROR;
    }

    Uint64 packetPts = 0;
    AVPacket *packet =
        KotonohaPacketQueuePop(&instance->stream->videoQueue, &packetPts);
    if (packet != NULL) {
      KotonohaDemuxStreamSetWaiting(instance->stream, false);
      const int sent = avcodec_send_packet(instance->pCodecCtx, packet);
      av_packet_free(&packet);
      if (sent < 0 && sent != AVERROR(EAGAIN)) {
        SDL_LogError(SDL_LOG_CATEGORY_RENDER, "Video packet send failed: %s",
                     av_err2str(sent));
        av_frame_free(&frame);
        av_frame_free(&latestDue);
        return KOTONOHA_SCENE_FATAL_ERROR;
      }
      continue;
    }

    bool queueEof = false;
    video_queue_state(&instance->stream->videoQueue, NULL, &queueEof);
    instance->demuxEof = queueEof;
    if (!queueEof) {
      if (instance->ownedDemuxer != NULL && legacyStarvationWaits < 10) {
        ++legacyStarvationWaits;
        SDL_Delay(1);
        continue;
      }
      /* Empty cache is starvation, not decoder EOF. Keep any valid texture. */
      KotonohaDemuxStreamSetWaiting(instance->stream, true);
      av_frame_free(&frame);
      return present_latest_due_frame(instance, render, &latestDue,
                                      latestDuePtsMs);
    }
    KotonohaDemuxStreamSetWaiting(instance->stream, false);

    if (!instance->decoderFlushSent) {
      const int sent = avcodec_send_packet(instance->pCodecCtx, NULL);
      if (sent == 0 || sent == AVERROR_EOF) {
        instance->decoderFlushSent = true;
        continue;
      }
      if (sent != AVERROR(EAGAIN)) {
        av_frame_free(&frame);
        av_frame_free(&latestDue);
        return KOTONOHA_SCENE_FATAL_ERROR;
      }
    }

    /* Once flush is pending, the next call will retry receive. Only
     * AVERROR_EOF from receive marks decoderEof. */
    av_frame_free(&frame);
    return present_latest_due_frame(instance, render, &latestDue,
                                    latestDuePtsMs);
  }
}

enum Kotonoha_Scene_Status Kotonoha_VideoRenderProcess(void *userData,
                                                       SDL_Renderer *render) {
  return KotonohaVideoRenderProcess(userData, render);
}
