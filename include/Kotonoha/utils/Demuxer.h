#pragma once

#include <Kotonoha/parsers/Config.h>
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
  Kotonoha_FFmpegInputContext *inputContext;
  Kotonoha_FFmpegInput directInput;
  bool hasDirectInput;
  bool directSource;
  bool resolverBackedInput;
  int videoStreamIdx;
  int audioStreamIdx;
  AVRational videoTimeBase;
  AVRational audioTimeBase;
  enum AVMediaType requestedType;

  Kotonoha_PacketQueue videoQueue;
  Kotonoha_PacketQueue audioQueue;

  // Total media length in ms (0 if unknown); cached when the file is opened.
  SDL_AtomicInt totalDurationMs;
  SDL_AtomicInt seekRequested;
  Uint64 seekTargetMs;
  SDL_Mutex *controlMutex;
  SDL_AtomicInt readingPacket;

  SDL_AtomicInt waitingForPackets;
  SDL_AtomicInt operationActive;
  SDL_AtomicInt operationStartedAt;
  SDL_AtomicInt cancelled;
  // After the cache is trimmed the reader re-seeks and drops packets that are
  // already queued (pts <= skip*UntilMs). Used by the demux thread only.
  bool skipVideoActive;
  bool skipAudioActive;
  Uint64 skipVideoUntilMs;
  Uint64 skipAudioUntilMs;
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
  // Not owned; must outlive the demuxer. Buffer limits are read from it.
  const Kotonoha_Config *config;
} Kotonoha_Demuxer;

void KotonohaPacketQueueInit(Kotonoha_PacketQueue *q);
void KotonohaPacketQueueDestroy(Kotonoha_PacketQueue *q);
void KotonohaPacketQueueFlush(Kotonoha_PacketQueue *q, bool preserveKeyFrame);
bool KotonohaPacketQueueHasPackets(Kotonoha_PacketQueue *q);
bool KotonohaPacketQueueIsEof(Kotonoha_PacketQueue *q);
void KotonohaPacketQueueGetBufferStats(Kotonoha_PacketQueue *q,
                                       Uint64 *cachedMs, bool *eof);
// Drops queued packets beyond maxSpanMs (keeps at least one). Returns true when
// something was discarded and stores the pts of the last kept packet.
bool KotonohaPacketQueueTrim(Kotonoha_PacketQueue *q, Uint64 maxSpanMs,
                             Uint64 *lastKeptPtsMs);
bool KotonohaPacketQueueAtCacheLimit(Kotonoha_PacketQueue *q, Uint64 maxSpanMs);
bool KotonohaPacketQueuePush(Kotonoha_PacketQueue *q, AVPacket *pkt,
                             AVStream *stream, Uint64 maxSpanMs);

AVPacket *KotonohaPacketQueuePop(Kotonoha_PacketQueue *q, Uint64 *outPtsMs);

Kotonoha_Demuxer *KotonohaDemuxerCreate(bool retryMissingAssets,
                                        const Kotonoha_Config *config);
void KotonohaDemuxerStart(Kotonoha_Demuxer *demuxer);
void KotonohaDemuxerSuspend(Kotonoha_Demuxer *demuxer);
void KotonohaDemuxerStop(Kotonoha_Demuxer *demuxer);
bool KotonohaDemuxerIsStarted(Kotonoha_Demuxer *demuxer);
bool KotonohaDemuxerIsLoading(Kotonoha_Demuxer *demuxer);
void KotonohaDemuxerSetExternalWaiter(Kotonoha_Demuxer *demuxer, bool waiting);
void KotonohaDemuxerDestroy(Kotonoha_Demuxer **demuxer);

Kotonoha_DemuxStream *KotonohaDemuxerAddStream(Kotonoha_Demuxer *demuxer,
                                               const char *filename,
                                               void *owner,
                                               enum AVMediaType type);

/* Direct custom-input variant used by archive/memory-backed consumers. The
 * input opaque ownership transfers to the stream. Resolver-backed URI inputs
 * should normally use KotonohaDemuxerAddStream so they can be reopened. */
Kotonoha_DemuxStream *KotonohaDemuxerAddStreamInput(
    Kotonoha_Demuxer *demuxer, const Kotonoha_FFmpegInput *input, void *owner,
    enum AVMediaType type);
bool KotonohaDemuxerActivateStream(Kotonoha_Demuxer *demuxer,
                                   Kotonoha_DemuxStream *stream);

void KotonohaDemuxerRemoveStream(Kotonoha_Demuxer *demuxer,
                                 Kotonoha_DemuxStream **stream);

void KotonohaDemuxStreamSeek(Kotonoha_DemuxStream *stream, Uint64 targetMs);
void KotonohaDemuxStreamSetWaiting(Kotonoha_DemuxStream *stream, bool waiting);
void KotonohaDemuxStreamFlushBuffers(Kotonoha_DemuxStream *stream,
                                     bool preserveKeyFrame);

#ifdef __cplusplus
}
#endif