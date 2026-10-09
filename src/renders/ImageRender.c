#include <Kotonoha/renders/ImageRender.h>
#include <limits.h>

static void prepareStraightAlphaForLinearFiltering(
	Uint8* rgba, int width, int height, int pitch) {
	if (rgba == NULL || width <= 0 || height <= 0 ||
		width > INT_MAX / 4 || pitch < width * 4) {
		return;
	}

	for (int y = 0; y < height; ++y) {
		for (int x = 0; x < width; ++x) {
			Uint8* target = rgba + (size_t)y * pitch + (size_t)x * 4;
			if (target[3] != 0) {
				continue;
			}

			const Uint8* nearest = NULL;
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

					const Uint8* source =
						rgba + (size_t)ny * pitch + (size_t)nx * 4;
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
static void cleanupResources(AVFrame* frame, AVPacket* packet,
	struct SwsContext* swsCtx) {
	if (frame)
		av_frame_free(&frame);
	if (packet)
		av_packet_unref(packet);
	if (swsCtx)
		sws_freeContext(swsCtx);
}

static int readImageFrame(AVFormatContext* format,
	AVPacket* packet, Kotonoha_IOMonitorOperation* ioOperation) {
	(void)ioOperation;
	return Kotonoha_UtilsFFmpegReadFrame(format, packet);
}

// Função intermediária para carregar e converter o primeiro frame em RGBA
static AVFrame* renderFrame(const char* path, int h, int w, Uint8** buffer,
	Kotonoha_IOMonitorOperation* ioOperation) {
	AVFormatContext* fmtCtx = NULL;
	AVCodecContext* codecCtx = NULL;
	struct SwsContext* swsCtx = NULL;
	AVPacket packet = { 0 };
	int imageStreamIndex, response;

	// Carregar o formato de vídeo e inicializar o codec
	if (!Kotonoha_UtilsFFmpegLoadMonitored(path, &fmtCtx, &codecCtx, AVMEDIA_TYPE_VIDEO,
		&imageStreamIndex, 0, NULL, ioOperation)) {
		if (!Kotonoha_IOMonitorIsIgnoredMissing(ioOperation)) {
			SDL_LogError(SDL_LOG_CATEGORY_ERROR, "Failed to load image: %s\n", path);
		}
		return NULL;
	}

	// Ler pacotes de vídeo e decodificar o primeiro frame
	while ((response = readImageFrame(fmtCtx, &packet, ioOperation)) >= 0) {
		if (packet.stream_index == imageStreamIndex) {
			// Enviar o pacote para o decodificador
			response = avcodec_send_packet(codecCtx, &packet);
			if (response < 0) {
				SDL_LogError(SDL_LOG_CATEGORY_ERROR,
					"Error sending packet to codec: %s\n",
					av_err2str(response));
				av_packet_unref(&packet);
				continue;
			}

			// Alocar frame e receber o frame decodificado
			AVFrame* frame = av_frame_alloc();
			if (!frame) {
				SDL_LogError(SDL_LOG_CATEGORY_ERROR, "Error allocating frame\n");
				cleanupResources(frame, &packet, NULL);
				return NULL;
			}

			response = avcodec_receive_frame(codecCtx, frame);
			if (response == AVERROR(EAGAIN) || response == AVERROR_EOF) {
				cleanupResources(frame, &packet, NULL);
				continue;
			}
			else if (response < 0) {
				SDL_LogError(SDL_LOG_CATEGORY_ERROR, "Error receiving frame: %s\n",
					av_err2str(response));
				cleanupResources(frame, &packet, NULL);
				return NULL;
			}

			// Definir altura e largura padrão se não fornecidos
			h = (h == -1) ? codecCtx->height : h;
			w = (w == -1) ? codecCtx->width : w;

			// Criar contexto de conversão de pixel para RGBA
			swsCtx = Kotonoha_UtilsFFmpegCreateSwsContext(codecCtx, codecCtx->pix_fmt,
				w, h);
			if (!swsCtx) {
				cleanupResources(frame, &packet, swsCtx);
				return NULL;
			}

			// Alocar frame para dados em formato RGBA
			AVFrame* pFrameRGB =
				Kotonoha_UtilsFFmpegSwsConvertFrame(swsCtx, frame, buffer, h, w);
			if (!pFrameRGB) {
				SDL_LogError(SDL_LOG_CATEGORY_ERROR,
					"Error converting frame to RGBA\n");
				cleanupResources(frame, &packet, swsCtx);
				return NULL;
			}
			pFrameRGB->width = w;
			pFrameRGB->height = h;
			prepareStraightAlphaForLinearFiltering(
				pFrameRGB->data[0], w, h, pFrameRGB->linesize[0]);

			// Liberar recursos intermediários e retornar o frame convertido
			cleanupResources(frame, &packet, swsCtx);
			avcodec_free_context(&codecCtx);
			avformat_close_input(&fmtCtx);
			return pFrameRGB;
		}
		av_packet_unref(&packet);
	}

	// Liberar recursos do FFmpeg
	avcodec_free_context(&codecCtx);
	avformat_close_input(&fmtCtx);
	return NULL;
}

AVFrame* Kotonoha_imageDecodeFrame(const char* path, int h, int w,
	Uint8** buffer, Kotonoha_IOMonitorOperation* ioOperation) {
	if (path == NULL || buffer == NULL || ioOperation == NULL) {
		SDL_LogError(SDL_LOG_CATEGORY_ERROR,
			"Invalid arguments for image frame decoding");
		return NULL;
	}
	return renderFrame(path, h, w, buffer, ioOperation);
}

SDL_Texture* Kotonoha_imageCreateTextureFromFrame(SDL_Renderer* render,
	AVFrame* frame) {
	if (render == NULL || frame == NULL) {
		SDL_LogError(SDL_LOG_CATEGORY_RENDER,
			"Invalid arguments for image texture creation");
		return NULL;
	}

	SDL_Texture* texture = SDL_CreateTexture(render, SDL_PIXELFORMAT_RGBA32,
		SDL_TEXTUREACCESS_STATIC, frame->width, frame->height);
	if (texture == NULL) {
		SDL_LogError(SDL_LOG_CATEGORY_ERROR,
			"Failed to create texture: %s", SDL_GetError());
		return NULL;
	}
	if (!SDL_UpdateTexture(texture, NULL, frame->data[0], frame->linesize[0])) {
		SDL_LogError(SDL_LOG_CATEGORY_RENDER,
			"Failed to update image texture: %s", SDL_GetError());
		SDL_DestroyTexture(texture);
		return NULL;
	}
	return texture;
}

void Kotonoha_imageReleaseFrame(AVFrame** frame, Uint8** buffer) {
	if (buffer != NULL) {
		av_free(*buffer);
		*buffer = NULL;
	}
	if (frame != NULL) {
		av_frame_free(frame);
	}
}

// Função para criar textura SDL a partir de um frame RGBA
SDL_Texture* Kotonoha_imageCreateTexture(SDL_Renderer* render, const char* path,
	int h, int w) {
	Kotonoha_IOMonitorOperation ioOperation;
	Kotonoha_IOMonitorOperationInit(&ioOperation, NULL);
	Uint8* buffer = NULL;
	AVFrame* pFrameRGB = renderFrame(path, h, w, &buffer, &ioOperation);
	if (!pFrameRGB) {
		if (!Kotonoha_IOMonitorIsIgnoredMissing(&ioOperation)) {
			SDL_LogError(SDL_LOG_CATEGORY_ERROR, "Failed to render image frame\n");
		}
		Kotonoha_IOMonitorOperationReset(&ioOperation);
		return NULL;
	}

	SDL_Texture* texture =
		Kotonoha_imageCreateTextureFromFrame(render, pFrameRGB);

	// Liberar recursos
	Kotonoha_imageReleaseFrame(&pFrameRGB, &buffer);
	Kotonoha_IOMonitorOperationReset(&ioOperation);
	return texture;
}

// Função para criar surface SDL a partir de um frame RGBA
SDL_Surface* Kotonoha_imageCreateSurface(const char* path, int h, int w) {
	Uint8* buffer = NULL;
	Kotonoha_IOMonitorOperation ioOperation;
	Kotonoha_IOMonitorOperationInit(&ioOperation, NULL);
	AVFrame* pFrameRGB = renderFrame(path, h, w, &buffer, &ioOperation);
	if (!pFrameRGB) {
		if (!Kotonoha_IOMonitorIsIgnoredMissing(&ioOperation)) {
			SDL_LogError(SDL_LOG_CATEGORY_ERROR, "Failed to render image frame\n");
		}
		Kotonoha_IOMonitorOperationReset(&ioOperation);
		return NULL;
	}

	SDL_Surface* surface = SDL_CreateSurface(pFrameRGB->width, pFrameRGB->height,
		SDL_PIXELFORMAT_RGBA32);

	if (!surface) {
		SDL_LogError(SDL_LOG_CATEGORY_ERROR, "Failed to create surface: %s\n",
			SDL_GetError());
	}
	else if (SDL_LockSurface(surface)) {
		SDL_memcpy(surface->pixels, pFrameRGB->data[0],
			pFrameRGB->linesize[0] * pFrameRGB->height);
		SDL_UnlockSurface(surface);
	}
	else {
		SDL_LogError(SDL_LOG_CATEGORY_RENDER,
			"Failed to lock image surface: %s", SDL_GetError());
		SDL_DestroySurface(surface);
		surface = NULL;
	}

	// Liberar recursos
	Kotonoha_imageReleaseFrame(&pFrameRGB, &buffer);
	Kotonoha_IOMonitorOperationReset(&ioOperation);
	return surface;
}
