#include <Kotonoha/renders/VideoRender.h>
#include <Kotonoha/speciais/Upscaling.h>

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

static void updateTextureFromFrame(struct Kotonoha_VideoData *instance,
                                   SDL_Renderer *render, AVFrame *frame) {
  if (!instance || !render || !frame)
    return;

  AVFrame *cpuFrame = getCpuReadableFrame(frame);
  if (!cpuFrame)
    return;

  if (instance->texture == NULL) {
    instance->texture = SDL_CreateTexture(
        render, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STREAMING,
        instance->pCodecCtx->width, instance->pCodecCtx->height);
    SDL_SetTextureBlendMode(instance->texture, SDL_BLENDMODE_BLEND);
  }

  if (instance->swsCtx == NULL) {
    instance->swsCtx =
        KotonohaFFmpegCreateSwsContext(instance->pCodecCtx, cpuFrame->format,
                                       cpuFrame->width, cpuFrame->height);
  }

  if (instance->pFrameRGB == NULL)
    instance->pFrameRGB = av_frame_alloc();

  const int bufSize = av_image_get_buffer_size(AV_PIX_FMT_RGBA, cpuFrame->width,
                                               cpuFrame->height, 1);
  if (instance->swsBuffer == NULL ||
      instance->swsBufferWidth != cpuFrame->width) {
    if (instance->swsBuffer)
      av_free(instance->swsBuffer);
    instance->swsBuffer = (Uint8 *)av_malloc(bufSize);
    instance->swsBufferWidth = cpuFrame->width;
    instance->swsBufferHeight = cpuFrame->height;
    av_image_fill_arrays(instance->pFrameRGB->data,
                         instance->pFrameRGB->linesize, instance->swsBuffer,
                         AV_PIX_FMT_RGBA, cpuFrame->width, cpuFrame->height, 1);
  }

  sws_scale(instance->swsCtx, (const uint8_t *const *)cpuFrame->data,
            cpuFrame->linesize, 0, cpuFrame->height, instance->pFrameRGB->data,
            instance->pFrameRGB->linesize);

  SDL_UpdateTexture(instance->texture, NULL, instance->pFrameRGB->data[0],
                    instance->pFrameRGB->linesize[0]);

  if (cpuFrame != frame) {
    av_frame_free(&cpuFrame);
  }
}

Kotonoha_VideoData *KotonohaVideoRenderInit(
    const char *filename, Kotonoha_Time *time, Uint64 startTime, Uint64 endTime,
    bool useOrsFrameTimeline, Uint64 orsEndFrame, Kotonoha_IOMonitor *ioMonitor,
    float cacheDurationSeconds, Kotonoha_Demuxer *demuxer) {
  struct Kotonoha_VideoData *videoInstance =
      (struct Kotonoha_VideoData *)SDL_calloc(
          1, sizeof(struct Kotonoha_VideoData));
  if (!videoInstance)
    return NULL;

  KotonohaIOMonitorOperationInit(&videoInstance->ioOperation, ioMonitor);

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

  videoInstance->pFormatCtx = videoInstance->stream->formatCtx;
  videoInstance->videoStreamIndex = videoInstance->stream->videoStreamIdx;

  initializeHwContext(videoInstance);

  if (!KotonohaFFmpegCreateDecoderCtx(
          videoInstance->pFormatCtx, videoInstance->videoStreamIndex,
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
    if (currentTime >= instance->startTime) {
      Uint64 initialSeekMs = currentTime - instance->startTime;
      instance->videoTime = initialSeekMs;
      KotonohaDemuxStreamSeek(instance->stream, initialSeekMs);
      avcodec_flush_buffers(instance->pCodecCtx);
      if (instance->currentFrame) {
        av_frame_free(&instance->currentFrame);
      }
    }
    instance->firstSeek = true; // Marca como inicializado para não repetir
  }

  if (u64_abs_diff(currentTime, instance->videoTime) > 1500) {
    KotonohaDemuxStreamSeek(instance->stream, currentTime);
    avcodec_flush_buffers(instance->pCodecCtx);
    if (instance->currentFrame) {
      av_frame_free(&instance->currentFrame);
    }
    instance->videoTime = currentTime;
    return KOTONOHA_SCENE_WAITING;
  }

  AVStream *stream = instance->pFormatCtx->streams[instance->videoStreamIndex];

  if (instance->currentFrame != NULL) {
    if (instance->currentPtsMs <= currentTime) {
      updateTextureFromFrame(instance, render, instance->currentFrame);
      av_frame_free(&instance->currentFrame);
      instance->videoTime = instance->currentPtsMs;
      return KOTONOHA_SCENE_DRAW;
    } else {
      return (instance->texture != NULL) ? KOTONOHA_SCENE_DRAW
                                         : KOTONOHA_SCENE_WAITING;
    }
  }

  AVFrame *recvFrame = av_frame_alloc();
  while (recvFrame != NULL) {
    int ret = avcodec_receive_frame(instance->pCodecCtx, recvFrame);
    if (ret == 0) {
      Uint64 ptsMs =
          KotonohaFFmpegGetTime(stream, recvFrame->pts, recvFrame->pkt_dts);

      if (ptsMs < currentTime && instance->stream->videoQueue.count > 0) {
        av_frame_unref(recvFrame);
        continue;
      }

      instance->currentFrame = recvFrame;
      instance->currentPtsMs = ptsMs;

      if (ptsMs <= currentTime) {
        updateTextureFromFrame(instance, render, instance->currentFrame);
        av_frame_free(&instance->currentFrame);
        instance->videoTime = ptsMs;
        return KOTONOHA_SCENE_DRAW;
      }
      break;
    }

    if (ret == AVERROR(EAGAIN)) {
      Uint64 pktPts = 0;
      AVPacket *pkt =
          KotonohaPacketQueuePop(&instance->stream->videoQueue, &pktPts);
      if (pkt != NULL) {
        avcodec_send_packet(instance->pCodecCtx, pkt);
        av_packet_free(&pkt);
      } else {
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