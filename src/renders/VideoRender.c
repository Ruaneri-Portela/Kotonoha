#include <Kotonoha/renders/VideoRender.h>
#include <Kotonoha/speciais/Upscaling.h>

static inline Uint64 u64_abs_diff(Uint64 a, Uint64 b) {
  return (a > b) ? (a - b) : (b - a);
}

static inline Uint64 KotonohaOrsSceneMsToFrame(Uint64 sceneMs) {
  return 1 + (sceneMs * 24) / 1000;
}

static bool initializeHwContext(struct Kotonoha_VideoData *instance) {
  instance->hwCtx = (struct Kotonoha_FFmpegHwContext *)SDL_calloc(
      1, sizeof(struct Kotonoha_FFmpegHwContext));
  if (!instance->hwCtx) {
    SDL_LogError(SDL_LOG_CATEGORY_ERROR,
                 "Failed to allocate hardware context: %s\n", SDL_GetError());
    return false;
  }
  return true;
}

static void Kotonoha_VideoSeekInternal(struct Kotonoha_VideoData *instance,
                                       Uint64 timeMs) {
  if (!instance || !instance->pFormatCtx || !instance->pCodecCtx)
    return;

  AVStream *stream = instance->pFormatCtx->streams[instance->videoStreamIndex];
  int64_t seekTs =
      av_rescale_q((int64_t)timeMs, (AVRational){1, 1000}, stream->time_base);

  KotonohaIOMonitorOperationBegin(&instance->ioOperation);
  const int seekResult = KotonohaFFmpegSeekFrame(
      instance->pFormatCtx, &instance->ioOperation, instance->videoStreamIndex,
      seekTs, AVSEEK_FLAG_BACKWARD);
  KotonohaIOMonitorOperationEnd(&instance->ioOperation);

  if (seekResult < 0) {
    if (seekResult != AVERROR_EXIT) {
      SDL_LogError(SDL_LOG_CATEGORY_ERROR,
                   "Video SeekError timeMs=%llu seekTs=%lld\n",
                   (unsigned long long)timeMs, (long long)seekTs);
    }
    return;
  }

  avcodec_flush_buffers(instance->pCodecCtx);

  instance->videoTime = timeMs;
  instance->lastTime = timeMs;
  instance->demuxEof = false;
  instance->decoderEof = false;
}

static AVFrame *getCpuReadableFrame(AVFrame *src) {
  if (!src)
    return NULL;

  if (!src->hw_frames_ctx)
    return src;

  AVFrame *swFrame = av_frame_alloc();
  if (!swFrame)
    return NULL;

  if (av_hwframe_transfer_data(swFrame, src, 0) < 0) {
    av_frame_free(&swFrame);
    return NULL;
  }

  if (av_frame_copy_props(swFrame, src) < 0) {
    av_frame_free(&swFrame);
    return NULL;
  }

  return swFrame;
}

static int SDLCALL Kotonoha_VideoDecodeWorker(void *opaque) {
  struct Kotonoha_VideoData *instance = (struct Kotonoha_VideoData *)opaque;
  if (!instance || !instance->pFormatCtx || !instance->pCodecCtx) {
    return 0;
  }

  AVPacket *packet = av_packet_alloc();
  AVFrame *recvFrame = av_frame_alloc();
  if (!packet || !recvFrame) {
    if (packet)
      av_packet_free(&packet);
    if (recvFrame)
      av_frame_free(&recvFrame);
    return 0;
  }

  AVStream *stream = instance->pFormatCtx->streams[instance->videoStreamIndex];
  const Uint64 targetCacheMs =
      (Uint64)(instance->cacheDurationSeconds * 1000.0f);
  const Uint64 minResumeCacheMs =
      (targetCacheMs / 2 > 300) ? (targetCacheMs / 2) : 300;

  while (SDL_GetAtomicInt(&instance->threadRunning) != 0) {
    // 1. Processar Seek se solicitado
    if (SDL_GetAtomicInt(&instance->seekRequested) != 0) {
      SDL_LockMutex(instance->queueMutex);
      const Uint64 targetTime = instance->seekTargetMs;

      while (instance->queueCount > 0) {
        av_frame_free(&instance->frameQueue[instance->queueHead].frame);
        instance->queueHead =
            (instance->queueHead + 1) % instance->queueCapacity;
        instance->queueCount--;
      }
      instance->queueHead = 0;
      instance->queueTail = 0;

      instance->demuxEof = false;
      instance->decoderEof = false;

      Kotonoha_VideoSeekInternal(instance, targetTime);

      SDL_SetAtomicInt(&instance->decodeThreadEof, 0);
      SDL_SetAtomicInt(&instance->seekRequested, 0);
      SDL_UnlockMutex(instance->queueMutex);
      continue;
    }

    // 2. Verificar se o buffer esta cheio
    SDL_LockMutex(instance->queueMutex);
    int count = instance->queueCount;
    Uint64 bufferedSpanMs = 0;
    if (count > 0) {
      int lastIdx = (instance->queueTail - 1 + instance->queueCapacity) %
                    instance->queueCapacity;
      if (instance->frameQueue[lastIdx].ptsMs >=
          instance->frameQueue[instance->queueHead].ptsMs) {
        bufferedSpanMs = instance->frameQueue[lastIdx].ptsMs -
                         instance->frameQueue[instance->queueHead].ptsMs;
      }
    }

    bool isFull = (count >= instance->queueCapacity - 2) ||
                  (bufferedSpanMs >= targetCacheMs);
    bool isEof = (SDL_GetAtomicInt(&instance->decodeThreadEof) != 0);

    if ((isFull || isEof) && SDL_GetAtomicInt(&instance->seekRequested) == 0 &&
        SDL_GetAtomicInt(&instance->threadRunning) != 0) {
      SDL_WaitConditionTimeout(instance->queueCond, instance->queueMutex, 20);
      SDL_UnlockMutex(instance->queueMutex);
      continue;
    }
    SDL_UnlockMutex(instance->queueMutex);

    // 3. Tentar receber frame decodificado
    int ret = avcodec_receive_frame(instance->pCodecCtx, recvFrame);
    if (ret == 0) {
      Uint64 framePtsMs =
          KotonohaFFmpegGetTime(stream, recvFrame->pts, recvFrame->pkt_dts);

      AVFrame *cpuFrame = getCpuReadableFrame(recvFrame);
      if (cpuFrame != NULL) {
        AVFrame *cachedFrame = av_frame_clone(cpuFrame);
        if (cpuFrame != recvFrame) {
          av_frame_free(&cpuFrame);
        }
        av_frame_unref(recvFrame);

        if (cachedFrame != NULL) {
          SDL_LockMutex(instance->queueMutex);
          if (instance->queueCount < instance->queueCapacity) {
            instance->frameQueue[instance->queueTail].frame = cachedFrame;
            instance->frameQueue[instance->queueTail].ptsMs = framePtsMs;
            instance->queueTail =
                (instance->queueTail + 1) % instance->queueCapacity;
            instance->queueCount++;

            if (SDL_GetAtomicInt(&instance->isBuffering) != 0) {
              int c = instance->queueCount;
              int lastIdx =
                  (instance->queueTail - 1 + instance->queueCapacity) %
                  instance->queueCapacity;
              Uint64 span = instance->frameQueue[lastIdx].ptsMs -
                            instance->frameQueue[instance->queueHead].ptsMs;
              if (span >= minResumeCacheMs || c >= 8 || isEof) {
                SDL_SetAtomicInt(&instance->isBuffering, 0);
                KotonohaIOMonitorOperationSetStalled(&instance->ioOperation,
                                                     false);
              }
            }

            SDL_SignalCondition(instance->queueCond);
          } else {
            av_frame_free(&cachedFrame);
          }
          SDL_UnlockMutex(instance->queueMutex);
        }
      }
      continue;
    } else if (ret == AVERROR(EAGAIN)) {
      bool fedPacket = false;
      while (!fedPacket && !instance->demuxEof &&
             SDL_GetAtomicInt(&instance->threadRunning) != 0) {

        if (KotonohaIOMonitorOperationIsCancelled(&instance->ioOperation)) {
          break;
        }

        KotonohaIOMonitorOperationBegin(&instance->ioOperation);
        ret = KotonohaFFmpegReadFrame(instance->pFormatCtx, packet);
        KotonohaIOMonitorOperationEnd(&instance->ioOperation);

        if (ret < 0) {
          if (ret == AVERROR_EOF) {
            instance->demuxEof = true;
            avcodec_send_packet(instance->pCodecCtx, NULL);
          } else {
            SDL_Delay(2);
          }
          break;
        }

        if (packet->stream_index == instance->videoStreamIndex) {
          avcodec_send_packet(instance->pCodecCtx, packet);
          fedPacket = true;
        }
        av_packet_unref(packet);
      }
    } else if (ret == AVERROR_EOF) {
      instance->decoderEof = true;
      SDL_SetAtomicInt(&instance->decodeThreadEof, 1);
      SDL_LockMutex(instance->queueMutex);
      if (SDL_GetAtomicInt(&instance->isBuffering) != 0) {
        SDL_SetAtomicInt(&instance->isBuffering, 0);
        KotonohaIOMonitorOperationSetStalled(&instance->ioOperation, false);
      }
      SDL_UnlockMutex(instance->queueMutex);
      SDL_Delay(15);
    } else {
      SDL_Delay(10);
    }
  }

  av_frame_free(&recvFrame);
  av_packet_free(&packet);
  return 0;
}

static bool initializeVideoContext(struct Kotonoha_VideoData *instance,
                                   const char *filename) {
  if (!KotonohaFFmpegLoadMonitored(filename, &instance->pFormatCtx,
                                   &instance->pCodecCtx, AVMEDIA_TYPE_VIDEO,
                                   &instance->videoStreamIndex, 0,
                                   instance->hwCtx, &instance->ioOperation)) {
    if (!KotonohaIOMonitorIsIgnoredMissing(&instance->ioOperation)) {
      SDL_LogError(SDL_LOG_CATEGORY_ERROR, "Failed to load video file: %s\n",
                   filename);
    }
    return false;
  }
  return true;
}

Kotonoha_VideoData *KotonohaVideoRenderInit(
    const char *filename, Kotonoha_Time *time, Uint64 startTime, Uint64 endTime,
    bool useOrsFrameTimeline, Uint64 orsEndFrame, Kotonoha_IOMonitor *ioMonitor,
    float cacheDurationSeconds) {
  struct Kotonoha_VideoData *videoInstance =
      (struct Kotonoha_VideoData *)SDL_calloc(
          1, sizeof(struct Kotonoha_VideoData));
  if (!videoInstance) {
    SDL_LogError(SDL_LOG_CATEGORY_ERROR,
                 "Failed to allocate video instance: %s\n", SDL_GetError());
    return NULL;
  }

  KotonohaIOMonitorOperationInit(&videoInstance->ioOperation, ioMonitor);

  if (!initializeHwContext(videoInstance) ||
      !initializeVideoContext(videoInstance, filename)) {
    KotonohaVideoRenderShutdown(&videoInstance);
    return NULL;
  }

  videoInstance->time = time;
  videoInstance->startTime = startTime;
  videoInstance->endTime = endTime;
  videoInstance->videoTime = 0;
  videoInstance->lastTime = 0;
  videoInstance->useOrsFrameTimeline = useOrsFrameTimeline;
  videoInstance->orsEndFrame = orsEndFrame;

  videoInstance->cacheDurationSeconds =
      (cacheDurationSeconds > 0.1f) ? cacheDurationSeconds : 2.0f;

  int capacity = (int)(videoInstance->cacheDurationSeconds * 60) + 32;
  if (capacity < 64)
    capacity = 64;
  if (capacity > 300)
    capacity = 300;
  videoInstance->queueCapacity = capacity;
  videoInstance->frameQueue = (Kotonoha_CachedVideoFrame *)SDL_calloc(
      capacity, sizeof(Kotonoha_CachedVideoFrame));
  if (!videoInstance->frameQueue) {
    KotonohaVideoRenderShutdown(&videoInstance);
    return NULL;
  }

  videoInstance->queueMutex = SDL_CreateMutex();
  videoInstance->queueCond = SDL_CreateCondition();
  if (!videoInstance->queueMutex || !videoInstance->queueCond) {
    KotonohaVideoRenderShutdown(&videoInstance);
    return NULL;
  }

  bool inRange;
  Sint64 diff;
  Uint64 toSeek =
      KotonohaTimeGetFromEvent(time, startTime, endTime, &inRange, &diff);

  videoInstance->seekTargetMs = toSeek;
  videoInstance->videoTime = toSeek;
  SDL_SetAtomicInt(&videoInstance->threadRunning, 1);
  SDL_SetAtomicInt(&videoInstance->seekRequested, 1);
  SDL_SetAtomicInt(&videoInstance->isBuffering, 1);
  SDL_SetAtomicInt(&videoInstance->decodeThreadEof, 0);

  videoInstance->decodeThread = SDL_CreateThread(
      Kotonoha_VideoDecodeWorker, "KotonohaVideoDecode", videoInstance);
  if (!videoInstance->decodeThread) {
    SDL_LogError(SDL_LOG_CATEGORY_ERROR,
                 "Failed to create video decode thread: %s\n", SDL_GetError());
    KotonohaVideoRenderShutdown(&videoInstance);
    return NULL;
  }

  SDL_LockMutex(videoInstance->queueMutex);
  Uint64 startWait = SDL_GetTicks();
  while (videoInstance->queueCount == 0 &&
         SDL_GetAtomicInt(&videoInstance->decodeThreadEof) == 0 &&
         (SDL_GetTicks() - startWait < 150)) {
    SDL_WaitConditionTimeout(videoInstance->queueCond,
                             videoInstance->queueMutex, 30);
  }
  SDL_UnlockMutex(videoInstance->queueMutex);

  return videoInstance;
}

void KotonohaVideoRenderShutdown(struct Kotonoha_VideoData **instance) {
  if (!instance || !*instance) {
    return;
  }

  struct Kotonoha_VideoData *videoData = *instance;

  SDL_SetAtomicInt(&videoData->threadRunning, 0);
  KotonohaIOMonitorOperationCancel(&videoData->ioOperation);
  if (videoData->queueCond) {
    SDL_SignalCondition(videoData->queueCond);
  }
  if (videoData->decodeThread) {
    SDL_WaitThread(videoData->decodeThread, NULL);
    videoData->decodeThread = NULL;
  }

  if (videoData->frameQueue) {
    for (int i = 0; i < videoData->queueCount; ++i) {
      int idx = (videoData->queueHead + i) % videoData->queueCapacity;
      if (videoData->frameQueue[idx].frame) {
        av_frame_free(&videoData->frameQueue[idx].frame);
      }
    }
    SDL_free(videoData->frameQueue);
    videoData->frameQueue = NULL;
  }

  if (videoData->queueCond) {
    SDL_DestroyCondition(videoData->queueCond);
    videoData->queueCond = NULL;
  }
  if (videoData->queueMutex) {
    SDL_DestroyMutex(videoData->queueMutex);
    videoData->queueMutex = NULL;
  }

  if (videoData->pFrameRGB) {
    av_frame_free(&videoData->pFrameRGB);
    videoData->pFrameRGB = NULL;
  }
  if (videoData->swsBuffer) {
    av_free(videoData->swsBuffer);
    videoData->swsBuffer = NULL;
  }
  if (videoData->swsCtx) {
    sws_freeContext(videoData->swsCtx);
    videoData->swsCtx = NULL;
  }

  if (videoData->pFrame) {
    av_frame_free(&videoData->pFrame);
  }
  if (videoData->pCodecCtx) {
    avcodec_free_context(&videoData->pCodecCtx);
  }
  if (videoData->pFormatCtx) {
    avformat_close_input(&videoData->pFormatCtx);
  }
  if (videoData->hwCtx) {
    if (videoData->hwCtx->deviceRef) {
      av_buffer_unref(&videoData->hwCtx->deviceRef);
    }
    SDL_free(videoData->hwCtx);
    videoData->hwCtx = NULL;
  }

  if (videoData->texture) {
    SDL_DestroyTexture(videoData->texture);
    videoData->texture = NULL;
  }
  if (videoData->extraTexture) {
    SDL_DestroyTexture(videoData->extraTexture);
    videoData->extraTexture = NULL;
  }

  KotonohaIOMonitorOperationReset(&videoData->ioOperation);

  SDL_free(videoData);
  *instance = NULL;
}

enum Kotonoha_Scene_Status KotonohaVideoRenderProcess(void *userData,
                                                      SDL_Renderer *render) {
  struct Kotonoha_VideoData *instance = (struct Kotonoha_VideoData *)userData;
  if (instance == NULL) {
    return KOTONOHA_SCENE_NULL;
  }

  if (instance->useOrsFrameTimeline) {
    const Uint64 sceneMs = KotonohaTimeGet(instance->time);
    if (KotonohaOrsSceneMsToFrame(sceneMs) >= instance->orsEndFrame) {
      return KOTONOHA_SCENE_COMPLETE;
    }
  }

  bool inRange = false;
  Sint64 diff = 0;
  const Uint64 currentTime = KotonohaTimeGetFromEvent(
      instance->time, instance->startTime, instance->endTime, &inRange, &diff);

  if (!inRange) {
    return (diff > 0) ? KOTONOHA_SCENE_COMPLETE : KOTONOHA_SCENE_NULL;
  }

  instance->lastTime = currentTime;

  if (u64_abs_diff(currentTime, instance->videoTime) > 1500) {
    SDL_LockMutex(instance->queueMutex);
    instance->seekTargetMs = currentTime;
    SDL_SetAtomicInt(&instance->seekRequested, 1);
    SDL_SetAtomicInt(&instance->isBuffering, 1);
    instance->videoTime = currentTime;
    SDL_SignalCondition(instance->queueCond);
    SDL_UnlockMutex(instance->queueMutex);
    return KOTONOHA_SCENE_WAITING;
  }

  AVFrame *selectedFrame = NULL;
  SDL_LockMutex(instance->queueMutex);

  // Descartar quadros atrasados
  while (instance->queueCount > 1) {
    int nextIdx = (instance->queueHead + 1) % instance->queueCapacity;
    if (instance->frameQueue[nextIdx].ptsMs <= currentTime) {
      av_frame_free(&instance->frameQueue[instance->queueHead].frame);
      instance->queueHead = nextIdx;
      instance->queueCount--;
    } else {
      break;
    }
  }

  if (instance->queueCount > 0) {
    Uint64 headPts = instance->frameQueue[instance->queueHead].ptsMs;

    if (headPts <= currentTime || instance->texture == NULL) {
      selectedFrame = instance->frameQueue[instance->queueHead].frame;
      instance->frameQueue[instance->queueHead].frame = NULL;

      instance->videoTime = headPts;

      instance->queueHead = (instance->queueHead + 1) % instance->queueCapacity;
      instance->queueCount--;

      SDL_SignalCondition(instance->queueCond);
    }
  }

  const bool isEof = (SDL_GetAtomicInt(&instance->decodeThreadEof) != 0);
  if (!isEof && instance->queueCount == 0 && selectedFrame == NULL) {
    if (SDL_GetAtomicInt(&instance->isBuffering) == 0) {
      SDL_SetAtomicInt(&instance->isBuffering, 1);
      KotonohaIOMonitorOperationSetStalled(&instance->ioOperation, true);
    }
  }
  SDL_UnlockMutex(instance->queueMutex);

  if (selectedFrame != NULL) {
    if (instance->texture == NULL) {
      instance->texture = SDL_CreateTexture(
          render, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STREAMING,
          instance->pCodecCtx->width, instance->pCodecCtx->height);
      if (instance->texture == NULL) {
        SDL_LogError(SDL_LOG_CATEGORY_ERROR,
                     "Failed to create video texture: %s", SDL_GetError());
        av_frame_free(&selectedFrame);
        return KOTONOHA_SCENE_FATAL_ERROR;
      }
      SDL_SetTextureBlendMode(instance->texture, SDL_BLENDMODE_BLEND);
    }

    if (instance->swsCtx == NULL) {
      instance->swsCtx = KotonohaFFmpegCreateSwsContext(
          instance->pCodecCtx, selectedFrame->format, selectedFrame->width,
          selectedFrame->height);
      if (instance->swsCtx == NULL) {
        SDL_LogError(SDL_LOG_CATEGORY_ERROR, "Cannot create sws context");
        av_frame_free(&selectedFrame);
        return KOTONOHA_SCENE_FATAL_ERROR;
      }
    }

    if (instance->pFrameRGB == NULL) {
      instance->pFrameRGB = av_frame_alloc();
    }

    const int bufSize = av_image_get_buffer_size(
        AV_PIX_FMT_RGBA, selectedFrame->width, selectedFrame->height, 1);
    if (instance->swsBuffer == NULL ||
        instance->swsBufferWidth != selectedFrame->width ||
        instance->swsBufferHeight != selectedFrame->height) {
      if (instance->swsBuffer) {
        av_free(instance->swsBuffer);
      }
      instance->swsBuffer = (Uint8 *)av_malloc(bufSize);
      instance->swsBufferWidth = selectedFrame->width;
      instance->swsBufferHeight = selectedFrame->height;
      if (instance->pFrameRGB && instance->swsBuffer) {
        av_image_fill_arrays(instance->pFrameRGB->data,
                             instance->pFrameRGB->linesize, instance->swsBuffer,
                             AV_PIX_FMT_RGBA, selectedFrame->width,
                             selectedFrame->height, 1);
      }
    }

    if (instance->pFrameRGB && instance->swsBuffer) {
      sws_scale(instance->swsCtx, (const uint8_t *const *)selectedFrame->data,
                selectedFrame->linesize, 0, selectedFrame->height,
                instance->pFrameRGB->data, instance->pFrameRGB->linesize);

      if (!SDL_UpdateTexture(instance->texture, NULL,
                             instance->pFrameRGB->data[0],
                             instance->pFrameRGB->linesize[0])) {
        SDL_LogError(SDL_LOG_CATEGORY_RENDER,
                     "Failed to update video texture: %s", SDL_GetError());
      }
    }

    av_frame_free(&selectedFrame);
    return KOTONOHA_SCENE_DRAW;
  }

  if (instance->texture != NULL) {
    return KOTONOHA_SCENE_DRAW;
  }

  return KOTONOHA_SCENE_WAITING;
}