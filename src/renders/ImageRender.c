#include <Kotonoha/utils/Log.h>
#include <Kotonoha/renders/ImageRender.h>
#include <Kotonoha/specials/Upscaling.h>
#include <Kotonoha/utils/FFmpegSDL.h>

static void prepareStraightAlphaForLinearFiltering(Uint8 *rgba, int width,
                                                   int height, int pitch,
                                                   int alphaIndex) {
  if (rgba == NULL || width <= 0 || height <= 0 || width > INT_MAX / 4 ||
      pitch < width * 4 || alphaIndex < 0 || alphaIndex > 3) {
    return;
  }

  for (int y = 0; y < height; ++y) {
    for (int x = 0; x < width; ++x) {
      Uint8 *target = rgba + (size_t)y * pitch + (size_t)x * 4;
      if (target[alphaIndex] != 0) {
        continue;
      }

      const Uint8 *nearest = NULL;
      int nearestDistance = 3;
      int nearestAlpha = -1;
      for (int dy = -1; dy <= 1; ++dy) {
        const int ny = y + dy;
        if (ny < 0 || ny >= height) {
          continue;
        }
        for (int dx = -1; dx <= 1; ++dx) {
          const int nx = x + dx;
          if ((dx == 0 && dy == 0) || nx < 0 || nx >= width) {
            continue;
          }

          const Uint8 *source = rgba + (size_t)ny * pitch + (size_t)nx * 4;
          const int alpha = source[alphaIndex];
          const int distance = dx * dx + dy * dy;
          if (alpha != 0 &&
              (distance < nearestDistance ||
               (distance == nearestDistance && alpha > nearestAlpha))) {
            nearest = source;
            nearestDistance = distance;
            nearestAlpha = alpha;
          }
        }
      }

      if (nearest != NULL) {
        for (int channel = 0; channel < 4; ++channel) {
          if (channel != alphaIndex) {
            target[channel] = nearest[channel];
          }
        }
      }
    }
  }
}

static AVFrame *decodeFrame(const char *path, bool ignoreMissingAssets) {
  AVFormatContext *fmtCtx = NULL;
  AVCodecContext *codecCtx = NULL;
  AVPacket *packet = av_packet_alloc();
  int imageStreamIndex = -1;
  AVFrame *frame = NULL;

  if (packet == NULL) {
    KLOG_ERROR(IMAGE, "Failed to allocate image packet: %s", SDL_GetError());
    return NULL;
  }

  if (!KotonohaFFmpegLoad(path, &fmtCtx, &codecCtx, AVMEDIA_TYPE_VIDEO,
                          &imageStreamIndex, 0, NULL)) {
    if (!ignoreMissingAssets) {
      KLOG_ERROR(IMAGE, "Failed to load image: %s", path);
    }
    avcodec_free_context(&codecCtx);
    avformat_close_input(&fmtCtx);
    av_packet_free(&packet);
    return NULL;
  }

  bool frameDecoded = false;

  while (KotonohaFFmpegReadFrame(fmtCtx, packet) >= 0) {
    if (packet->stream_index == imageStreamIndex) {
      int ret = avcodec_send_packet(codecCtx, packet);
      if (ret < 0 && ret != AVERROR(EAGAIN)) {
        av_packet_unref(packet);
        break;
      }

      frame = av_frame_alloc();
      if (!frame) {
        av_packet_unref(packet);
        break;
      }

      ret = avcodec_receive_frame(codecCtx, frame);
      if (ret == 0) {
        frameDecoded = true;
        av_packet_unref(packet);
        break;
      } else {
        av_frame_free(&frame);
      }
    }
    av_packet_unref(packet);
  }

  if (!frameDecoded) {
    avcodec_send_packet(codecCtx, NULL);
    frame = av_frame_alloc();
    if (frame && avcodec_receive_frame(codecCtx, frame) == 0) {
      frameDecoded = true;
    } else {
      av_frame_free(&frame);
    }
  }

  if (!frameDecoded || !frame) {
    KLOG_ERROR(IMAGE, "Failed to decode image frame from codec: %s", path);
    av_packet_free(&packet);
    avcodec_free_context(&codecCtx);
    avformat_close_input(&fmtCtx);
    return NULL;
  }

  av_packet_free(&packet);
  avcodec_free_context(&codecCtx);
  avformat_close_input(&fmtCtx);

  return frame;
}

static AVFrame *convertFrameToRgba(AVFrame *frame, int h, int w,
                                   Uint8 **buffer) {
  if (frame == NULL || buffer == NULL || frame->width <= 0 ||
      frame->height <= 0 || h <= 0 || w <= 0) {
    KLOG_ERROR(IMAGE, "Invalid arguments for image RGBA conversion");
    return NULL;
  }

  *buffer = NULL;
  struct SwsContext *swsCtx =
      sws_getContext(frame->width, frame->height,
                     (enum AVPixelFormat)frame->format, w, h, AV_PIX_FMT_RGBA,
                     SWS_LANCZOS, NULL, NULL, NULL);
  if (swsCtx == NULL) {
    KLOG_ERROR(IMAGE, "Failed to create image frame converter");
    return NULL;
  }

  AVFrame *rgbaFrame =
      KotonohaFFmpegSwsConvertFrame(swsCtx, frame, buffer, h, w);
  sws_freeContext(swsCtx);
  if (rgbaFrame != NULL) {
    prepareStraightAlphaForLinearFiltering(rgbaFrame->data[0], w, h,
                                           rgbaFrame->linesize[0], 3);
  }
  return rgbaFrame;
}

AVFrame *KotonohaImageDecodeFrame(const char *path, int h, int w,
                                  Uint8 **buffer, bool ignoreMissingAssets) {
  if (path == NULL || buffer == NULL) {
    KLOG_ERROR(IMAGE, "Invalid arguments for image frame decoding");
    return NULL;
  }
  AVFrame *frame = decodeFrame(path, ignoreMissingAssets);
  if (frame == NULL) {
    return NULL;
  }

  h = h == -1 ? frame->height : h;
  w = w == -1 ? frame->width : w;
  AVFrame *rgbaFrame = convertFrameToRgba(frame, h, w, buffer);
  av_frame_free(&frame);
  return rgbaFrame;
}

AVFrame *KotonohaImageDecodeNativeFrame(const char *path,
                                        bool ignoreMissingAssets) {
  if (path == NULL) {
    KLOG_ERROR(IMAGE, "Invalid path for native image frame decoding");
    return NULL;
  }
  return decodeFrame(path, ignoreMissingAssets);
}

SDL_Texture *KotonohaImageCreateTextureFromFrame(SDL_Renderer *render,
                                                 AVFrame *frame) {
  if (render == NULL || frame == NULL || frame->width <= 0 ||
      frame->height <= 0 || frame->data[0] == NULL) {
    KLOG_ERROR(IMAGE, "Invalid arguments for image texture creation");
    return NULL;
  }

  bool supportsDirectUpload = false;
  const Uint32 nativeFormat =
      KotonohaSDLFormatForAVFrame(frame, &supportsDirectUpload);
  SDL_Texture *texture = NULL;
  if (supportsDirectUpload) {
    texture = SDL_CreateTexture(render, nativeFormat, SDL_TEXTUREACCESS_STREAMING,
                                frame->width, frame->height);
    if (texture != NULL &&
        SDL_SetTextureBlendMode(
            texture, KotonohaAVPixelFormatHasAlpha(
                         (enum AVPixelFormat)frame->format)
                         ? SDL_BLENDMODE_BLEND
                         : SDL_BLENDMODE_NONE)) {
      if (KotonohaAVPixelFormatHasAlpha(
              (enum AVPixelFormat)frame->format)) {
        const int alphaIndex =
            frame->format == AV_PIX_FMT_ARGB ||
                    frame->format == AV_PIX_FMT_ABGR
                ? 0
                : 3;
        prepareStraightAlphaForLinearFiltering(
            frame->data[0], frame->width, frame->height, frame->linesize[0],
            alphaIndex);
      }
      if (KotonohaSDLUpdateTextureFromAVFrame(texture, frame)) {
        return texture;
      }
      KLOG_WARN(IMAGE, "Native SDL image upload failed; falling back to SWS: %s",
                  SDL_GetError());
    } else if (texture == NULL) {
      KLOG_WARN(IMAGE, "Could not create native SDL image texture; falling back "
                  "to SWS: %s",
                  SDL_GetError());
    } else {
      KLOG_WARN(IMAGE, "Could not configure native SDL image texture; falling "
                  "back to SWS: %s",
                  SDL_GetError());
    }
    if (texture != NULL) {
      SDL_DestroyTexture(texture);
    }
    texture = NULL;
  }

  Uint8 *buffer = NULL;
  AVFrame *rgbaFrame =
      convertFrameToRgba(frame, frame->height, frame->width, &buffer);
  if (rgbaFrame == NULL) {
    return NULL;
  }

  texture = SDL_CreateTexture(render, SDL_PIXELFORMAT_RGBA32,
                              SDL_TEXTUREACCESS_STREAMING, frame->width,
                              frame->height);
  if (texture == NULL) {
    KLOG_ERROR(IMAGE, "Failed to create SWS image texture: %s", SDL_GetError());
  } else if (!SDL_SetTextureBlendMode(
                 texture, KotonohaAVPixelFormatHasAlpha(
                              (enum AVPixelFormat)frame->format)
                              ? SDL_BLENDMODE_BLEND
                              : SDL_BLENDMODE_NONE) ||
             !SDL_UpdateTexture(texture, NULL, rgbaFrame->data[0],
                                rgbaFrame->linesize[0])) {
    KLOG_ERROR(IMAGE, "Failed to upload SWS image texture: %s", SDL_GetError());
    SDL_DestroyTexture(texture);
    texture = NULL;
  }

  KotonohaImageReleaseFrame(&rgbaFrame, &buffer);
  return texture;
}

void KotonohaImageReleaseFrame(AVFrame **frame, Uint8 **buffer) {
  if (buffer != NULL && *buffer != NULL) {
    av_free(*buffer);
    *buffer = NULL;
  }
  if (frame != NULL && *frame != NULL) {
    av_frame_free(frame);
  }
}

SDL_Texture *KotonohaImageCreateTexture(SDL_Renderer *render, const char *path,
                                        int h, int w) {
  if (render == NULL || path == NULL) {
    KLOG_ERROR(IMAGE, "Invalid arguments for image texture creation");
    return NULL;
  }

  AVFrame *frame = decodeFrame(path, false);
  if (frame == NULL) {
    KLOG_ERROR(IMAGE, "Failed to render image frame");
    return NULL;
  }

  h = h == -1 ? frame->height : h;
  w = w == -1 ? frame->width : w;
  if (h <= 0 || w <= 0) {
    KLOG_ERROR(IMAGE, "Invalid image texture dimensions: %dx%d", w, h);
    av_frame_free(&frame);
    return NULL;
  }

  SDL_Texture *texture = NULL;
  if (h == frame->height && w == frame->width) {
    texture = KotonohaImageCreateTextureFromFrame(render, frame);
  } else {
    Uint8 *buffer = NULL;
    AVFrame *rgbaFrame = convertFrameToRgba(frame, h, w, &buffer);
    if (rgbaFrame != NULL) {
      texture = KotonohaImageCreateTextureFromFrame(render, rgbaFrame);
    }
    KotonohaImageReleaseFrame(&rgbaFrame, &buffer);
  }

  av_frame_free(&frame);
  return texture;
}

static bool copyFrameToSurface(SDL_Surface *surface, AVFrame *frame) {
  if (surface == NULL || frame == NULL || frame->width <= 0 ||
      frame->height <= 0 || frame->data[0] == NULL) {
    return false;
  }

  const int bytesPerPixel = frame->format == AV_PIX_FMT_RGB24 ||
                                    frame->format == AV_PIX_FMT_BGR24
                                ? 3
                                : 4;
  if (frame->width > INT_MAX / bytesPerPixel ||
      frame->linesize[0] < frame->width * bytesPerPixel ||
      surface->pitch < frame->width * bytesPerPixel) {
    return false;
  }

  if (!SDL_LockSurface(surface)) {
    KLOG_ERROR(IMAGE, "Failed to lock image surface: %s",
                 SDL_GetError());
    return false;
  }

  for (int y = 0; y < frame->height; ++y) {
    SDL_memcpy((Uint8 *)surface->pixels + (size_t)y * surface->pitch,
               frame->data[0] + (size_t)y * frame->linesize[0],
               (size_t)frame->width * bytesPerPixel);
  }
  SDL_UnlockSurface(surface);
  return true;
}

SDL_Surface *KotonohaImageCreateSurface(const char *path, int h, int w) {
  if (path == NULL) {
    KLOG_ERROR(IMAGE, "Invalid path for image surface creation");
    return NULL;
  }

  AVFrame *frame = decodeFrame(path, false);
  if (frame == NULL) {
    KLOG_ERROR(IMAGE, "Failed to render image frame");
    return NULL;
  }

  h = h == -1 ? frame->height : h;
  w = w == -1 ? frame->width : w;
  if (h <= 0 || w <= 0) {
    KLOG_ERROR(IMAGE, "Invalid image surface dimensions: %dx%d", w, h);
    av_frame_free(&frame);
    return NULL;
  }

  SDL_Surface *surface = NULL;
  bool directSurfaceSupported =
      h == frame->height && w == frame->width &&
      KotonohaAVPixelFormatSupportsDirectSurface(
          (enum AVPixelFormat)frame->format);
  if (directSurfaceSupported) {
    bool supportsDirectUpload = false;
    const Uint32 format =
        KotonohaSDLFormatForAVFrame(frame, &supportsDirectUpload);
    if (supportsDirectUpload) {
      surface = SDL_CreateSurface(frame->width, frame->height, format);
      if (surface != NULL && copyFrameToSurface(surface, frame)) {
        av_frame_free(&frame);
        return surface;
      }
      if (surface != NULL) {
        SDL_DestroySurface(surface);
        surface = NULL;
      }
      KLOG_WARN(IMAGE, "Direct SDL surface creation failed; falling back to SWS");
    }
  }

  Uint8 *buffer = NULL;
  AVFrame *rgbaFrame = convertFrameToRgba(frame, h, w, &buffer);
  av_frame_free(&frame);
  if (rgbaFrame == NULL) {
    return NULL;
  }

  surface = SDL_CreateSurface(w, h, SDL_PIXELFORMAT_RGBA32);
  if (surface == NULL) {
    KLOG_ERROR(IMAGE, "Failed to create SWS image surface: %s", SDL_GetError());
  } else if (!copyFrameToSurface(surface, rgbaFrame)) {
    SDL_DestroySurface(surface);
    surface = NULL;
  }

  KotonohaImageReleaseFrame(&rgbaFrame, &buffer);
  return surface;
}