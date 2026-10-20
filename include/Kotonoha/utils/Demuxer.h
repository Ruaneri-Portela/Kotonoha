#pragma once

#include <Kotonoha/utils/FFmpeg.h>
#include <SDL3/SDL.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct Kotonoha_PacketNode {
  AVPacket *packet;
  Uint64 ptsMs;
  Uint64 durationMs;
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

struct Kotonoha_Demuxer;
typedef struct Kotonoha_DemuxStream {
  char *filename;
  AVFormatContext *formatCtx;
  int videoStreamIdx;
  int audioStreamIdx;
  AVRational videoTimeBase;
  AVRational audioTimeBase;
  enum AVMediaType requestedType;

  Kotonoha_PacketQueue videoQueue;
  Kotonoha_PacketQueue audioQueue;

  Uint64 targetCacheMs;
  SDL_AtomicInt seekRequested;
  Uint64 seekTargetMs;
  SDL_Mutex *controlMutex;
  SDL_AtomicInt readingPacket;

  SDL_AtomicInt waitingForPackets;
  SDL_AtomicInt operationActive;
  SDL_AtomicInt operationStartedAt;
  SDL_AtomicInt cancelled;
  Uint64 lastPacketPtsMs;
  Uint64 reopenAfterMs;
  bool isNetwork;
  bool registered;
  int workerRefs;
  void *owner;
  struct Kotonoha_Demuxer *demuxer;
  struct Kotonoha_DemuxStream *next;
} Kotonoha_DemuxStream;

typedef struct Kotonoha_Demuxer {
  Kotonoha_DemuxStream *streamsHead;
  SDL_Mutex *mutex;
  SDL_Condition *streamCondition;
  SDL_Thread *demuxThread;
  SDL_AtomicInt running;
  SDL_AtomicInt waitingStreams;
  SDL_AtomicInt externalWaiters;
  bool retryMissingAssets;
} Kotonoha_Demuxer;

void KotonohaPacketQueueInit(Kotonoha_PacketQueue *q);
void KotonohaPacketQueueDestroy(Kotonoha_PacketQueue *q);
void KotonohaPacketQueueFlush(Kotonoha_PacketQueue *q, bool preserveKeyFrame);
bool KotonohaPacketQueueHasPackets(Kotonoha_PacketQueue *q);
bool KotonohaPacketQueueIsEof(Kotonoha_PacketQueue *q);
bool KotonohaPacketQueueAtCacheLimit(Kotonoha_PacketQueue *q,
                                     Uint64 maxSpanMs);
bool KotonohaPacketQueuePush(Kotonoha_PacketQueue *q, AVPacket *pkt,
                             AVStream *stream, Uint64 maxSpanMs);

AVPacket *KotonohaPacketQueuePop(Kotonoha_PacketQueue *q, Uint64 *outPtsMs);

Kotonoha_Demuxer *KotonohaDemuxerCreate(bool retryMissingAssets);
void KotonohaDemuxerStart(Kotonoha_Demuxer *demuxer);
void KotonohaDemuxerSuspend(Kotonoha_Demuxer *demuxer);
void KotonohaDemuxerStop(Kotonoha_Demuxer *demuxer);
bool KotonohaDemuxerIsStarted(Kotonoha_Demuxer *demuxer);
bool KotonohaDemuxerIsLoading(Kotonoha_Demuxer *demuxer);
void KotonohaDemuxerSetExternalWaiter(Kotonoha_Demuxer *demuxer, bool waiting);
void KotonohaDemuxerDestroy(Kotonoha_Demuxer **demuxer);

Kotonoha_DemuxStream *KotonohaDemuxerAddStream(Kotonoha_Demuxer *demuxer,
                                               const char *filename,
                                               float cacheDurationSeconds,
                                               void *owner, enum AVMediaType type);
bool KotonohaDemuxerActivateStream(Kotonoha_Demuxer *demuxer,
                                   Kotonoha_DemuxStream *stream);

void KotonohaDemuxerRemoveStream(Kotonoha_Demuxer *demuxer,
                                 Kotonoha_DemuxStream **stream);

void KotonohaDemuxStreamSeek(Kotonoha_DemuxStream *stream, Uint64 targetMs);
void KotonohaDemuxStreamSetWaiting(Kotonoha_DemuxStream *stream,
                                   bool waiting);
void KotonohaDemuxStreamFlushBuffers(Kotonoha_DemuxStream *stream,
                                     bool preserveKeyFrame);

#ifdef __cplusplus
}
#endif