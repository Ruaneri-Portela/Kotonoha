#include <Kotonoha/utils/Demuxer.h>

void KotonohaPacketQueueInit(Kotonoha_PacketQueue *q) {
  SDL_memset(q, 0, sizeof(Kotonoha_PacketQueue));
  q->mutex = SDL_CreateMutex();
  q->cond = SDL_CreateCondition();
}

void KotonohaPacketQueueFlush(Kotonoha_PacketQueue *q, bool preserveKeyFrame) {
  if (!q || !q->mutex)
    return;

  SDL_LockMutex(q->mutex);
  Kotonoha_PacketNode *curr = q->head;
  Kotonoha_PacketNode *savedKeyFrameNode = NULL;

  while (curr != NULL) {
    Kotonoha_PacketNode *next = curr->next;

    if (preserveKeyFrame && savedKeyFrameNode == NULL && curr->packet != NULL &&
        (curr->packet->flags & AV_PKT_FLAG_KEY) != 0) {
      savedKeyFrameNode = curr;
      savedKeyFrameNode->next = NULL;
    } else {
      if (curr->packet != NULL) {
        av_packet_free(&curr->packet);
      }
      SDL_free(curr);
    }
    curr = next;
  }

  if (savedKeyFrameNode != NULL) {
    q->head = savedKeyFrameNode;
    q->tail = savedKeyFrameNode;
    q->count = 1;
    q->durationMs = 0;
  } else {
    q->head = NULL;
    q->tail = NULL;
    q->count = 0;
    q->durationMs = 0;
  }

  q->eof = false;
  SDL_UnlockMutex(q->mutex);
}

void KotonohaPacketQueueDestroy(Kotonoha_PacketQueue *q) {
  if (!q)
    return;
  KotonohaPacketQueueFlush(q, false);
  if (q->mutex != NULL)
    SDL_DestroyMutex(q->mutex);
  if (q->cond != NULL)
    SDL_DestroyCondition(q->cond);
}

// Modificação em KotonohaPacketQueuePush para evitar retenção de locks globais
// Em Demuxer.c - Ajuste na rotina KotonohaPacketQueuePush
bool KotonohaPacketQueuePush(Kotonoha_PacketQueue *q, AVPacket *pkt,
                             AVStream *stream, Uint64 maxSpanMs) {
  if (!q || !q->mutex)
    return false;

  SDL_LockMutex(q->mutex);

  // Se a fila atingiu a capacidade, não bloqueie infinitamente sob risco de
  // deadlock; retorne false para que o demuxer worker libere o ciclo e tente
  // novamente na próxima iteração.
  if (q->durationMs >= maxSpanMs && !q->eof) {
    SDL_UnlockMutex(q->mutex);
    return false;
  }

  Kotonoha_PacketNode *node =
      (Kotonoha_PacketNode *)SDL_calloc(1, sizeof(Kotonoha_PacketNode));
  if (node == NULL) {
    SDL_UnlockMutex(q->mutex);
    return false;
  }

  node->packet = av_packet_clone(pkt);
  if (!node->packet) {
    SDL_free(node);
    SDL_UnlockMutex(q->mutex);
    return false;
  }

  node->ptsMs =
      KotonohaFFmpegGetTime(stream, pkt->pts, pkt->dts); //[cite: 3, 4]
  node->next = NULL;

  if (q->tail == NULL) {
    q->head = node;
    q->tail = node;
  } else {
    q->tail->next = node;
    q->tail = node;
  }
  q->count++;

  q->durationMs = (q->tail->ptsMs >= q->head->ptsMs)
                      ? (q->tail->ptsMs - q->head->ptsMs)
                      : 0; //

  SDL_SignalCondition(q->cond); //[cite: 3]
  SDL_UnlockMutex(q->mutex);
  return true;
}

AVPacket *KotonohaPacketQueuePop(Kotonoha_PacketQueue *q, Uint64 *outPtsMs) {
  if (!q || !q->mutex)
    return NULL;

  SDL_LockMutex(q->mutex);
  if (q->head == NULL) {
    SDL_UnlockMutex(q->mutex);
    return NULL;
  }

  Kotonoha_PacketNode *node = q->head;
  q->head = node->next;
  if (q->head == NULL)
    q->tail = NULL;
  q->count--;

  if (q->head != NULL && q->tail != NULL) {
    q->durationMs = (q->tail->ptsMs >= q->head->ptsMs)
                        ? (q->tail->ptsMs - q->head->ptsMs)
                        : 0;
  } else {
    q->durationMs = 0;
  }

  AVPacket *pkt = node->packet;
  if (outPtsMs != NULL)
    *outPtsMs = node->ptsMs;

  SDL_free(node);

  SDL_SignalCondition(q->cond);
  SDL_UnlockMutex(q->mutex);
  return pkt;
}

void KotonohaDemuxStreamFlushBuffers(Kotonoha_DemuxStream *stream,
                                     bool preserveKeyFrame) {
  if (!stream)
    return;

  KotonohaPacketQueueFlush(&stream->videoQueue, preserveKeyFrame);
  KotonohaPacketQueueFlush(&stream->audioQueue, false);
}

void KotonohaDemuxStreamSeek(Kotonoha_DemuxStream *stream, Uint64 targetMs) {
  if (stream == NULL)
    return;

  KotonohaDemuxStreamFlushBuffers(stream, true);
  stream->seekTargetMs = targetMs;
  SDL_SetAtomicInt(&stream->seekRequested, 1);
}

static int SDLCALL KotonohaDemuxerWorker(void *opaque) {
  Kotonoha_Demuxer *demuxer = (Kotonoha_Demuxer *)opaque;
  AVPacket *pkt = av_packet_alloc();

  while (SDL_GetAtomicInt(&demuxer->running) != 0) {
    bool allFullOrEmpty = true;

    // Copiamos os ponteiros das streams para evitar segurar demuxer->mutex
    // durante o I/O
    SDL_LockMutex(demuxer->mutex);
    // Processamento de seek rápido sob lock
    for (Kotonoha_DemuxStream *s = demuxer->streamsHead; s != NULL;
         s = s->next) {
      if (SDL_GetAtomicInt(&s->seekRequested) != 0) {
        const Uint64 target = s->seekTargetMs;
        KotonohaDemuxStreamFlushBuffers(s, true); //[cite: 3]

        const int activeStream = (s->videoStreamIdx >= 0)
                                     ? s->videoStreamIdx
                                     : s->audioStreamIdx; //[cite: 3]
        if (activeStream >= 0 && s->formatCtx != NULL) {
          AVStream *st = s->formatCtx->streams[activeStream];
          const int64_t seekTs = av_rescale_q(
              (int64_t)target, (AVRational){1, 1000}, st->time_base);
          KotonohaFFmpegSeekFrame(s->formatCtx, &s->ioOperation, activeStream,
                                  seekTs, AVSEEK_FLAG_BACKWARD); //[cite: 3, 4]
        }
        SDL_SetAtomicInt(&s->seekRequested, 0);
        allFullOrEmpty = false;
      }
    }

    // Processamento de leitura sem segurar a tranca global do demuxer
    for (Kotonoha_DemuxStream *s = demuxer->streamsHead; s != NULL;
         s = s->next) {
      const bool videoFull =
          (s->videoStreamIdx < 0) ||
          (s->videoQueue.durationMs >= s->targetCacheMs); //[cite: 3]
      const bool audioFull =
          (s->audioStreamIdx < 0) ||
          (s->audioQueue.durationMs >= s->targetCacheMs); //[cite: 3]

      if (!videoFull || !audioFull) {
        allFullOrEmpty = false;
        if (s->formatCtx != NULL) {
          int ret = KotonohaFFmpegReadFrame(s->formatCtx, pkt); //[cite: 3, 4]
          if (ret >= 0) {
            if (pkt->stream_index == s->videoStreamIdx) {
              if (!KotonohaPacketQueuePush(
                      &s->videoQueue, pkt,
                      s->formatCtx->streams[s->videoStreamIdx],
                      s->targetCacheMs)) {
                // Se a fila estiver cheia, desfaz e tenta na próxima rodada
              }
            } else if (pkt->stream_index == s->audioStreamIdx) {
              if (!KotonohaPacketQueuePush(
                      &s->audioQueue, pkt,
                      s->formatCtx->streams[s->audioStreamIdx],
                      s->targetCacheMs)) {
                // Se a fila estiver cheia, desfaz e tenta na próxima rodada
              }
            }
            av_packet_unref(pkt); //[cite: 3]
          } else if (ret == AVERROR_EOF) {
            SDL_LockMutex(s->videoQueue.mutex);
            s->videoQueue.eof = true;
            SDL_UnlockMutex(s->videoQueue.mutex);

            SDL_LockMutex(s->audioQueue.mutex);
            s->audioQueue.eof = true;
            SDL_UnlockMutex(s->audioQueue.mutex); //[cite: 3]
          }
        }
      }
    }
    SDL_UnlockMutex(demuxer->mutex);

    if (allFullOrEmpty) {
      SDL_Delay(2);
    }
  }

  av_packet_free(&pkt); //[cite: 3]
  return 0;
}

Kotonoha_Demuxer *KotonohaDemuxerCreate(Kotonoha_IOMonitor *ioMonitor) {
  Kotonoha_Demuxer *demuxer =
      (Kotonoha_Demuxer *)SDL_calloc(1, sizeof(Kotonoha_Demuxer));
  if (demuxer == NULL)
    return NULL;

  demuxer->ioMonitor = ioMonitor;
  demuxer->mutex = SDL_CreateMutex();
  demuxer->streamsHead = NULL;
  SDL_SetAtomicInt(&demuxer->running, 0);
  demuxer->demuxThread = NULL;

  return demuxer;
}

void KotonohaDemuxerStart(Kotonoha_Demuxer *demuxer) {
  if (demuxer == NULL)
    return;

  if (SDL_GetAtomicInt(&demuxer->running) == 0 &&
      demuxer->demuxThread == NULL) {
    SDL_SetAtomicInt(&demuxer->running, 1);
    demuxer->demuxThread = SDL_CreateThread(KotonohaDemuxerWorker,
                                            "KotonohaGameplayDemuxer", demuxer);
  }
}

void KotonohaDemuxerStop(Kotonoha_Demuxer *demuxer) {
  if (demuxer == NULL)
    return;

  if (SDL_GetAtomicInt(&demuxer->running) != 0) {
    SDL_SetAtomicInt(&demuxer->running, 0);

    if (demuxer->demuxThread != NULL) {
      SDL_WaitThread(demuxer->demuxThread, NULL);
      demuxer->demuxThread = NULL;
    }

    SDL_LockMutex(demuxer->mutex);
    for (Kotonoha_DemuxStream *s = demuxer->streamsHead; s != NULL;
         s = s->next) {
      KotonohaDemuxStreamFlushBuffers(s, false);
    }
    SDL_UnlockMutex(demuxer->mutex);
  }
}

bool KotonohaDemuxerIsStarted(Kotonoha_Demuxer *demuxer) {
  if (demuxer == NULL)
    return false;
  return SDL_GetAtomicInt(&demuxer->running) != 0;
}

Kotonoha_DemuxStream *KotonohaDemuxerAddStream(Kotonoha_Demuxer *demuxer,
                                               const char *filename,
                                               float cacheDurationSeconds,
                                               void *owner,
                                               enum AVMediaType type) {
  if (demuxer == NULL || filename == NULL || *filename == '\0') {
    return NULL;
  }

  Kotonoha_DemuxStream *stream =
      (Kotonoha_DemuxStream *)SDL_calloc(1, sizeof(Kotonoha_DemuxStream));
  if (stream == NULL)
    return NULL;

  stream->filename = SDL_strdup(filename);
  stream->targetCacheMs =
      (Uint64)((cacheDurationSeconds > 0.1f ? cacheDurationSeconds : 2.0f) *
               1000.0f);
  stream->videoStreamIdx = -1;
  stream->audioStreamIdx = -1;
  stream->owner = owner;

  KotonohaIOMonitorOperationInit(&stream->ioOperation, demuxer->ioMonitor);
  KotonohaPacketQueueInit(&stream->videoQueue);
  KotonohaPacketQueueInit(&stream->audioQueue);

  AVCodecContext *dummyCodecCtx = NULL;

  // Tenta carregar o stream com base no tipo de mídia solicitado de entrada
  if (!KotonohaFFmpegLoadMonitored(
          filename, &stream->formatCtx, &dummyCodecCtx, type,
          (type == AVMEDIA_TYPE_VIDEO) ? &stream->videoStreamIdx
                                       : &stream->audioStreamIdx,
          0, NULL, &stream->ioOperation)) {
    // Se falhar o tipo principal, limpa os recursos e retorna NULL
    KotonohaPacketQueueDestroy(&stream->videoQueue);
    KotonohaPacketQueueDestroy(&stream->audioQueue);
    KotonohaIOMonitorOperationReset(&stream->ioOperation);
    SDL_free(stream->filename);
    SDL_free(stream);
    return NULL;
  } else {
    if (dummyCodecCtx) {
      avcodec_free_context(&dummyCodecCtx);
    }

    // Varre o formatCtx para encontrar e preencher os índices de vídeo e áudio
    // disponíveis
    for (unsigned int i = 0; i < stream->formatCtx->nb_streams; i++) {
      if (stream->formatCtx->streams[i]->codecpar->codec_type ==
              AVMEDIA_TYPE_VIDEO &&
          stream->videoStreamIdx < 0) {
        stream->videoStreamIdx = (int)i;
      } else if (stream->formatCtx->streams[i]->codecpar->codec_type ==
                     AVMEDIA_TYPE_AUDIO &&
                 stream->audioStreamIdx < 0) {
        stream->audioStreamIdx = (int)i;
      }
    }
  }

  SDL_LockMutex(demuxer->mutex);
  stream->next = demuxer->streamsHead;
  demuxer->streamsHead = stream;
  SDL_UnlockMutex(demuxer->mutex);

  return stream;
}

void KotonohaDemuxerRemoveStream(Kotonoha_Demuxer *demuxer,
                                 Kotonoha_DemuxStream **streamPtr) {
  if (demuxer == NULL || streamPtr == NULL || *streamPtr == NULL)
    return;

  Kotonoha_DemuxStream *s = *streamPtr;

  SDL_LockMutex(demuxer->mutex);
  if (demuxer->streamsHead == s) {
    demuxer->streamsHead = s->next;
  } else {
    Kotonoha_DemuxStream *curr = demuxer->streamsHead;
    while (curr != NULL && curr->next != s) {
      curr = curr->next;
    }
    if (curr != NULL) {
      curr->next = s->next;
    }
  }
  SDL_UnlockMutex(demuxer->mutex);

  KotonohaPacketQueueDestroy(&s->videoQueue);
  KotonohaPacketQueueDestroy(&s->audioQueue);

  if (s->formatCtx != NULL) {
    avformat_close_input(&s->formatCtx);
  }

  KotonohaIOMonitorOperationReset(&s->ioOperation);
  if (s->filename != NULL) {
    SDL_free(s->filename);
  }
  SDL_free(s);
  *streamPtr = NULL;
}

void KotonohaDemuxerDestroy(Kotonoha_Demuxer **demuxer) {
  if (demuxer == NULL || *demuxer == NULL)
    return;

  Kotonoha_Demuxer *d = *demuxer;

  KotonohaDemuxerStop(d);

  SDL_LockMutex(d->mutex);
  Kotonoha_DemuxStream *curr = d->streamsHead;
  while (curr != NULL) {
    Kotonoha_DemuxStream *next = curr->next;
    KotonohaPacketQueueDestroy(&curr->videoQueue);
    KotonohaPacketQueueDestroy(&curr->audioQueue);
    if (curr->formatCtx != NULL) {
      avformat_close_input(&curr->formatCtx);
    }
    KotonohaIOMonitorOperationReset(&curr->ioOperation);
    if (curr->filename != NULL) {
      SDL_free(curr->filename);
    }
    SDL_free(curr);
    curr = next;
  }
  d->streamsHead = NULL;
  SDL_UnlockMutex(d->mutex);

  if (d->mutex != NULL) {
    SDL_DestroyMutex(d->mutex);
  }

  SDL_free(d);
  *demuxer = NULL;
}