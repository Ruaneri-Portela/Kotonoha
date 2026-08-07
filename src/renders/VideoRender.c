#include <Kotonoha/renders/VideoRender.h>

static inline Uint64 u64_abs_diff(Uint64 a, Uint64 b) {
	return (a > b) ? (a - b) : (b - a);
}

// Inicializa o contexto de hardware
static bool initializeHwContext(struct Kotonoha_videoData* instance) {
	instance->hwCtx =
		(struct ffmpegHwContext*)SDL_calloc(1, sizeof(struct ffmpegHwContext));
	if (!instance->hwCtx) {
		SDL_LogError(SDL_LOG_CATEGORY_ERROR,
			"Failed to allocate hardware context: %s\n", SDL_GetError());
		return false;
	}
	return true;
}

static void Kotonoha_VideoSeek(struct Kotonoha_videoData* instance, Uint64 timeMs) {
	if (!instance || !instance->pFormatCtx || !instance->pCodecCtx)
		return;

	AVStream* stream = instance->pFormatCtx->streams[instance->videoStreamIndex];
	int64_t seekTs = av_rescale_q(
		(int64_t)timeMs,
		(AVRational) {
		1, 1000
	},
		stream->time_base);

	if (av_seek_frame(instance->pFormatCtx, instance->videoStreamIndex, seekTs,
		AVSEEK_FLAG_BACKWARD) < 0) {
		SDL_LogError(SDL_LOG_CATEGORY_ERROR,
			"Video SeekError timeMs=%llu seekTs=%lld\n",
			(unsigned long long)timeMs, (long long)seekTs);
		return;
	}

	avcodec_flush_buffers(instance->pCodecCtx);

	if (instance->pFrame) {
		av_frame_free(&instance->pFrame);
		instance->pFrame = NULL;
	}

	instance->videoTime = timeMs;
	instance->lastTime = timeMs;
}

static void Kotonoha_VideoEnsureSync(struct Kotonoha_videoData* instance,
	Uint64 currentTimeMs,
	bool forceOnOpen) {
	if (!instance)
		return;

	if (forceOnOpen) {
		Kotonoha_VideoSeek(instance, currentTimeMs);
		return;
	}

	if (u64_abs_diff(currentTimeMs, instance->videoTime) > 1000) {
		Kotonoha_VideoSeek(instance, currentTimeMs);
	}
}

// Renderiza/decodifica até obter um frame atual
static bool renderVideo(struct Kotonoha_videoData* instance, Uint64 currentTime) {
	AVPacket packet = { 0 };
	int response;
	bool hasNewFrame = false;
	AVStream* stream = instance->pFormatCtx->streams[instance->videoStreamIndex];

	if (!instance->pFrame) {
		instance->pFrame = av_frame_alloc();
		if (!instance->pFrame) {
			SDL_LogError(SDL_LOG_CATEGORY_ERROR, "Cannot allocate frame\n");
			return false;
		}
	}

	while (true) {
		response = avcodec_receive_frame(instance->pCodecCtx, instance->pFrame);

		if (response == 0) {
			Uint64 framePtsMs = Kotonoha_UtilsFFmpegGetTime(
				stream,
				instance->pFrame->pts,
				instance->pFrame->pkt_dts);

			if (framePtsMs < currentTime) {
				av_frame_unref(instance->pFrame);
				continue;
			}

			instance->videoTime = framePtsMs;
			hasNewFrame = true;
			break;
		}

		if (response == AVERROR(EAGAIN)) {
			// precisa alimentar mais packets
		}
		else if (response == AVERROR_EOF) {
			hasNewFrame = false;
			break;
		}
		else {
			SDL_LogError(SDL_LOG_CATEGORY_ERROR, "Cannot receive frame: %s\n",
				av_err2str(response));
			hasNewFrame = false;
			break;
		}

		response = av_read_frame(instance->pFormatCtx, &packet);
		if (response < 0) {
			if (response == AVERROR_EOF) {
				avcodec_send_packet(instance->pCodecCtx, NULL);
			}
			hasNewFrame = false;
			break;
		}

		if (packet.stream_index != instance->videoStreamIndex) {
			av_packet_unref(&packet);
			continue;
		}

		response = avcodec_send_packet(instance->pCodecCtx, &packet);
		av_packet_unref(&packet);

		if (response < 0) {
			SDL_LogError(SDL_LOG_CATEGORY_ERROR, "Cannot send packet: %s\n",
				av_err2str(response));
			hasNewFrame = false;
			break;
		}
	}

	if (!hasNewFrame)
		return false;

	return true;
}

// Inicializa o contexto de vídeo
static bool initializeVideoContext(struct Kotonoha_videoData* instance,
	const char* filename) {
	if (!Kotonoha_UtilsFFmpegLoad(filename, &instance->pFormatCtx,
		&instance->pCodecCtx, AVMEDIA_TYPE_VIDEO,
		&instance->videoStreamIndex, 0,
		instance->hwCtx)) {
		SDL_LogError(SDL_LOG_CATEGORY_ERROR, "Failed to load video file: %s\n",
			filename);
		return false;
	}
	return true;
}

// Converte frame HW para SW se necessário.
// Se retornar o mesmo ponteiro recebido, não deve liberar.
// Se retornar outro frame, o chamador deve liberar com av_frame_free().
static AVFrame* getCpuReadableFrame(AVFrame* src) {
	if (!src)
		return NULL;

	// Frame já está em memória normal
	if (!src->hw_frames_ctx)
		return src;

	AVFrame* swFrame = av_frame_alloc();
	if (!swFrame)
		return NULL;

	// FFmpeg recomenda transferir o frame HW para SW antes do uso na CPU
	if (av_hwframe_transfer_data(swFrame, src, 0) < 0) {
		av_frame_free(&swFrame);
		return NULL;
	}

	if (av_frame_copy_props(swFrame, src) < 0) {
		av_frame_free(&swFrame);
		return NULL;
	}

	return swFrame;
}

// Inicialização principal
struct Kotonoha_videoData* Kotonoha_VideoRenderInit(const char* filename,
	struct Kotonoha_time* time,
	Uint64 startTime,
	Uint64 endTime) {
	struct Kotonoha_videoData* videoInstance =
		(struct Kotonoha_videoData*)SDL_calloc(1, sizeof(struct Kotonoha_videoData));
	if (!videoInstance) {
		SDL_LogError(SDL_LOG_CATEGORY_ERROR,
			"Failed to allocate video instance: %s\n", SDL_GetError());
		return NULL;
	}

	if (!initializeHwContext(videoInstance) ||
		!initializeVideoContext(videoInstance, filename)) {
		Kotonoha_VideoRenderShutdown(&videoInstance);
		return NULL;
	}

	videoInstance->time = time;
	videoInstance->startTime = startTime;
	videoInstance->endTime = endTime;
	videoInstance->videoTime = 0;
	videoInstance->lastTime = 0;

	bool inRange;
	Sint64 diff;
	Uint64 toSeek = Kotonoha_timeGetFromEvent(
		time, startTime, endTime, &inRange, &diff);

	Kotonoha_VideoEnsureSync(videoInstance, toSeek, true);

	if (!renderVideo(videoInstance, toSeek)) {
		Kotonoha_VideoRenderShutdown(&videoInstance);
		return NULL;
	}

	return videoInstance;
}

// Shutdown
void Kotonoha_VideoRenderShutdown(struct Kotonoha_videoData** instance) {
	if (!instance || !*instance)
		return;
	struct Kotonoha_videoData* videoData = *instance;
	if (videoData->hwCtx && videoData->hwCtx->deviceRef) {
		av_buffer_unref(&videoData->pCodecCtx->hw_device_ctx);
		av_buffer_unref(&videoData->hwCtx->deviceRef);
	}
	if (videoData->hwCtx) {
		SDL_free(videoData->hwCtx);
		videoData->hwCtx = NULL;
	}
	if (videoData->pFrame)
		av_frame_free(&videoData->pFrame);
	if (videoData->pFormatCtx)
		avformat_close_input(&videoData->pFormatCtx);
	if (videoData->pCodecCtx)
		avcodec_free_context(&videoData->pCodecCtx);
	if (videoData->swsCtx)
		sws_freeContext(videoData->swsCtx);
	if (videoData->texture)
		SDL_DestroyTexture(videoData->texture);

	SDL_free(videoData);
	*instance = NULL;
}

// Processa a renderização do vídeo
enum Kotonoha_Scene_Status Kotonoha_VideoRenderProcess(void* userData,
	SDL_Renderer* render) {
	struct Kotonoha_videoData* instance = (struct Kotonoha_videoData*)userData;

	if (!instance)
		return KOTONOHA_SCENE_NULL;

	bool inRange;
	Sint64 diff;
	Uint64 currentTime = Kotonoha_timeGetFromEvent(
		instance->time, instance->startTime, instance->endTime, &inRange, &diff);

	if (!inRange)
		return (diff > 0) ? KOTONOHA_SCENE_COMPLETE : KOTONOHA_SCENE_NULL;

	Kotonoha_VideoEnsureSync(instance, currentTime, false);

	if (instance->pFrame == NULL && !renderVideo(instance, currentTime))
		return KOTONOHA_SCENE_WAITING;

	if (currentTime < instance->videoTime)
		return KOTONOHA_SCENE_DRAW;

	if (instance->texture == NULL) {
		instance->texture = SDL_CreateTexture(
			render, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STREAMING,
			instance->pCodecCtx->width, instance->pCodecCtx->height);

		if (instance->texture == NULL) {
			SDL_LogError(SDL_LOG_CATEGORY_ERROR, "Failed to create texture: %s\n",
				SDL_GetError());
			return KOTONOHA_SCENE_FATAL_ERROR;
		}

		SDL_SetTextureBlendMode(instance->texture, SDL_BLENDMODE_BLEND);
	}

	if (instance->pFrame == NULL ||
		instance->pFrame->pict_type == AV_PICTURE_TYPE_NONE) {
		return KOTONOHA_SCENE_NULL;
	}

	AVFrame* cpuFrame = getCpuReadableFrame(instance->pFrame);
	if (!cpuFrame) {
		SDL_LogError(SDL_LOG_CATEGORY_ERROR,
			"Failed to obtain CPU-readable frame from decoder output\n");
		return KOTONOHA_SCENE_FATAL_ERROR;
	}

	// O swsCtx deve ser criado com base no frame realmente lido pela CPU,
	// pois um frame HW transferido pode virar NV12/P010/etc em memória normal.
	if (!instance->swsCtx) {
		instance->swsCtx = Kotonoha_UtilsFFmpegCreateSwsContext(
			instance->pCodecCtx,
			cpuFrame->format,
			cpuFrame->width,
			cpuFrame->height);

		if (!instance->swsCtx) {
			SDL_LogError(SDL_LOG_CATEGORY_ERROR, "Cannot create sws context\n");
			if (cpuFrame != instance->pFrame)
				av_frame_free(&cpuFrame);
			return KOTONOHA_SCENE_FATAL_ERROR;
		}
	}

	Uint8* buffer = NULL;
	AVFrame* pFrameRGB = Kotonoha_UtilsFFmpegSwsConvertFrame(
		instance->swsCtx,
		cpuFrame,
		&buffer,
		cpuFrame->height,
		cpuFrame->width);

	if (pFrameRGB == NULL) {
		if (cpuFrame != instance->pFrame)
			av_frame_free(&cpuFrame);
		return KOTONOHA_SCENE_FATAL_ERROR;
	}

	SDL_UpdateTexture(instance->texture, NULL,
		pFrameRGB->data[0], pFrameRGB->linesize[0]);

	av_free(buffer);
	av_frame_free(&pFrameRGB);

	if (cpuFrame != instance->pFrame)
		av_frame_free(&cpuFrame);

	av_frame_free(&instance->pFrame);
	instance->lastTime = currentTime;

	return KOTONOHA_SCENE_DRAW;
}