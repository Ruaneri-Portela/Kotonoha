#pragma once

#include <SDL3/SDL.h>
#include <libavutil/frame.h>
#include <libavutil/pixdesc.h>
#include <libavutil/pixfmt.h>
#include <limits.h>

static inline Uint32 KotonohaSDLFormatForAVFrame(
    const AVFrame *frame, bool *supportsDirectUpload) {
  if (supportsDirectUpload == NULL) {
    return SDL_PIXELFORMAT_RGBA32;
  }

  if (frame == NULL) {
    *supportsDirectUpload = false;
    return SDL_PIXELFORMAT_RGBA32;
  }

  const enum AVPixelFormat pixelFormat = (enum AVPixelFormat)frame->format;
  if ((pixelFormat == AV_PIX_FMT_NV12 || pixelFormat == AV_PIX_FMT_NV21 ||
       pixelFormat == AV_PIX_FMT_YUV420P ||
       pixelFormat == AV_PIX_FMT_YUVJ420P) &&
      (pixelFormat == AV_PIX_FMT_YUVJ420P ||
       frame->color_range == AVCOL_RANGE_JPEG)) {
    *supportsDirectUpload = false;
    return SDL_PIXELFORMAT_RGBA32;
  }

  *supportsDirectUpload = true;
  switch (pixelFormat) {
  case AV_PIX_FMT_NV12:
    return SDL_PIXELFORMAT_NV12;
  case AV_PIX_FMT_NV21:
    return SDL_PIXELFORMAT_NV21;
  case AV_PIX_FMT_YUV420P:
    return SDL_PIXELFORMAT_IYUV;
  case AV_PIX_FMT_RGBA:
    return SDL_PIXELFORMAT_RGBA32;
  case AV_PIX_FMT_BGRA:
    return SDL_PIXELFORMAT_BGRA32;
  case AV_PIX_FMT_ARGB:
    return SDL_PIXELFORMAT_ARGB32;
  case AV_PIX_FMT_ABGR:
    return SDL_PIXELFORMAT_ABGR32;
  case AV_PIX_FMT_RGB24:
    return SDL_PIXELFORMAT_RGB24;
  case AV_PIX_FMT_BGR24:
    return SDL_PIXELFORMAT_BGR24;
  default:
    *supportsDirectUpload = false;
    return SDL_PIXELFORMAT_RGBA32;
  }
}

static inline bool KotonohaAVPixelFormatHasAlpha(enum AVPixelFormat format) {
  const AVPixFmtDescriptor *descriptor = av_pix_fmt_desc_get(format);
  return descriptor != NULL && (descriptor->flags & AV_PIX_FMT_FLAG_ALPHA) != 0;
}

static inline bool KotonohaSDLUpdateTextureFromAVFrame(SDL_Texture *texture,
                                                       AVFrame *frame) {
  if (texture == NULL || frame == NULL) {
    return false;
  }

  switch ((enum AVPixelFormat)frame->format) {
  case AV_PIX_FMT_NV12:
  case AV_PIX_FMT_NV21:
    return frame->data[0] != NULL && frame->data[1] != NULL &&
           frame->width > 0 && frame->height > 0 &&
           frame->linesize[0] >= frame->width &&
           frame->linesize[1] >= frame->width &&
           SDL_UpdateNVTexture(texture, NULL, frame->data[0],
                               frame->linesize[0], frame->data[1],
                               frame->linesize[1]);
  case AV_PIX_FMT_YUV420P:
    return frame->data[0] != NULL && frame->data[1] != NULL &&
           frame->data[2] != NULL && frame->width > 0 &&
           frame->height > 0 && frame->linesize[0] >= frame->width &&
           frame->linesize[1] >= frame->width / 2 + frame->width % 2 &&
           frame->linesize[2] >= frame->width / 2 + frame->width % 2 &&
           SDL_UpdateYUVTexture(texture, NULL, frame->data[0],
                                frame->linesize[0], frame->data[1],
                                frame->linesize[1], frame->data[2],
                                frame->linesize[2]);
  case AV_PIX_FMT_RGBA:
  case AV_PIX_FMT_BGRA:
  case AV_PIX_FMT_ARGB:
  case AV_PIX_FMT_ABGR:
  case AV_PIX_FMT_RGB24:
  case AV_PIX_FMT_BGR24:
    {
      const int bytesPerPixel = frame->format == AV_PIX_FMT_RGB24 ||
                                        frame->format == AV_PIX_FMT_BGR24
                                    ? 3
                                    : 4;
      return frame->data[0] != NULL && frame->width > 0 &&
             frame->width <= INT_MAX / bytesPerPixel && frame->height > 0 &&
             frame->linesize[0] >= frame->width * bytesPerPixel &&
             SDL_UpdateTexture(texture, NULL, frame->data[0],
                               frame->linesize[0]);
    }
  default:
    return false;
  }
}

static inline bool KotonohaAVPixelFormatSupportsDirectSurface(
    enum AVPixelFormat format) {
  return format == AV_PIX_FMT_RGBA || format == AV_PIX_FMT_BGRA ||
         format == AV_PIX_FMT_ARGB || format == AV_PIX_FMT_ABGR ||
         format == AV_PIX_FMT_RGB24 || format == AV_PIX_FMT_BGR24;
}
