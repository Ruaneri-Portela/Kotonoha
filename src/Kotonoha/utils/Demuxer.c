#include <Kotonoha/utils/Demuxer.h>
#include <Kotonoha/utils/Log.h>

#include <errno.h>
#include <libavutil/error.h>

#define KOTONOHA_DEMUX_IO_TIMEOUT_MS 750
#define KOTONOHA_DEMUX_OPEN_TIMEOUT_MS 5000
#define KOTONOHA_DEMUX_REOPEN_DELAY_MS 300
#define KOTONOHA_DEMUX_TRIM_TOLERANCE_MS 500

static Uint64 KotonohaPacketDurationMs(const AVPacket *packet,
                                       const AVStream *stream) {
  if (packet->duration <= 0) {
    return 1;
  }
  const double duration =
      (double)packet->duration * av_q2d(stream->time_base) * 1000.0;
  return duration > 0.0 ? (Uint64)duration : 1;
}

static bool KotonohaDemuxIsNetworkPath(const char *filename) {
  static const char *protocols[] = {"http://", "https://", "rtsp://", "rtmp://",
                                    "tcp://",  "udp://",   "srt://"};
  for (size_t i = 0; i < SDL_arraysize(protocols); ++i) {
    if (SDL_strncasecmp(filename, protocols[i], SDL_strlen(protocols[i])) ==
        0) {
      return true;
    }
  }
  return false;
}

void KotonohaPacketQueueInit(Kotonoha_PacketQueue *q) {
  if (!q) {
    return;
  }
  SDL_memset(q, 0, sizeof(*q));
  q->mutex = SDL_CreateMutex();
  q->cond = SDL_CreateCondition();
}

void KotonohaPacketQueueFlush(Kotonoha_PacketQueue *q, bool preserveKeyFrame) {
  if (!q || !q->mutex) {
    return;
  }

  SDL_LockMutex(q->mutex);
  Kotonoha_PacketNode *curr = q->head;
  Kotonoha_PacketNode *savedKeyFrameNode = NULL;
  while (curr) {
    Kotonoha_PacketNode *next = curr->next;
    if (preserveKeyFrame && !savedKeyFrameNode && curr->packet &&
        (curr->packet->flags & AV_PKT_FLAG_KEY) != 0) {
      savedKeyFrameNode = curr;
      savedKeyFrameNode->next = NULL;
    } else {
      if (curr->packet) {
        av_packet_free(&curr->packet);
      }
      SDL_free(curr);
    }
    curr = next;
  }

  q->head = savedKeyFrameNode;
  q->tail = savedKeyFrameNode;
  q->count = savedKeyFrameNode ? 1 : 0;
  q->durationMs = savedKeyFrameNode ? savedKeyFrameNode->durationMs : 0;
  q->eof = false;
  SDL_UnlockMutex(q->mutex);
}

void KotonohaPacketQueueDestroy(Kotonoha_PacketQueue *q) {
  if (!q) {
    return;
  }
  KotonohaPacketQueueFlush(q, false);
  if (q->mutex) {
    SDL_DestroyMutex(q->mutex);
  }
  if (q->cond) {
    SDL_DestroyCondition(q->cond);
  }
  q->mutex = NULL;
  q->cond = NULL;
}

bool KotonohaPacketQueueHasPackets(Kotonoha_PacketQueue *q) {
  if (!q || !q->mutex) {
    return false;
  }
  SDL_LockMutex(q->mutex);
  const bool hasPackets = q->head != NULL;
  SDL_UnlockMutex(q->mutex);
  return hasPackets;
}

bool KotonohaPacketQueueIsEof(Kotonoha_PacketQueue *q) {
  if (!q || !q->mutex) {
    return true;
  }
  SDL_LockMutex(q->mutex);
  const bool eof = q->eof;
  SDL_UnlockMutex(q->mutex);
  return eof;
}

void KotonohaPacketQueueGetBufferStats(Kotonoha_PacketQueue *q,
                                       Uint64 *cachedMs, bool *eof) {
  if (cachedMs != NULL) {
    *cachedMs = 0;
  }
  if (eof != NULL) {
    *eof = false;
  }
  if (!q || !q->mutex) {
    return;
  }

  SDL_LockMutex(q->mutex);
  if (cachedMs != NULL) {
    *cachedMs = q->durationMs;
  }
  if (eof != NULL) {
    *eof = q->eof;
  }
  SDL_UnlockMutex(q->mutex);
}

bool KotonohaPacketQueueTrim(Kotonoha_PacketQueue *q, Uint64 maxSpanMs,
                             Uint64 *lastKeptPtsMs) {
  if (!q || !q->mutex) {
    return false;
  }

  SDL_LockMutex(q->mutex);
  // Small tolerance: a queue normally overshoots the limit by one packet.
  if (q->durationMs <= maxSpanMs + KOTONOHA_DEMUX_TRIM_TOLERANCE_MS ||
      !q->head) {
    SDL_UnlockMutex(q->mutex);
    return false;
  }

  Kotonoha_PacketNode *keep = q->head;
  Uint64 kept = keep->durationMs;
  while (keep->next && kept < maxSpanMs) {
    keep = keep->next;
    kept += keep->durationMs;
  }

  Kotonoha_PacketNode *curr = keep->next;
  keep->next = NULL;
  q->tail = keep;
  q->durationMs = kept;
  q->eof = false;
  q->count = 0;
  for (Kotonoha_PacketNode *node = q->head; node; node = node->next) {
    q->count++;
  }
  if (lastKeptPtsMs) {
    *lastKeptPtsMs = keep->ptsMs;
  }

  while (curr) {
    Kotonoha_PacketNode *next = curr->next;
    if (curr->packet) {
      av_packet_free(&curr->packet);
    }
    SDL_free(curr);
    curr = next;
  }
  SDL_UnlockMutex(q->mutex);
  return true;
}

bool KotonohaPacketQueueAtCacheLimit(Kotonoha_PacketQueue *q,
                                     Uint64 maxSpanMs) {
  if (!q || !q->mutex) {
    return true;
  }
  SDL_LockMutex(q->mutex);
  const bool atLimit = q->durationMs >= maxSpanMs || q->count >= 1024;
  SDL_UnlockMutex(q->mutex);
  return atLimit;
}

bool KotonohaPacketQueuePush(Kotonoha_PacketQueue *q, AVPacket *pkt,
                             AVStream *stream, Uint64 maxSpanMs) {
  if (!q || !q->mutex || !pkt || !stream) {
    return false;
  }

  SDL_LockMutex(q->mutex);
  if (q->durationMs >= maxSpanMs && !q->eof) {
    SDL_UnlockMutex(q->mutex);
    return false;
  }

  Kotonoha_PacketNode *node =
      (Kotonoha_PacketNode *)SDL_calloc(1, sizeof(*node));
  if (!node) {
    SDL_UnlockMutex(q->mutex);
    KLOG_ERROR(DEMUXER, "Could not allocate a demuxer packet queue entry");
    return false;
  }
  node->packet = av_packet_clone(pkt);
  if (!node->packet) {
    SDL_free(node);
    SDL_UnlockMutex(q->mutex);
    KLOG_ERROR(DEMUXER, "Could not clone a demuxer packet");
    return false;
  }

  node->ptsMs = KotonohaFFmpegGetTime(stream, pkt->pts, pkt->dts);
  node->durationMs = KotonohaPacketDurationMs(pkt, stream);
  if (q->tail) {
    q->tail->next = node;
  } else {
    q->head = node;
  }
  q->tail = node;
  q->count++;
  q->durationMs += node->durationMs;
  SDL_SignalCondition(q->cond);
  SDL_UnlockMutex(q->mutex);
  return true;
}

AVPacket *KotonohaPacketQueuePop(Kotonoha_PacketQueue *q, Uint64 *outPtsMs) {
  if (!q || !q->mutex) {
    return NULL;
  }

  SDL_LockMutex(q->mutex);
  Kotonoha_PacketNode *node = q->head;
  if (!node) {
    SDL_UnlockMutex(q->mutex);
    return NULL;
  }

  q->head = node->next;
  if (!q->head) {
    q->tail = NULL;
  }
  q->count--;
  q->durationMs =
      q->durationMs >= node->durationMs ? q->durationMs - node->durationMs : 0;
  AVPacket *packet = node->packet;
  if (outPtsMs) {
    *outPtsMs = node->ptsMs;
  }
  SDL_free(node);
  SDL_SignalCondition(q->cond);
  SDL_UnlockMutex(q->mutex);
  return packet;
}

void KotonohaDemuxStreamFlushBuffers(Kotonoha_DemuxStream *stream,
                                     bool preserveKeyFrame) {
  if (!stream) {
    return;
  }
  KotonohaPacketQueueFlush(&stream->videoQueue, preserveKeyFrame);
  KotonohaPacketQueueFlush(&stream->audioQueue, false);
}

void KotonohaDemuxStreamSeek(Kotonoha_DemuxStream *stream, Uint64 targetMs) {
  if (!stream || !stream->controlMutex) {
    return;
  }
  SDL_LockMutex(stream->controlMutex);
  stream->seekTargetMs = targetMs;
  SDL_SetAtomicInt(&stream->seekRequested, 1);
  KotonohaDemuxStreamFlushBuffers(stream, true);
  KotonohaDemuxStreamSetWaiting(stream, false);
  SDL_UnlockMutex(stream->controlMutex);
}

void KotonohaDemuxStreamSetWaiting(Kotonoha_DemuxStream *stream, bool waiting) {
  if (!stream) {
    return;
  }
  const int value = waiting ? 1 : 0;
  int oldValue = SDL_GetAtomicInt(&stream->waitingForPackets);
  while (oldValue != value) {
    if (SDL_CompareAndSwapAtomicInt(&stream->waitingForPackets, oldValue,
                                    value)) {
      if (stream->demuxer) {
        SDL_AddAtomicInt(&stream->demuxer->waitingStreams, waiting ? 1 : -1);
      }
      break;
    }
    oldValue = SDL_GetAtomicInt(&stream->waitingForPackets);
  }
  if (waiting) {
    Kotonoha_PacketQueue *queue = stream->requestedType == AVMEDIA_TYPE_VIDEO
                                      ? &stream->videoQueue
                                      : &stream->audioQueue;
    if (KotonohaPacketQueueHasPackets(queue) ||
        KotonohaPacketQueueIsEof(queue)) {
      KotonohaDemuxStreamSetWaiting(stream, false);
    }
  }
}

static int KotonohaDemuxInterrupt(void *opaque) {
  Kotonoha_DemuxStream *stream = (Kotonoha_DemuxStream *)opaque;
  if (!stream) {
    return 0;
  }
  if (SDL_GetAtomicInt(&stream->cancelled) != 0 ||
      (SDL_GetAtomicInt(&stream->readingPacket) != 0 &&
       SDL_GetAtomicInt(&stream->seekRequested) != 0) ||
      (SDL_GetAtomicInt(&stream->operationActive) != 0 &&
       stream->demuxer != NULL && stream->demuxer->demuxThread != NULL &&
       SDL_GetAtomicInt(&stream->demuxer->running) == 0)) {
    return 1;
  }
  if (SDL_GetAtomicInt(&stream->operationActive) == 0) {
    return 0;
  }
  const Uint32 timeout = SDL_GetAtomicInt(&stream->readingPacket) != 0
                             ? KOTONOHA_DEMUX_IO_TIMEOUT_MS
                             : KOTONOHA_DEMUX_OPEN_TIMEOUT_MS;
  return SDL_GetTicks() -
             (Uint32)SDL_GetAtomicInt(&stream->operationStartedAt) >=
         timeout;
}

static void KotonohaDemuxOperationBegin(Kotonoha_DemuxStream *stream) {
  SDL_SetAtomicInt(&stream->operationStartedAt, (int)SDL_GetTicks());
  SDL_SetAtomicInt(&stream->operationActive, 1);
}

static void KotonohaDemuxOperationEnd(Kotonoha_DemuxStream *stream) {
  SDL_SetAtomicInt(&stream->operationActive, 0);
}

static void KotonohaDemuxCloseFormat(Kotonoha_DemuxStream *stream) {
  if (!stream) {
    return;
  }
  if (stream->inputContext) {
    KotonohaFFmpegCloseInput(&stream->formatCtx, &stream->inputContext);
  } else if (stream->formatCtx) {
    avformat_close_input(&stream->formatCtx);
  }
}

static bool KotonohaDemuxDescribeStreams(Kotonoha_DemuxStream *stream) {
  if (!stream || !stream->formatCtx) {
    return false;
  }
  int videoIndex = -1;
  int audioIndex = -1;
  AVRational videoTimeBase = {0, 1};
  AVRational audioTimeBase = {0, 1};
  for (unsigned int i = 0; i < stream->formatCtx->nb_streams; ++i) {
    AVStream *avStream = stream->formatCtx->streams[i];
    if (avStream->codecpar->codec_type == AVMEDIA_TYPE_VIDEO && videoIndex < 0) {
      videoIndex = (int)i;
      videoTimeBase = avStream->time_base;
    } else if (avStream->codecpar->codec_type == AVMEDIA_TYPE_AUDIO &&
               audioIndex < 0) {
      audioIndex = (int)i;
      audioTimeBase = avStream->time_base;
    }
  }
  const int selectedIndex =
      stream->requestedType == AVMEDIA_TYPE_VIDEO ? videoIndex : audioIndex;
  if (selectedIndex < 0) {
    KLOG_ERROR(DEMUXER, "No requested media stream found in '%s'",
               stream->filename ? stream->filename : "<custom-input>");
    return false;
  }
  SDL_SetAtomicInt(&stream->totalDurationMs,
                   stream->formatCtx->duration > 0
                       ? (int)(stream->formatCtx->duration /
                               (AV_TIME_BASE / 1000))
                       : 0);
  stream->videoStreamIdx = videoIndex;
  stream->audioStreamIdx = audioIndex;
  stream->videoTimeBase = videoTimeBase;
  stream->audioTimeBase = audioTimeBase;
  stream->formatCtx->interrupt_callback.callback = KotonohaDemuxInterrupt;
  stream->formatCtx->interrupt_callback.opaque = stream;
  return true;
}

static bool KotonohaDemuxOpenStream(Kotonoha_DemuxStream *stream) {
  if (!stream) {
    return false;
  }

  KotonohaDemuxCloseFormat(stream);
  stream->videoStreamIdx = -1;
  stream->audioStreamIdx = -1;

  Kotonoha_FFmpegInput resolvedInput;
  SDL_zero(resolvedInput);
  const Kotonoha_FFmpegInput *customInput = NULL;
  bool direct = false;

  if (stream->hasDirectInput) {
    customInput = &stream->directInput;
    direct = true;
  } else if (stream->filename && *stream->filename) {
    const int resolved = KotonohaFFmpegResolveInput(stream->filename,
                                                    &resolvedInput);
    if (resolved < 0) {
      return false;
    }
    if (resolved > 0) {
      customInput = &resolvedInput;
      stream->resolverBackedInput = true;
    }
  }

  if (customInput) {
    AVCodecContext *dummyCodec = NULL;
    int selectedIndex = -1;
    Kotonoha_FFmpegInputContext *inputContext = NULL;
    AVFormatContext *formatContext = NULL;
    const bool loaded = KotonohaFFmpegLoadInput(
        customInput, &formatContext, &dummyCodec, stream->requestedType,
        &selectedIndex, 0, NULL, &inputContext);
    if (dummyCodec) {
      avcodec_free_context(&dummyCodec);
    }
    if (!loaded) {
      /* Custom input loader owns/closes opaque after validation. */
      if (direct) {
        SDL_zero(stream->directInput);
        stream->hasDirectInput = false;
      }
      return false;
    }
    stream->formatCtx = formatContext;
    stream->inputContext = inputContext;
    if (direct) {
      SDL_zero(stream->directInput);
      stream->hasDirectInput = false;
    }
    return KotonohaDemuxDescribeStreams(stream);
  }

  AVFormatContext *formatCtx = avformat_alloc_context();
  if (!formatCtx) {
    KLOG_ERROR(DEMUXER, "Could not allocate FFmpeg context for '%s'",
               stream->filename);
    return false;
  }
  formatCtx->interrupt_callback.callback = KotonohaDemuxInterrupt;
  formatCtx->interrupt_callback.opaque = stream;

  AVDictionary *options = NULL;
  if (stream->isNetwork) {
    av_dict_set(&options, "rw_timeout", "5000000", 0);
    av_dict_set(&options, "timeout", "5000000", 0);
  }
  if (SDL_strncasecmp(stream->filename, "http://", 7) == 0 ||
      SDL_strncasecmp(stream->filename, "https://", 8) == 0) {
    av_dict_set(&options, "user_agent", "KotonohaEngine/0.1", 0);
  }

  KotonohaDemuxOperationBegin(stream);
  int result = avformat_open_input(&formatCtx, stream->filename, NULL, &options);
  av_dict_free(&options);
  if (result >= 0) {
    KotonohaDemuxOperationBegin(stream);
    result = avformat_find_stream_info(formatCtx, NULL);
    KotonohaDemuxOperationEnd(stream);
  } else {
    KotonohaDemuxOperationEnd(stream);
  }
  if (result < 0) {
    KLOG_ERROR(DEMUXER, "Failed to open '%s': %s", stream->filename,
               av_err2str(result));
    if (formatCtx) {
      avformat_close_input(&formatCtx);
    }
    return false;
  }
  stream->formatCtx = formatCtx;
  if (!KotonohaDemuxDescribeStreams(stream)) {
    KotonohaDemuxCloseFormat(stream);
    return false;
  }
  return true;
}

static void KotonohaDemuxSetQueueEof(Kotonoha_PacketQueue *queue, bool eof) {
  SDL_LockMutex(queue->mutex);
  queue->eof = eof;
  SDL_UnlockMutex(queue->mutex);
}

static bool KotonohaDemuxReopenStream(Kotonoha_DemuxStream *stream) {
  KotonohaDemuxCloseFormat(stream);
  if (stream->directSource || !stream->filename || !*stream->filename) {
    return false;
  }
  if (!KotonohaDemuxOpenStream(stream)) {
    stream->reopenAfterMs = SDL_GetTicks() + 1000;
    return false;
  }

  stream->skipVideoActive = false;
  stream->skipAudioActive = false;
  Uint64 resumeTimeMs = stream->lastPacketPtsMs;
  SDL_LockMutex(stream->controlMutex);
  if (SDL_GetAtomicInt(&stream->seekRequested) != 0) {
    resumeTimeMs = stream->seekTargetMs;
    SDL_SetAtomicInt(&stream->seekRequested, 0);
  }
  SDL_UnlockMutex(stream->controlMutex);

  if (resumeTimeMs > 0) {
    const int index = stream->requestedType == AVMEDIA_TYPE_VIDEO
                          ? stream->videoStreamIdx
                          : stream->audioStreamIdx;
    AVStream *avStream = stream->formatCtx->streams[index];
    const Sint64 timestamp = av_rescale_q(
        (Sint64)resumeTimeMs, (AVRational){1, 1000}, avStream->time_base);
    KotonohaDemuxOperationBegin(stream);
    av_seek_frame(stream->formatCtx, index, timestamp, AVSEEK_FLAG_BACKWARD);
    KotonohaDemuxOperationEnd(stream);
  }
  KotonohaDemuxSetQueueEof(&stream->videoQueue, false);
  KotonohaDemuxSetQueueEof(&stream->audioQueue, false);
  stream->reopenAfterMs = 0;
  KLOG_INFO(DEMUXER, "Reopened media stream '%s'", stream->filename);
  return true;
}

static void KotonohaDemuxHandleSeek(Kotonoha_DemuxStream *stream) {
  SDL_LockMutex(stream->controlMutex);
  if (SDL_GetAtomicInt(&stream->seekRequested) == 0) {
    SDL_UnlockMutex(stream->controlMutex);
    return;
  }
  if (!stream->formatCtx || stream->reopenAfterMs != 0) {
    SDL_UnlockMutex(stream->controlMutex);
    return;
  }
  const Uint64 target = stream->seekTargetMs;
  SDL_SetAtomicInt(&stream->seekRequested, 0);
  stream->skipVideoActive = false;
  stream->skipAudioActive = false;
  SDL_UnlockMutex(stream->controlMutex);
  if (stream->formatCtx) {
    const int index = stream->requestedType == AVMEDIA_TYPE_VIDEO
                          ? stream->videoStreamIdx
                          : stream->audioStreamIdx;
    if (index >= 0) {
      AVStream *avStream = stream->formatCtx->streams[index];
      const Sint64 timestamp = av_rescale_q(
          (Sint64)target, (AVRational){1, 1000}, avStream->time_base);
      KotonohaDemuxOperationBegin(stream);
      const int result = av_seek_frame(stream->formatCtx, index, timestamp,
                                       AVSEEK_FLAG_BACKWARD);
      KotonohaDemuxOperationEnd(stream);
      if (result >= 0) {
        KotonohaDemuxSetQueueEof(&stream->videoQueue, false);
        KotonohaDemuxSetQueueEof(&stream->audioQueue, false);
      }
      if (result < 0) {
        KLOG_WARN(DEMUXER, "Could not seek '%s' to %llu ms: %s",
                  stream->filename, (unsigned long long)target,
                  av_err2str(result));
      }
    }
  }
}

// Buffer limit for a stream, always taken live from the global config.
static Uint64 KotonohaDemuxStreamCacheMs(const Kotonoha_DemuxStream *stream) {
  float seconds = stream->demuxer->config
                      ? stream->demuxer->config->mediaCacheSeconds
                      : 2.0f;
  if (!(seconds >= 0.1f)) {
    seconds = 2.0f;
  }
  return (Uint64)(seconds * 1000.0f);
}

// When the cache limit shrinks, discards the newest buffered packets and moves
// the reader back so the discarded range is read again if the limit grows.
static void KotonohaDemuxTrimCache(Kotonoha_DemuxStream *stream,
                                   Uint64 cacheMs) {
  if (!stream->formatCtx || stream->reopenAfterMs != 0) {
    return;
  }

  Uint64 videoPts = 0;
  Uint64 audioPts = 0;
  const bool videoTrimmed =
      stream->videoStreamIdx >= 0 &&
      KotonohaPacketQueueTrim(&stream->videoQueue, cacheMs, &videoPts);
  const bool audioTrimmed =
      stream->audioStreamIdx >= 0 &&
      KotonohaPacketQueueTrim(&stream->audioQueue, cacheMs, &audioPts);
  if (!videoTrimmed && !audioTrimmed) {
    return;
  }

  if (videoTrimmed) {
    stream->skipVideoActive = true;
    stream->skipVideoUntilMs = videoPts;
  }
  if (audioTrimmed) {
    stream->skipAudioActive = true;
    stream->skipAudioUntilMs = audioPts;
  }

  const bool useVideo = stream->requestedType == AVMEDIA_TYPE_VIDEO
                            ? stream->videoStreamIdx >= 0
                            : stream->audioStreamIdx < 0;
  const int index = useVideo ? stream->videoStreamIdx : stream->audioStreamIdx;
  const Uint64 target = useVideo ? videoPts : audioPts;
  if (index < 0) {
    return;
  }

  AVStream *avStream = stream->formatCtx->streams[index];
  const Sint64 timestamp =
      av_rescale_q((Sint64)target, (AVRational){1, 1000}, avStream->time_base);
  KotonohaDemuxOperationBegin(stream);
  av_seek_frame(stream->formatCtx, index, timestamp, AVSEEK_FLAG_BACKWARD);
  KotonohaDemuxOperationEnd(stream);
}

static bool KotonohaDemuxProcessStream(Kotonoha_DemuxStream *stream,
                                       AVPacket *packet) {
  if (SDL_GetAtomicInt(&stream->cancelled) != 0) {
    return false;
  }
  KotonohaDemuxHandleSeek(stream);

  const Uint64 cacheMs = KotonohaDemuxStreamCacheMs(stream);
  KotonohaDemuxTrimCache(stream, cacheMs);
  const bool videoFull =
      stream->videoStreamIdx < 0 ||
      KotonohaPacketQueueAtCacheLimit(&stream->videoQueue, cacheMs);
  const bool audioFull =
      stream->audioStreamIdx < 0 ||
      KotonohaPacketQueueAtCacheLimit(&stream->audioQueue, cacheMs);
  if (videoFull && audioFull) {
    return false;
  }

  if (stream->reopenAfterMs == 0 && stream->formatCtx &&
      KotonohaPacketQueueIsEof(&stream->videoQueue) &&
      KotonohaPacketQueueIsEof(&stream->audioQueue)) {
    return false;
  }

  if (stream->reopenAfterMs != 0) {
    if (SDL_GetTicks() < stream->reopenAfterMs) {
      return false;
    }
    KotonohaDemuxReopenStream(stream);
    return true;
  }

  if (!stream->formatCtx) {
    KotonohaDemuxReopenStream(stream);
    return true;
  }

  KotonohaDemuxOperationBegin(stream);
  SDL_SetAtomicInt(&stream->readingPacket, 1);
  const int result = av_read_frame(stream->formatCtx, packet);
  SDL_SetAtomicInt(&stream->readingPacket, 0);
  KotonohaDemuxOperationEnd(stream);
  if (result >= 0) {
    const AVStream *packetStream =
        packet->stream_index >= 0 &&
                packet->stream_index < (int)stream->formatCtx->nb_streams
            ? stream->formatCtx->streams[packet->stream_index]
            : NULL;
    const Uint64 packetPtsMs =
        packetStream
            ? KotonohaFFmpegGetTime(packetStream, packet->pts, packet->dts)
            : 0;
    if (packet->stream_index == stream->videoStreamIdx &&
        stream->skipVideoActive) {
      if (packetPtsMs <= stream->skipVideoUntilMs) {
        av_packet_unref(packet);
        return true;
      }
      stream->skipVideoActive = false;
    } else if (packet->stream_index == stream->audioStreamIdx &&
               stream->skipAudioActive) {
      if (packetPtsMs <= stream->skipAudioUntilMs) {
        av_packet_unref(packet);
        return true;
      }
      stream->skipAudioActive = false;
    }

    if (packet->stream_index == stream->videoStreamIdx) {
      const bool queued = KotonohaPacketQueuePush(
          &stream->videoQueue, packet,
          stream->formatCtx->streams[stream->videoStreamIdx],
          KotonohaDemuxStreamCacheMs(stream));
      if (queued && stream->requestedType == AVMEDIA_TYPE_VIDEO) {
        stream->lastPacketPtsMs = KotonohaFFmpegGetTime(
            stream->formatCtx->streams[stream->videoStreamIdx], packet->pts,
            packet->dts);
        KotonohaDemuxStreamSetWaiting(stream, false);
      }
    } else if (packet->stream_index == stream->audioStreamIdx) {
      const bool queued = KotonohaPacketQueuePush(
          &stream->audioQueue, packet,
          stream->formatCtx->streams[stream->audioStreamIdx],
          KotonohaDemuxStreamCacheMs(stream));
      if (queued && stream->requestedType == AVMEDIA_TYPE_AUDIO) {
        stream->lastPacketPtsMs = KotonohaFFmpegGetTime(
            stream->formatCtx->streams[stream->audioStreamIdx], packet->pts,
            packet->dts);
        KotonohaDemuxStreamSetWaiting(stream, false);
      }
    }
    av_packet_unref(packet);
  } else if (result == AVERROR_EOF) {
    KLOG_DEBUG(DEMUXER, "End of media reached: '%s'", stream->filename);
    KotonohaDemuxSetQueueEof(&stream->videoQueue, true);
    KotonohaDemuxSetQueueEof(&stream->audioQueue, true);
    KotonohaDemuxStreamSetWaiting(stream, false);
  } else if (result == AVERROR_EXIT &&
             SDL_GetAtomicInt(&stream->seekRequested) != 0) {
    av_packet_unref(packet);
  } else if (result != AVERROR(EAGAIN)) {
    stream->reopenAfterMs = SDL_GetTicks() + KOTONOHA_DEMUX_REOPEN_DELAY_MS;
    if (result != AVERROR_EXIT) {
      KLOG_WARN(DEMUXER, "Read failed for '%s'; reopening media input: %s",
                stream->filename, av_err2str(result));
    }
    av_packet_unref(packet);
  }
  return true;
}

static Kotonoha_DemuxStream *
KotonohaDemuxNextStreamLocked(Kotonoha_Demuxer *demuxer,
                              Kotonoha_DemuxStream *after) {
  Kotonoha_DemuxStream *stream = after ? after->next : demuxer->streamsHead;
  while (stream && SDL_GetAtomicInt(&stream->cancelled) != 0) {
    stream = stream->next;
  }
  if (stream) {
    stream->workerRefs++;
  }
  return stream;
}

static int SDLCALL KotonohaDemuxerWorker(void *opaque) {
  Kotonoha_Demuxer *demuxer = (Kotonoha_Demuxer *)opaque;
  AVPacket *packet = av_packet_alloc();
  if (!packet) {
    KLOG_ERROR(DEMUXER, "Could not allocate demuxer packet");
    SDL_SetAtomicInt(&demuxer->running, 0);
    return -1;
  }

  while (SDL_GetAtomicInt(&demuxer->running) != 0) {
    bool didWork = false;
    SDL_LockMutex(demuxer->mutex);
    Kotonoha_DemuxStream *stream = KotonohaDemuxNextStreamLocked(demuxer, NULL);
    SDL_UnlockMutex(demuxer->mutex);

    while (stream && SDL_GetAtomicInt(&demuxer->running) != 0) {
      didWork |= KotonohaDemuxProcessStream(stream, packet);

      SDL_LockMutex(demuxer->mutex);
      Kotonoha_DemuxStream *next =
          SDL_GetAtomicInt(&demuxer->running) != 0
              ? KotonohaDemuxNextStreamLocked(demuxer, stream)
              : NULL;
      stream->workerRefs--;
      SDL_BroadcastCondition(demuxer->streamCondition);
      SDL_UnlockMutex(demuxer->mutex);
      stream = next;
    }

    if (stream) {
      SDL_LockMutex(demuxer->mutex);
      stream->workerRefs--;
      SDL_BroadcastCondition(demuxer->streamCondition);
      SDL_UnlockMutex(demuxer->mutex);
    }

    SDL_Delay(didWork ? 1 : 3);
  }

  av_packet_free(&packet);
  return 0;
}

Kotonoha_Demuxer *KotonohaDemuxerCreate(bool retryMissingAssets,
                                        const Kotonoha_Config *config) {
  Kotonoha_Demuxer *demuxer =
      (Kotonoha_Demuxer *)SDL_calloc(1, sizeof(*demuxer));
  if (!demuxer) {
    return NULL;
  }
  demuxer->mutex = SDL_CreateMutex();
  demuxer->streamCondition = SDL_CreateCondition();
  if (!demuxer->mutex || !demuxer->streamCondition) {
    if (demuxer->mutex) {
      SDL_DestroyMutex(demuxer->mutex);
    }
    if (demuxer->streamCondition) {
      SDL_DestroyCondition(demuxer->streamCondition);
    }
    SDL_free(demuxer);
    return NULL;
  }
  demuxer->retryMissingAssets = retryMissingAssets;
  demuxer->config = config;
  SDL_SetAtomicInt(&demuxer->running, 0);
  SDL_SetAtomicInt(&demuxer->waitingStreams, 0);
  SDL_SetAtomicInt(&demuxer->externalWaiters, 0);
  return demuxer;
}

void KotonohaDemuxerStart(Kotonoha_Demuxer *demuxer) {
  if (!demuxer || SDL_GetAtomicInt(&demuxer->running) != 0) {
    return;
  }
  if (demuxer->demuxThread != NULL) {
    SDL_WaitThread(demuxer->demuxThread, NULL);
    demuxer->demuxThread = NULL;
  }
  SDL_SetAtomicInt(&demuxer->running, 1);
  demuxer->demuxThread = SDL_CreateThread(KotonohaDemuxerWorker,
                                          "KotonohaGameplayDemuxer", demuxer);
  if (!demuxer->demuxThread) {
    SDL_SetAtomicInt(&demuxer->running, 0);
    KLOG_ERROR(DEMUXER, "Could not start the media demuxer thread: %s",
               SDL_GetError());
  }
}

void KotonohaDemuxerSuspend(Kotonoha_Demuxer *demuxer) {
  if (!demuxer) {
    return;
  }
  SDL_SetAtomicInt(&demuxer->running, 0);
  if (demuxer->demuxThread) {
    SDL_WaitThread(demuxer->demuxThread, NULL);
    demuxer->demuxThread = NULL;
  }
}

void KotonohaDemuxerStop(Kotonoha_Demuxer *demuxer) {
  if (!demuxer) {
    return;
  }
  SDL_SetAtomicInt(&demuxer->running, 0);
  if (demuxer->demuxThread) {
    SDL_WaitThread(demuxer->demuxThread, NULL);
    demuxer->demuxThread = NULL;
  }
  SDL_LockMutex(demuxer->mutex);
  for (Kotonoha_DemuxStream *stream = demuxer->streamsHead; stream;
       stream = stream->next) {
    KotonohaDemuxStreamFlushBuffers(stream, false);
    KotonohaDemuxStreamSetWaiting(stream, false);
  }
  SDL_UnlockMutex(demuxer->mutex);
}

bool KotonohaDemuxerIsStarted(Kotonoha_Demuxer *demuxer) {
  return demuxer && SDL_GetAtomicInt(&demuxer->running) != 0;
}

bool KotonohaDemuxerIsLoading(Kotonoha_Demuxer *demuxer) {
  if (!demuxer) {
    return false;
  }
  return SDL_GetAtomicInt(&demuxer->externalWaiters) > 0 ||
         SDL_GetAtomicInt(&demuxer->waitingStreams) > 0;
}

void KotonohaDemuxerSetExternalWaiter(Kotonoha_Demuxer *demuxer, bool waiting) {
  if (!demuxer) {
    return;
  }
  int oldValue = SDL_GetAtomicInt(&demuxer->externalWaiters);
  int newValue = waiting ? oldValue + 1 : (oldValue > 0 ? oldValue - 1 : 0);
  while (!SDL_CompareAndSwapAtomicInt(&demuxer->externalWaiters, oldValue,
                                      newValue)) {
    oldValue = SDL_GetAtomicInt(&demuxer->externalWaiters);
    newValue = waiting ? oldValue + 1 : (oldValue > 0 ? oldValue - 1 : 0);
  }
}

static Kotonoha_DemuxStream *KotonohaDemuxerCreateStream(
    Kotonoha_Demuxer *demuxer, const char *filename,
    const Kotonoha_FFmpegInput *input, void *owner, enum AVMediaType type) {
  const bool pathInput = filename && *filename;
  const bool directInput = input && input->opaque && input->read;
  if (!demuxer || pathInput == directInput) {
    return NULL;
  }

  Kotonoha_DemuxStream *stream =
      (Kotonoha_DemuxStream *)SDL_calloc(1, sizeof(*stream));
  if (!stream) {
    if (directInput && input->close) {
      input->close(input->opaque);
    }
    KLOG_ERROR(DEMUXER, "Could not allocate demuxer stream");
    return NULL;
  }
  const char *displayName = pathInput
                                ? filename
                                : (input->name && *input->name
                                       ? input->name
                                       : "<custom-input>");
  stream->filename = SDL_strdup(displayName);
  stream->videoStreamIdx = -1;
  stream->audioStreamIdx = -1;
  stream->requestedType = type;
  stream->owner = owner;
  stream->demuxer = demuxer;
  stream->controlMutex = SDL_CreateMutex();
  stream->isNetwork = pathInput && KotonohaDemuxIsNetworkPath(filename);
  if (directInput) {
    stream->directInput = *input;
    stream->hasDirectInput = true;
    stream->directSource = true;
  }
  KotonohaPacketQueueInit(&stream->videoQueue);
  KotonohaPacketQueueInit(&stream->audioQueue);

  if (!stream->filename || !stream->controlMutex || !stream->videoQueue.mutex ||
      !stream->videoQueue.cond || !stream->audioQueue.mutex ||
      !stream->audioQueue.cond || !KotonohaDemuxOpenStream(stream)) {
    if (stream->hasDirectInput && stream->directInput.close &&
        stream->directInput.opaque) {
      stream->directInput.close(stream->directInput.opaque);
      SDL_zero(stream->directInput);
      stream->hasDirectInput = false;
    }
    KotonohaDemuxCloseFormat(stream);
    KotonohaPacketQueueDestroy(&stream->videoQueue);
    KotonohaPacketQueueDestroy(&stream->audioQueue);
    if (stream->controlMutex) {
      SDL_DestroyMutex(stream->controlMutex);
    }
    SDL_free(stream->filename);
    SDL_free(stream);
    return NULL;
  }
  return stream;
}

Kotonoha_DemuxStream *KotonohaDemuxerAddStream(Kotonoha_Demuxer *demuxer,
                                               const char *filename,
                                               void *owner,
                                               enum AVMediaType type) {
  return KotonohaDemuxerCreateStream(demuxer, filename, NULL, owner, type);
}

Kotonoha_DemuxStream *KotonohaDemuxerAddStreamInput(
    Kotonoha_Demuxer *demuxer, const Kotonoha_FFmpegInput *input, void *owner,
    enum AVMediaType type) {
  return KotonohaDemuxerCreateStream(demuxer, NULL, input, owner, type);
}

bool KotonohaDemuxerActivateStream(Kotonoha_Demuxer *demuxer,
                                   Kotonoha_DemuxStream *stream) {
  if (!demuxer || !stream || stream->demuxer != demuxer) {
    return false;
  }
  SDL_LockMutex(demuxer->mutex);
  if (!stream->registered && SDL_GetAtomicInt(&stream->cancelled) == 0) {
    stream->next = demuxer->streamsHead;
    demuxer->streamsHead = stream;
    stream->registered = true;
  }
  const bool registered = stream->registered;
  SDL_UnlockMutex(demuxer->mutex);
  return registered;
}

void KotonohaDemuxerRemoveStream(Kotonoha_Demuxer *demuxer,
                                 Kotonoha_DemuxStream **streamPtr) {
  if (!demuxer || !streamPtr || !*streamPtr) {
    return;
  }
  Kotonoha_DemuxStream *stream = *streamPtr;

  SDL_LockMutex(demuxer->mutex);
  SDL_SetAtomicInt(&stream->cancelled, 1);
  while (stream->workerRefs > 0) {
    SDL_WaitCondition(demuxer->streamCondition, demuxer->mutex);
  }
  KotonohaDemuxStreamSetWaiting(stream, false);
  if (stream->registered) {
    if (demuxer->streamsHead == stream) {
      demuxer->streamsHead = stream->next;
    } else {
      Kotonoha_DemuxStream *current = demuxer->streamsHead;
      while (current && current->next != stream) {
        current = current->next;
      }
      if (current) {
        current->next = stream->next;
      }
    }
    stream->registered = false;
  }
  SDL_UnlockMutex(demuxer->mutex);

  KotonohaPacketQueueDestroy(&stream->videoQueue);
  KotonohaPacketQueueDestroy(&stream->audioQueue);
  SDL_DestroyMutex(stream->controlMutex);
  KotonohaDemuxCloseFormat(stream);
  SDL_free(stream->filename);
  SDL_free(stream);
  *streamPtr = NULL;
}

void KotonohaDemuxerDestroy(Kotonoha_Demuxer **demuxer) {
  if (!demuxer || !*demuxer) {
    return;
  }
  Kotonoha_Demuxer *instance = *demuxer;
  KotonohaDemuxerStop(instance);

  SDL_LockMutex(instance->mutex);
  Kotonoha_DemuxStream *stream = instance->streamsHead;
  instance->streamsHead = NULL;
  SDL_UnlockMutex(instance->mutex);
  while (stream) {
    Kotonoha_DemuxStream *next = stream->next;
    KotonohaPacketQueueDestroy(&stream->videoQueue);
    KotonohaPacketQueueDestroy(&stream->audioQueue);
    SDL_DestroyMutex(stream->controlMutex);
    KotonohaDemuxCloseFormat(stream);
    SDL_free(stream->filename);
    SDL_free(stream);
    stream = next;
  }
  SDL_DestroyMutex(instance->mutex);
  SDL_DestroyCondition(instance->streamCondition);
  SDL_free(instance);
  *demuxer = NULL;
}
