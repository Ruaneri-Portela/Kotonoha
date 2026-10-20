#pragma once
#include <Kotonoha/utils/FFmpeg.h>
#include <SDL3/SDL.h>
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/imgutils.h>
#include <libswscale/swscale.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct Kotonoha_ImageLoad {
  SDL_Thread *thread;
  AVFrame *frame;
  Uint8 *buffer;
  const char *path;
  SDL_AtomicInt state;
  bool failed;
  bool missingIgnored;
} Kotonoha_ImageLoad;

typedef struct Kotonoha_Picture {
  char *path;
  SDL_Texture *texture;
  Kotonoha_ImageLoad load;
  Uint64 startTime, endTime, lastTime;
  Uint8 id;
  bool canRender;
  Uint64 baseGeneration;
} Kotonoha_Picture;

SDL_Texture *KotonohaImageCreateTexture(SDL_Renderer *render, const char *path,
                                        int h, int w);
AVFrame *KotonohaImageDecodeFrame(const char *path, int h, int w,
                                  Uint8 **buffer, bool ignoreMissingAssets);
AVFrame *KotonohaImageDecodeNativeFrame(const char *path,
                                        bool ignoreMissingAssets);
SDL_Texture *KotonohaImageCreateTextureFromFrame(SDL_Renderer *render,
                                                 AVFrame *frame);

void KotonohaImageReleaseFrame(AVFrame **frame, Uint8 **buffer);

SDL_Surface *KotonohaImageCreateSurface(const char *path, int h, int w);

#ifdef __cplusplus
}
#endif
