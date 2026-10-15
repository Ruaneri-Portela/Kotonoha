#pragma once

#include <Kotonoha/utils/FFmpeg.h>
#include <SDL3/SDL.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct Kotonoha_PacketNode {
  AVPacket *packet;
  Uint64 ptsMs;
  struct Kotonoha_PacketNode *next;
} Kotonoha_PacketNode;

typedef struct Kotonoha_PacketQueue {
  Kotonoha_PacketNode *head;
  Kotonoha_PacketNode *tail;
  int count;
  Uint64 durationMs;
  SDL_Mutex *mutex;
  SDL_Condition *cond;
  bool eof;
} Kotonoha_PacketQueue;

typedef struct Kotonoha_DemuxStream {
  char *filename;
  AVFormatContext *formatCtx;
  int videoStreamIdx;
  int audioStreamIdx;

  Kotonoha_PacketQueue videoQueue;
  Kotonoha_PacketQueue audioQueue;

  Uint64 targetCacheMs;
  SDL_AtomicInt seekRequested;
  Uint64 seekTargetMs;

  Kotonoha_IOMonitorOperation ioOperation;
  void *owner;
  struct Kotonoha_DemuxStream *next;
} Kotonoha_DemuxStream;

typedef struct Kotonoha_Demuxer {
  Kotonoha_DemuxStream *streamsHead;
  SDL_Mutex *mutex;
  SDL_Thread *demuxThread;
  SDL_AtomicInt running;
  Kotonoha_IOMonitor *ioMonitor;
} Kotonoha_Demuxer;

void KotonohaPacketQueueInit(Kotonoha_PacketQueue *q);
void KotonohaPacketQueueDestroy(Kotonoha_PacketQueue *q);
void KotonohaPacketQueueFlush(Kotonoha_PacketQueue *q, bool preserveKeyFrame);
bool KotonohaPacketQueuePush(Kotonoha_PacketQueue *q, AVPacket *pkt,
                             AVStream *stream, Uint64 maxSpanMs);

AVPacket *KotonohaPacketQueuePop(Kotonoha_PacketQueue *q, Uint64 *outPtsMs);

Kotonoha_Demuxer *KotonohaDemuxerCreate(Kotonoha_IOMonitor *ioMonitor);
void KotonohaDemuxerStart(Kotonoha_Demuxer *demuxer);
void KotonohaDemuxerStop(Kotonoha_Demuxer *demuxer);
bool KotonohaDemuxerIsStarted(Kotonoha_Demuxer *demuxer);
void KotonohaDemuxerDestroy(Kotonoha_Demuxer **demuxer);

Kotonoha_DemuxStream *KotonohaDemuxerAddStream(Kotonoha_Demuxer *demuxer,
                                               const char *filename,
                                               float cacheDurationSeconds,
                                               void *owner, enum AVMediaType type);

void KotonohaDemuxerRemoveStream(Kotonoha_Demuxer *demuxer,
                                 Kotonoha_DemuxStream **stream);

void KotonohaDemuxStreamSeek(Kotonoha_DemuxStream *stream, Uint64 targetMs);
void KotonohaDemuxStreamFlushBuffers(Kotonoha_DemuxStream *stream,
                                     bool preserveKeyFrame);

#ifdef __cplusplus
}
#endif