#pragma once
#include <Kotonoha/utils/FFmpeg.h>
#include <Kotonoha/utils/Time.h>
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/frame.h>
#include <libswresample/swresample.h>
#include <SDL3/SDL.h>

struct Kotonoha_audioDecode
{
	AVFormatContext *formatCtx;
	AVCodecContext *codecCtx;
	SwrContext *swrCtx;

	int audioStreamIndex;
	SDL_AudioSpec specification;

	Uint64 start, end, audioTime, lastTime;
	bool inLoop;
	int executions;
	struct Kotonoha_time **tm;
	void *dataGeneric;
};

void genWhiteNoise(void *data, Uint8 **target, size_t *size);

struct Kotonoha_audioDecode *Kotonoha_AudioInit(const char *path, SDL_AudioSpec specification);

void Kotonoha_AudioSeek(struct Kotonoha_audioDecode *ctx, Sint64 time);

int Kotonoha_AudioRender(void *data, Uint8 **target, int *size);

void Kotonoha_AudioFree(void *data);