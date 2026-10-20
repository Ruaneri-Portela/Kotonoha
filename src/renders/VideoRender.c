#include <Kotonoha/renders/VideoRender.h>
#include <Kotonoha/speciais/Upscaling.h>
#include <Kotonoha/utils/FFmpegSDL.h>
#include <limits.h>

static inline Uint64 u64_abs_diff(Uint64 a, Uint64 b) {
  return (a > b) ? (a - b) : (b - a);
}

static bool initializeHwContext(struct Kotonoha_VideoData *instance) {
  instance->hwCtx = (struct Kotonoha_FFmpegHwContext *)SDL_calloc(
      1, sizeof(struct Kotonoha_FFmpegHwContext));
  return instance->hwCtx != NULL;
}

static AVFrame *getCpuReadableFrame(AVFrame *src) {
  if (!src)
    return NULL;
  if (!src->hw_frames_ctx)
    return src;

  AVFrame *swFrame = av_frame_alloc();
  if (!swFrame)
    return NULL;

  if (av_hwframe_transfer_data(swFrame, src, 0) < 0 ||
      av_frame_copy_props(swFrame, src) < 0) {
    av_frame_free(&swFrame);
    return NULL;
  }
  return swFrame;
}

static bool KotonohaVideoCreateTexture(Kotonoha_VideoData *instance,
                                       SDL_Renderer *render, AVFrame *frame) {
  bool supportsDirectUpload = false;
  Uint32 format =
      KotonohaSDLFormatForAVFrame(frame, &supportsDirectUpload);
  instance->texture =
      SDL_CreateTexture(render, format, SDL_TEXTUREACCESS_STREAMING,
                        frame->width, frame->height);
  if (instance->texture == NULL && format != SDL_PIXELFORMAT_RGBA32) {
    SDL_LogWarn(SDL_LOG_CATEGORY_RENDER,
                "Renderer does not support native video pixel format %u; "
                "using SWS conversion",
                format);
    format = SDL_PIXELFORMAT_RGBA32;
    instance->texture =
        SDL_CreateTexture(render, format, SDL_TEXTUREACCESS_STREAMING,
                          frame->width, frame->height);
    instance->directTextureUploadFailed = supportsDirectUpload;
  }
  if (instance->texture == NULL) {
    SDL_LogError(SDL_LOG_CATEGORY_RENDER,
                 "Failed to create video texture: %s", SDL_GetError());
    return false;
  }

  if (!SDL_SetTextureBlendMode(
          instance->texture,
          KotonohaAVPixelFormatHasAlpha(
              (enum AVPixelFormat)frame->format)
              ? SDL_BLENDMODE_BLEND
              : SDL_BLENDMODE_NONE)) {
    SDL_LogError(SDL_LOG_CATEGORY_RENDER,
                 "Failed to set video texture blending: %s", SDL_GetError());
    SDL_DestroyTexture(instance->texture);
    instance->texture = NULL;
    return false;
  }

  instance->sourcePixelFormat = (enum AVPixelFormat)frame->format;
  instance->textureFormatInitialized = true;
  return true;
}

static bool KotonohaVideoEnsureSws(Kotonoha_VideoData *instance,
                                   AVFrame *frame) {
  instance->swsCtx = sws_getCachedContext(
      instance->swsCtx, frame->width, frame->height,
      (enum AVPixelFormat)frame->format, frame->width, frame->height,
      AV_PIX_FMT_RGBA, SWS_LANCZOS, NULL, NULL, NULL);
  if (instance->swsCtx == NULL) {
    SDL_LogError(SDL_LOG_CATEGORY_RENDER,
                 "Failed to create video frame converter");
    return false;
  }
  return true;
}

static bool KotonohaVideoScaleFrameToRgba(Kotonoha_VideoData *instance,
                                          AVFrame *frame, uint8_t *pixels,
                                          int pitch) {
  if (pixels == NULL || frame->width <= 0 || frame->width > INT_MAX / 4 ||
      pitch < frame->width * 4 ||
      !KotonohaVideoEnsureSws(instance, frame)) {
    return false;
  }

  uint8_t *destinationData[4] = {pixels, NULL, NULL, NULL};
  const int destinationLines[4] = {pitch, 0, 0, 0};
  return sws_scale(instance->swsCtx,
                   (const uint8_t *const *)frame->data, frame->linesize, 0,
                   frame->height, destinationData, destinationLines) > 0;
}

static bool updateTextureFromFrame(struct Kotonoha_VideoData *instance,
                                   SDL_Renderer *render, AVFrame *frame) {
  if (!instance || !render || !frame)
    return false;

  AVFrame *cpuFrame = getCpuReadableFrame(frame);
  if (!cpuFrame)
    return false;

  if (instance->texture != NULL) {
    float textureWidth = 0.0f;
    float textureHeight = 0.0f;
    if (!SDL_GetTextureSize(instance->texture, &textureWidth, &textureHeight)) {
      SDL_LogError(SDL_LOG_CATEGORY_RENDER,
                   "Failed to get video texture dimensions: %s",
                   SDL_GetError());
      goto cleanup;
    }
    if ((int)textureWidth != cpuFrame->width ||
        (int)textureHeight != cpuFrame->height ||
        !instance->textureFormatInitialized ||
        instance->sourcePixelFormat != (enum AVPixelFormat)cpuFrame->format) {
      SDL_DestroyTexture(instance->texture);
      instance->texture = NULL;
      instance->textureFormatInitialized = false;
      instance->directTextureUploadFailed = false;
    }
  }

  if (instance->texture == NULL) {
    if (!KotonohaVideoCreateTexture(instance, render, cpuFrame)) {
      goto cleanup;
    }
  }

  bool supportsDirectUpload = false;
  KotonohaSDLFormatForAVFrame(cpuFrame, &supportsDirectUpload);
  if (supportsDirectUpload && !instance->directTextureUploadFailed) {
    if (KotonohaSDLUpdateTextureFromAVFrame(instance->texture, cpuFrame)) {
      if (cpuFrame != frame) {
        av_frame_free(&cpuFrame);
      }
      return true;
    }
    SDL_LogWarn(SDL_LOG_CATEGORY_RENDER,
                "SDL native YUV/RGB upload failed; switching to SWS: %s",
                SDL_GetError());
    SDL_DestroyTexture(instance->texture);
    instance->texture = SDL_CreateTexture(
        render, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STREAMING,
        cpuFrame->width, cpuFrame->height);
    if (instance->texture == NULL ||
        !SDL_SetTextureBlendMode(instance->texture,
                                 KotonohaAVPixelFormatHasAlpha(
                                     (enum AVPixelFormat)cpuFrame->format)
                                     ? SDL_BLENDMODE_BLEND
                                     : SDL_BLENDMODE_NONE)) {
      SDL_LogError(SDL_LOG_CATEGORY_RENDER,
                   "Failed to create SWS fallback video texture: %s",
                   SDL_GetError());
      SDL_DestroyTexture(instance->texture);
      instance->texture = NULL;
      goto cleanup;
    }
    instance->directTextureUploadFailed = true;
  }

  if (!KotonohaVideoEnsureSws(instance, cpuFrame)) {
    goto cleanup;
  }

  if (instance->pFrameRGB == NULL) {
    instance->pFrameRGB = av_frame_alloc();
    if (instance->pFrameRGB == NULL) {
      SDL_LogError(SDL_LOG_CATEGORY_RENDER,
                   "Failed to allocate converted video frame");
      goto cleanup;
    }
  }

  const int bufSize = av_image_get_buffer_size(AV_PIX_FMT_RGBA, cpuFrame->width,
                                               cpuFrame->height, 1);
  if (bufSize <= 0) {
    SDL_LogError(SDL_LOG_CATEGORY_RENDER,
                 "Invalid video frame dimensions: %dx%d", cpuFrame->width,
                 cpuFrame->height);
    goto cleanup;
  }
  if (instance->swsBuffer == NULL ||
      instance->swsBufferWidth != cpuFrame->width ||
      instance->swsBufferHeight != cpuFrame->height) {
    if (instance->swsBuffer)
      av_free(instance->swsBuffer);
    instance->swsBuffer = (Uint8 *)av_malloc(bufSize);
    if (instance->swsBuffer == NULL) {
      instance->swsBufferWidth = 0;
      instance->swsBufferHeight = 0;
      SDL_LogError(SDL_LOG_CATEGORY_RENDER,
                   "Failed to allocate video conversion buffer");
      goto cleanup;
    }
    instance->swsBufferWidth = cpuFrame->width;
    instance->swsBufferHeight = cpuFrame->height;
    if (av_image_fill_arrays(instance->pFrameRGB->data,
                             instance->pFrameRGB->linesize,
                             instance->swsBuffer, AV_PIX_FMT_RGBA,
                             cpuFrame->width, cpuFrame->height, 1) < 0) {
      instance->swsBufferWidth = 0;
      instance->swsBufferHeight = 0;
      SDL_LogError(SDL_LOG_CATEGORY_RENDER,
                   "Failed to initialize converted video frame");
      goto cleanup;
    }
  }

  if (!KotonohaVideoScaleFrameToRgba(
          instance, cpuFrame, instance->pFrameRGB->data[0],
          instance->pFrameRGB->linesize[0])) {
    SDL_LogError(SDL_LOG_CATEGORY_RENDER, "Failed to convert video frame");
    goto cleanup;
  }

  if (!SDL_UpdateTexture(instance->texture, NULL, instance->pFrameRGB->data[0],
                         instance->pFrameRGB->linesize[0])) {
    SDL_LogError(SDL_LOG_CATEGORY_RENDER, "Failed to update video texture: %s",
                 SDL_GetError());
    goto cleanup;
  }

  if (cpuFrame != frame) {
    av_frame_free(&cpuFrame);
  }
  return true;

cleanup:
  if (cpuFrame != frame) {
    av_frame_free(&cpuFrame);
  }
  return false;
}

Kotonoha_VideoData *KotonohaVideoRenderInit(
    const char *filename, Kotonoha_Time *time, Uint64 startTime, Uint64 endTime,
    bool useOrsFrameTimeline, Uint64 orsEndFrame, float cacheDurationSeconds,
    Kotonoha_Demuxer *demuxer) {
  struct Kotonoha_VideoData *videoInstance =
      (struct Kotonoha_VideoData *)SDL_calloc(
          1, sizeof(struct Kotonoha_VideoData));
  if (!videoInstance)
    return NULL;

  videoInstance->demuxer = demuxer;
  if (demuxer != NULL) {
    videoInstance->stream =
        KotonohaDemuxerAddStream(demuxer, filename, cacheDurationSeconds,
                                 videoInstance, AVMEDIA_TYPE_VIDEO);
  }

  if (!videoInstance->stream || videoInstance->stream->videoStreamIdx < 0) {
    KotonohaVideoRenderShutdown(&videoInstance);
    return NULL;
  }

  videoInstance->videoStreamIndex = videoInstance->stream->videoStreamIdx;
  videoInstance->videoTimeBase = videoInstance->stream->videoTimeBase;

  initializeHwContext(videoInstance);

  if (!KotonohaFFmpegCreateDecoderCtx(
          videoInstance->stream->formatCtx, videoInstance->videoStreamIndex,
          &videoInstance->pCodecCtx, videoInstance->hwCtx)) {
    KotonohaVideoRenderShutdown(&videoInstance);
    return NULL;
  }

  videoInstance->time = time;
  videoInstance->startTime = startTime;
  videoInstance->endTime = endTime;
  videoInstance->useOrsFrameTimeline = useOrsFrameTimeline;
  videoInstance->orsEndFrame = orsEndFrame;

  bool inRange;
  Sint64 diff;
  Uint64 toSeek =
      KotonohaTimeGetFromEvent(time, startTime, endTime, &inRange, &diff);

  videoInstance->videoTime = toSeek;
  KotonohaDemuxStreamSeek(videoInstance->stream, toSeek);
  if (!KotonohaDemuxerActivateStream(demuxer, videoInstance->stream)) {
    KotonohaVideoRenderShutdown(&videoInstance);
    return NULL;
  }

  return videoInstance;
}

void KotonohaVideoRenderShutdown(struct Kotonoha_VideoData **instance) {
  if (!instance || !*instance)
    return;
  struct Kotonoha_VideoData *videoData = *instance;

  if (videoData->demuxer && videoData->stream)
    KotonohaDemuxerRemoveStream(videoData->demuxer, &videoData->stream);

  if (videoData->currentFrame)
    av_frame_free(&videoData->currentFrame);
  if (videoData->pFrameRGB)
    av_frame_free(&videoData->pFrameRGB);
  if (videoData->swsBuffer)
    av_free(videoData->swsBuffer);
  if (videoData->swsCtx)
    sws_freeContext(videoData->swsCtx);
  if (videoData->pCodecCtx)
    avcodec_free_context(&videoData->pCodecCtx);

  if (videoData->hwCtx) {
    if (videoData->hwCtx->deviceRef)
      av_buffer_unref(&videoData->hwCtx->deviceRef);
    SDL_free(videoData->hwCtx);
  }

  if (videoData->texture)
    SDL_DestroyTexture(videoData->texture);
  if (videoData->extraTexture)
    SDL_DestroyTexture(videoData->extraTexture);

  SDL_free(videoData);
  *instance = NULL;
}

enum Kotonoha_Scene_Status KotonohaVideoRenderProcess(void *userData,
                                                      SDL_Renderer *render) {
  struct Kotonoha_VideoData *instance = (struct Kotonoha_VideoData *)userData;
  if (instance == NULL || instance->stream == NULL)
    return KOTONOHA_SCENE_NULL;

  bool inRange = false;
  Sint64 diff = 0;
  Uint64 currentTime = KotonohaTimeGetFromEvent(
      instance->time, instance->startTime, instance->endTime, &inRange, &diff);

  if (!inRange)
    return (diff > 0) ? KOTONOHA_SCENE_COMPLETE : KOTONOHA_SCENE_NULL;

  if (!instance->firstSeek) {
    if (currentTime != instance->videoTime) {
      instance->videoTime = currentTime;
      KotonohaDemuxStreamSeek(instance->stream, currentTime);
      avcodec_flush_buffers(instance->pCodecCtx);
      instance->decoderFlushSent = false;
      if (instance->currentFrame) {
        av_frame_free(&instance->currentFrame);
      }
    }
    instance->firstSeek = true; // Marca como inicializado para não repetir
  }

  if (u64_abs_diff(currentTime, instance->videoTime) > 1500) {
    KotonohaDemuxStreamSeek(instance->stream, currentTime);
    avcodec_flush_buffers(instance->pCodecCtx);
    instance->decoderFlushSent = false;
    if (instance->currentFrame) {
      av_frame_free(&instance->currentFrame);
    }
    instance->videoTime = currentTime;
    return KOTONOHA_SCENE_WAITING;
  }

  if (instance->currentFrame != NULL) {
    if (instance->currentPtsMs <= currentTime) {
      const bool updated =
          updateTextureFromFrame(instance, render, instance->currentFrame);
      av_frame_free(&instance->currentFrame);
      instance->videoTime = instance->currentPtsMs;
      return updated ? KOTONOHA_SCENE_DRAW : KOTONOHA_SCENE_WAITING;
    } else {
      return (instance->texture != NULL) ? KOTONOHA_SCENE_DRAW
                                         : KOTONOHA_SCENE_WAITING;
    }
  }

  AVFrame *recvFrame = av_frame_alloc();
  while (recvFrame != NULL) {
    int ret = avcodec_receive_frame(instance->pCodecCtx, recvFrame);
    if (ret == 0) {
      Uint64 ptsMs = 0;
      if (recvFrame->pts != AV_NOPTS_VALUE) {
        ptsMs = (Uint64)(recvFrame->pts *
                         av_q2d(instance->videoTimeBase) * 1000.0);
      } else if (recvFrame->pkt_dts != AV_NOPTS_VALUE) {
        ptsMs = (Uint64)(recvFrame->pkt_dts *
                         av_q2d(instance->videoTimeBase) * 1000.0);
      }

      if (ptsMs < currentTime &&
          KotonohaPacketQueueHasPackets(&instance->stream->videoQueue)) {
        av_frame_unref(recvFrame);
        continue;
      }

      instance->currentFrame = recvFrame;
      instance->currentPtsMs = ptsMs;

      if (ptsMs <= currentTime) {
        const bool updated =
            updateTextureFromFrame(instance, render, instance->currentFrame);
        av_frame_free(&instance->currentFrame);
        instance->videoTime = ptsMs;
        return updated ? KOTONOHA_SCENE_DRAW : KOTONOHA_SCENE_WAITING;
      }
      break;
    }

    if (ret == AVERROR(EAGAIN)) {
      Uint64 pktPts = 0;
      AVPacket *pkt =
          KotonohaPacketQueuePop(&instance->stream->videoQueue, &pktPts);
      if (pkt != NULL) {
        KotonohaDemuxStreamSetWaiting(instance->stream, false);
        avcodec_send_packet(instance->pCodecCtx, pkt);
        av_packet_free(&pkt);
      } else {
        if (KotonohaPacketQueueIsEof(&instance->stream->videoQueue)) {
          if (!instance->decoderFlushSent) {
            avcodec_send_packet(instance->pCodecCtx, NULL);
            instance->decoderFlushSent = true;
            continue;
          }
        } else {
          KotonohaDemuxStreamSetWaiting(instance->stream, true);
        }
        break;
      }
    } else {
      break;
    }
  }

  if (instance->currentFrame == NULL) {
    av_frame_free(&recvFrame);
  }

  return (instance->texture != NULL) ? KOTONOHA_SCENE_DRAW
                                     : KOTONOHA_SCENE_WAITING;
}