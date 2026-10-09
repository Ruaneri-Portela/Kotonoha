#pragma once
#include <Kotonoha/utils/FFmpeg.h>
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/imgutils.h>
#include <libswscale/swscale.h>
#include <SDL3/SDL.h>

struct Kotonoha_imageLoad {
	SDL_Thread* thread;
	AVFrame* frame;
	Uint8* buffer;
	const char* path;
	Kotonoha_IOMonitor* owner;
	Kotonoha_IOMonitorOperation ioOperation;
	SDL_AtomicInt state;
	bool failed;
	bool missingIgnored;
};

struct Kotonoha_Picture {
	char* path;
	SDL_Texture* texture;
	struct Kotonoha_imageLoad load;
	Uint64 startTime, endTime, lastTime;
	Uint8 id;
	bool canRender;
	Uint64 baseGeneration;
};

SDL_Texture* Kotonoha_imageCreateTexture(SDL_Renderer* render, const char* path,
	int h, int w);
AVFrame* Kotonoha_imageDecodeFrame(const char* path, int h, int w,
	Uint8** buffer, Kotonoha_IOMonitorOperation* ioOperation);
SDL_Texture* Kotonoha_imageCreateTextureFromFrame(SDL_Renderer* render,
	AVFrame* frame);
void Kotonoha_imageReleaseFrame(AVFrame** frame, Uint8** buffer);

SDL_Surface* Kotonoha_imageCreateSurface(const char* path, int h, int w);