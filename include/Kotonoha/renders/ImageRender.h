#pragma once
#include <Kotonoha/utils/FFmpeg.h>
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/imgutils.h>
#include <libswscale/swscale.h>
#include <SDL3/SDL.h>

struct Kotonoha_Picture
{
	char *path;
	SDL_Texture *texture;
	Uint64 startTime, endTime, lastTime;
	Uint8 id;
	bool canRender;
};

SDL_Texture *Kotonoha_imageCreateTexture(SDL_Renderer *render, const char *path, int h, int w);

SDL_Surface *Kotonoha_imageCreateSurface(const char *path, int h, int w);