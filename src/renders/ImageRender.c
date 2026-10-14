#include <Kotonoha/renders/ImageRender.h>
#include <Kotonoha/speciais/Upscaling.h>

static void prepareStraightAlphaForLinearFiltering(Uint8 *rgba, int width,
                                                   int height, int pitch) {
  if (rgba == NULL || width <= 0 || height <= 0 || width > INT_MAX / 4 ||
      pitch < width * 4) {
    return;
  }

  for (int y = 0; y < height; ++y) {
    for (int x = 0; x < width; ++x) {
      Uint8 *target = rgba + (size_t)y * pitch + (size_t)x * 4;
      if (target[3] != 0) {
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
          const int alpha = source[3];
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
        target[0] = nearest[0];
        target[1] = nearest[1];
        target[2] = nearest[2];
      }
    }
  }
}

// Função auxiliar para liberar recursos de FFmpeg e SDL
static void cleanupResources(AVFrame *frame, AVPacket *packet,
                             struct SwsContext *swsCtx) {
  if (frame)
    av_frame_free(&frame);
  if (packet)
    av_packet_unref(packet);
  if (swsCtx)
    sws_freeContext(swsCtx);
}

// Função corrigida para carregar, decodificar e converter o frame de imagem
// estática
static AVFrame *renderFrame(const char *path, int h, int w, Uint8 **buffer,
                            Kotonoha_IOMonitorOperation *ioOperation) {
  AVFormatContext *fmtCtx = NULL;
  AVCodecContext *codecCtx = NULL;
  struct SwsContext *swsCtx = NULL;
  AVPacket *packet = av_packet_alloc();
  int imageStreamIndex = -1;
  AVFrame *frame = NULL;
  AVFrame *pFrameRGB = NULL;

  if (!packet)
    return NULL;

  // Carregar o formato de imagem/vídeo e inicializar o codec
  if (!KotonohaFFmpegLoadMonitored(path, &fmtCtx, &codecCtx, AVMEDIA_TYPE_VIDEO,
                                   &imageStreamIndex, 0, NULL, ioOperation)) {
    if (!KotonohaIOMonitorIsIgnoredMissing(ioOperation)) {
      SDL_LogError(SDL_LOG_CATEGORY_ERROR, "Failed to load image: %s\n", path);
    }
    av_packet_free(&packet);
    return NULL;
  }

  bool frameDecoded = false;

  // Loop de leitura de pacotes robusto para imagens e vídeos estáticos
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

  // Se o loop principal esgotou sem decodificar (comum em alguns
  // decodificadores de imagem), envia EOF para drenar
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
    SDL_LogError(SDL_LOG_CATEGORY_ERROR,
                 "Failed to decode image frame from codec: %s\n", path);
    av_packet_free(&packet);
    avcodec_free_context(&codecCtx);
    avformat_close_input(&fmtCtx);
    return NULL;
  }

  // Definir altura e largura padrão se não fornecidos
  h = (h == -1) ? codecCtx->height : h;
  w = (w == -1) ? codecCtx->width : w;

  // Criar contexto de conversão de pixel para RGBA
  swsCtx = KotonohaFFmpegCreateSwsContext(codecCtx, codecCtx->pix_fmt, w, h);
  if (!swsCtx) {
    cleanupResources(frame, packet, NULL);
    avcodec_free_context(&codecCtx);
    avformat_close_input(&fmtCtx);
    av_packet_free(&packet);
    return NULL;
  }

  // Converter para RGBA utilizando o buffer fornecido
  pFrameRGB = KotonohaFFmpegSwsConvertFrame(swsCtx, frame, buffer, h, w);
  if (!pFrameRGB) {
    SDL_LogError(SDL_LOG_CATEGORY_ERROR, "Error converting frame to RGBA\n");
    cleanupResources(frame, packet, swsCtx);
    avcodec_free_context(&codecCtx);
    avformat_close_input(&fmtCtx);
    av_packet_free(&packet);
    return NULL;
  }

  pFrameRGB->width = w;
  pFrameRGB->height = h;
  prepareStraightAlphaForLinearFiltering(pFrameRGB->data[0], w, h,
                                         pFrameRGB->linesize[0]);

  // Limpeza de recursos intermediários (mantendo o pFrameRGB e o buffer
  // alocados)
  av_frame_free(&frame);
  if (swsCtx)
    sws_freeContext(swsCtx);
  av_packet_free(&packet);
  avcodec_free_context(&codecCtx);
  avformat_close_input(&fmtCtx);

  return pFrameRGB;
}

AVFrame *KotonohaImageDecodeFrame(const char *path, int h, int w,
                                  Uint8 **buffer,
                                  Kotonoha_IOMonitorOperation *ioOperation) {
  if (path == NULL || buffer == NULL || ioOperation == NULL) {
    SDL_LogError(SDL_LOG_CATEGORY_ERROR,
                 "Invalid arguments for image frame decoding");
    return NULL;
  }
  return renderFrame(path, h, w, buffer, ioOperation);
}

SDL_Texture *KotonohaImageCreateTextureFromFrame(SDL_Renderer *render,
                                                 AVFrame *frame) {
  if (render == NULL || frame == NULL) {
    SDL_LogError(SDL_LOG_CATEGORY_RENDER,
                 "Invalid arguments for image texture creation");
    return NULL;
  }

  SDL_Texture *texture =
      SDL_CreateTexture(render, SDL_PIXELFORMAT_RGBA32,
                        SDL_TEXTUREACCESS_STATIC, frame->width, frame->height);

  if (texture == NULL) {
    SDL_LogError(SDL_LOG_CATEGORY_ERROR, "Failed to create texture: %s",
                 SDL_GetError());
    return NULL;
  }

  if (!SDL_UpdateTexture(texture, NULL, frame->data[0], frame->linesize[0])) {
    SDL_LogError(SDL_LOG_CATEGORY_RENDER, "Failed to update image texture: %s",
                 SDL_GetError());
    SDL_DestroyTexture(texture);
    return NULL;
  }

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
  Kotonoha_IOMonitorOperation ioOperation;
  KotonohaIOMonitorOperationInit(&ioOperation, NULL);
  Uint8 *buffer = NULL;
  AVFrame *pFrameRGB = renderFrame(path, h, w, &buffer, &ioOperation);
  if (!pFrameRGB) {
    if (!KotonohaIOMonitorIsIgnoredMissing(&ioOperation)) {
      SDL_LogError(SDL_LOG_CATEGORY_ERROR, "Failed to render image frame\n");
    }
    KotonohaIOMonitorOperationReset(&ioOperation);
    return NULL;
  }

  SDL_Texture *texture = KotonohaImageCreateTextureFromFrame(render, pFrameRGB);

  KotonohaImageReleaseFrame(&pFrameRGB, &buffer);
  KotonohaIOMonitorOperationReset(&ioOperation);
  return texture;
}

SDL_Surface *KotonohaImageCreateSurface(const char *path, int h, int w) {
  Uint8 *buffer = NULL;
  Kotonoha_IOMonitorOperation ioOperation;
  KotonohaIOMonitorOperationInit(&ioOperation, NULL);
  AVFrame *pFrameRGB = renderFrame(path, h, w, &buffer, &ioOperation);
  if (!pFrameRGB) {
    if (!KotonohaIOMonitorIsIgnoredMissing(&ioOperation)) {
      SDL_LogError(SDL_LOG_CATEGORY_ERROR, "Failed to render image frame\n");
    }
    KotonohaIOMonitorOperationReset(&ioOperation);
    return NULL;
  }

  SDL_Surface *surface = SDL_CreateSurface(pFrameRGB->width, pFrameRGB->height,
                                           SDL_PIXELFORMAT_RGBA32);

  if (!surface) {
    SDL_LogError(SDL_LOG_CATEGORY_ERROR, "Failed to create surface: %s\n",
                 SDL_GetError());
  } else if (SDL_LockSurface(surface)) {
    SDL_memcpy(surface->pixels, pFrameRGB->data[0],
               pFrameRGB->linesize[0] * pFrameRGB->height);
    SDL_UnlockSurface(surface);
  } else {
    SDL_LogError(SDL_LOG_CATEGORY_RENDER, "Failed to lock image surface: %s",
                 SDL_GetError());
    SDL_DestroySurface(surface);
    surface = NULL;
  }

  KotonohaImageReleaseFrame(&pFrameRGB, &buffer);
  KotonohaIOMonitorOperationReset(&ioOperation);
  return surface;
}