#pragma once

#include <Kotonoha/utils/Demuxer.h>
#include <Kotonoha/utils/FFmpeg.h>
#include <Kotonoha/utils/Time.h>
#include <SDL3/SDL.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct Kotonoha_SchoolDaysAudioRatePlan {
  int sourceSampleRate;
  int processingSampleRate;
  double requestedRate;
  double softwareFactor;
  int windowFrames;
  int overlapFrames;
  int inputFrames;
  int skipFrames;
  int prefixFrames;
  double residualRate;
  double hardwareFrequency;
  double hardwareRatio;
} Kotonoha_SchoolDaysAudioRatePlan;

/*
 * Keep the legacy lowercase tag/typedef used by the validated School Days
 * runtime while exposing Ruaneri's modern typedef as an alias to the same
 * object.  There is only one decoder implementation.
 */
typedef struct Kotonoha_audioDecode {
  AVFormatContext *formatCtx;
  AVCodecContext *codecCtx;
  SwrContext *swrCtx;

  Kotonoha_Demuxer *demuxer;
  Kotonoha_DemuxStream *stream;

  /* Non-null only for the legacy standalone C API compatibility path. */
  Kotonoha_Demuxer *ownedDemuxer;

  int audioStreamIndex;
  AVRational audioTimeBase;
  SDL_AudioSpec specification;

  Uint64 start;
  Uint64 end;
  Uint64 audioTime;
  Uint64 lastTime;
  Uint64 targetTime;
  Uint64 lastTargetTime;
  float cacheDurationSeconds;
  SDL_Mutex *stateMutex;

  bool inLoop;
  SDL_AtomicInt enabled;
  int executions;
  Kotonoha_time **tm;
  void *dataGeneric;
  struct Kotonoha_audioDecode *loopMedia;
  bool playingLoop;
  bool cannotSeek;
  bool firstSeek;
  bool hasTargetTime;
  bool decoderFlushSent;
  bool decoderEof;
  volatile bool oneShotDone;

  /* School Days speed controls are applied per decoder so the global Sound
   * mixer (and UI audio outside a Gameplay Audio instance) stays untouched. */
  int schoolDaysPlaybackRateIndex;
  double schoolDaysPlaybackRate;
  SDL_AudioStream *schoolDaysRateStream;
  float *schoolDaysOlaBuffer;
  size_t schoolDaysOlaFrames;
  size_t schoolDaysOlaCapacityFrames;
} Kotonoha_audioDecode;

typedef Kotonoha_audioDecode Kotonoha_AudioDecode;

void genWhiteNoise(void *data, Uint8 **target, size_t *size);

/* Pure helpers shared by the runtime and focused fidelity tests. */
int KotonohaSchoolDaysAudioRateIndex(double requestedRate);
bool KotonohaSchoolDaysAudioRateNeedsSourceResync(int oldIndex, int newIndex);
bool KotonohaSchoolDaysAudioRatePlanInit(
    int sourceSampleRate, int processingSampleRate, double requestedRate,
    Kotonoha_SchoolDaysAudioRatePlan *plan);
bool KotonohaSchoolDaysAudioOlaBlock(
    const float *source, int sourceFrames, int channels,
    const Kotonoha_SchoolDaysAudioRatePlan *plan, float *output,
    int outputFrames);

/* Shared-backend API used by Gameplay. */
Kotonoha_AudioDecode *KotonohaAudioInit(const char *path,
                                        SDL_AudioSpec specification,
                                        Kotonoha_Demuxer *demuxer);
void KotonohaAudioSeek(Kotonoha_AudioDecode *ctx, Uint64 time);
bool KotonohaAudioSetSchoolDaysPlaybackRate(Kotonoha_AudioDecode *ctx,
                                            double requestedRate);
int KotonohaAudioRender(void *data, Uint8 **target, int *size);
void KotonohaAudioFree(void *data);

/* G2.7 compatibility surface. These wrappers use the same demux/cache decoder
 * with a private backend when no Gameplay-owned backend exists. */
Kotonoha_audioDecode *Kotonoha_AudioInit(const char *path,
                                         SDL_AudioSpec specification);
void Kotonoha_AudioSeek(Kotonoha_audioDecode *ctx, Uint64 time);
int Kotonoha_AudioRender(void *data, Uint8 **target, int *size);
void Kotonoha_AudioFree(void *data);

#ifdef __cplusplus
}
#endif
