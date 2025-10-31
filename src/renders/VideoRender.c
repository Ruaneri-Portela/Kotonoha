#include <Kotonoha/renders/VideoRender.h>

// Função para inicializar o contexto de hardware
static bool initializeHwContext(struct Kotonoha_videoData *instance)
{
	instance->hwCtx = (struct ffmpegHwContext *)SDL_calloc(1, sizeof(struct ffmpegHwContext));
	if (!instance->hwCtx)
	{
		SDL_LogError(SDL_LOG_CATEGORY_ERROR, "Failed to allocate hardware context: %s\n",
					 SDL_GetError());
		return false;
	}
	return true;
}

// Função para renderizar o vídeo
static bool renderVideo(struct Kotonoha_videoData *instance)
{
	AVPacket packet = {0};
	int tryCount = 0;
	bool hasNewFrame = false;

	while (av_read_frame(instance->pFormatCtx, &packet) >= 0)
	{
		// Pula pacotes não relacionados ao vídeo
		if (packet.stream_index != instance->videoStreamIndex)
		{
			av_packet_unref(&packet);
			continue;
		}

		// Envia o pacote de dados para o decoder
		int response = avcodec_send_packet(instance->pCodecCtx, &packet);
		if (response < 0)
		{
			SDL_LogError(SDL_LOG_CATEGORY_ERROR, "Cannot send packet: %s\n", av_err2str(response));
			av_packet_unref(&packet);
			return false;
		}

		// Inicializa o frame
		if (instance->pFrame)
			av_frame_free(&instance->pFrame);

		instance->pFrame = av_frame_alloc();
		if (!instance->pFrame)
		{
			SDL_LogError(SDL_LOG_CATEGORY_ERROR, "Cannot allocate frame\n");
			av_packet_unref(&packet);
			return false;
		}

		// Recebe o frame decodificado
		response = avcodec_receive_frame(instance->pCodecCtx, instance->pFrame);
		if (response == AVERROR(EAGAIN))
		{
			av_packet_unref(&packet);
			continue;
		}
		if (response < 0)
		{
			SDL_LogError(SDL_LOG_CATEGORY_ERROR, "Cannot receive frame: %s\n",
						 av_err2str(response));
			av_frame_free(&instance->pFrame);
			av_packet_unref(&packet);
			continue; // Tenta novamente se não conseguir receber o frame
		}

		// Inicializa o tempo do frame, se necessário
		if (instance->frameTime == 0)
		{
			AVRational avFrameRate = av_guess_frame_rate(instance->pFormatCtx,
														 instance->pFormatCtx->streams[instance->videoStreamIndex],
														 instance->pFrame);
			double frameRate = av_q2d(avFrameRate);
			if (frameRate <= 0)
			{
				SDL_LogError(SDL_LOG_CATEGORY_ERROR, "Invalid frame rate\n");
				av_packet_unref(&packet);
				return false;
			}
			instance->frameTime = (Uint64)(1000.0 / frameRate);
		}

		// Calcula o tempo do vídeo e verifica se o frame é válido
		instance->videoTime = Kotonoha_UtilsFFmpegGetTime(
			instance->pFormatCtx->streams[instance->videoStreamIndex], instance->pFrame->pts,
			instance->pFrame->pkt_dts);
		Sint64 diff = instance->lastTime - instance->videoTime;
		if (diff > 0)
		{
			av_frame_free(&instance->pFrame);
			av_packet_unref(&packet);
			continue;
		}

		// Transferência de dados para a CPU, se necessário
		if (instance->pCodecCtx->hw_device_ctx)
		{
			AVFrame *pFrameHw = instance->pFrame;
			instance->pFrame = av_frame_alloc();
			if (!instance->pFrame)
			{
				SDL_LogError(SDL_LOG_CATEGORY_ERROR, "Cannot allocate frame for HW data\n");
				av_frame_free(&pFrameHw);
				av_packet_unref(&packet);
				return false;
			}

			response = av_hwframe_transfer_data(instance->pFrame, pFrameHw, 0);
			av_frame_free(&pFrameHw);
			if (response < 0)
			{
				SDL_LogError(SDL_LOG_CATEGORY_ERROR, "Cannot transfer HW data: %s\n",
							 av_err2str(response));
				av_packet_unref(&packet);
				return false;
			}
		}

		hasNewFrame = true;
		break; // Renderiza um quadro por chamada
	}

	av_packet_unref(&packet);
	return hasNewFrame;
}

// Função para inicializar o contexto de vídeo
static bool initializeVideoContext(struct Kotonoha_videoData *instance, const char *filename)
{
	if (!Kotonoha_UtilsFFmpegLoad(filename, &instance->pFormatCtx, &instance->pCodecCtx,
								  AVMEDIA_TYPE_VIDEO, &instance->videoStreamIndex, 0,
								  instance->hwCtx))
	{
		SDL_LogError(SDL_LOG_CATEGORY_ERROR, "Failed to load video file: %s\n", filename);
		return false;
	}

	renderVideo(instance);

	instance->swsCtx = Kotonoha_UtilsFFmpegCreateSwsContext(instance->pCodecCtx,
															instance->pFrame->format,
															instance->pCodecCtx->width,
															instance->pCodecCtx->height);

	if (!instance->swsCtx)
	{
		SDL_LogError(SDL_LOG_CATEGORY_ERROR, "Cannot create sws context\n");
		return false;
	}

	return true;
}

// Função principal para inicializar o vídeo
struct Kotonoha_videoData *Kotonoha_VideoRenderInit(const char *filename, struct Kotonoha_time *time, Uint64 startTime, Uint64 endTime)
{
	struct Kotonoha_videoData *videoInstance = (struct Kotonoha_videoData *)SDL_calloc(1,
																					   sizeof(struct Kotonoha_videoData));
	if (!videoInstance)
	{
		SDL_LogError(SDL_LOG_CATEGORY_ERROR, "Failed to allocate video instance: %s\n",
					 SDL_GetError());
		return NULL;
	}

	if (!initializeHwContext(videoInstance) || !initializeVideoContext(videoInstance, filename))
	{
		Kotonoha_VideoRenderShutdown(&videoInstance);
		return NULL;
	}

	videoInstance->time = time;
	videoInstance->startTime = startTime;
	videoInstance->endTime = endTime;
	renderVideo(videoInstance);

	return videoInstance;
}

// Função para finalizar a renderização de vídeo
void Kotonoha_VideoRenderShutdown(struct Kotonoha_videoData **instance)
{
	if (!instance || !*instance)
		return;

	struct Kotonoha_videoData *videoData = *instance;

	if (videoData->hwCtx)
	{
		SDL_free(videoData->hwCtx);
		videoData->hwCtx = NULL;
	}
	if (videoData->pFrame)
		av_frame_free(&videoData->pFrame);
	if (videoData->pFormatCtx)
		avformat_free_context(videoData->pFormatCtx);
	if (videoData->pCodecCtx)
		avcodec_free_context(&videoData->pCodecCtx);
	if (videoData->swsCtx)
		sws_freeContext(videoData->swsCtx);
	if (videoData->texture)
		SDL_DestroyTexture(videoData->texture);

	SDL_free(videoData);
	*instance = NULL;
}

// Função para processar a renderização do vídeo
enum Kotonoha_Scene_Status Kotonoha_VideoRenderProcess(void *userData, SDL_Renderer *render)
{
	struct Kotonoha_videoData *instance = (struct Kotonoha_videoData *)userData;

	if (!instance)
		return KOTONOHA_SCENE_NULL;

	bool inRange;
	int diff;
	Uint64 currentTime = Kotonoha_timeGetFromEvent(instance->time, instance->startTime,
												   instance->endTime, &inRange, &diff);

	if (!inRange)
		return (diff > 0) ? KOTONOHA_SCENE_COMPLETE : KOTONOHA_SCENE_WAITING;

	Uint64 elapsedTime = currentTime - instance->lastTime;
	if (elapsedTime >= instance->frameTime)
	{
		if (instance->pFrame == NULL && !renderVideo(instance))
			return KOTONOHA_SCENE_WAITING;

		if (instance->texture == NULL)
		{
			instance->texture = SDL_CreateTexture(render, SDL_PIXELFORMAT_RGBA32,
												  SDL_TEXTUREACCESS_STREAMING,
												  instance->pCodecCtx->width,
												  instance->pCodecCtx->height);
			if (instance->texture == NULL)
			{
				SDL_LogError(SDL_LOG_CATEGORY_ERROR, "Failed to create texture: %s\n",
							 SDL_GetError());
				return KOTONOHA_SCENE_FATAL_ERROR;
			}
			SDL_SetTextureBlendMode(instance->texture, SDL_BLENDMODE_BLEND);
		}

		Uint8 *buffer = NULL;
		AVFrame *pFrameRGB = Kotonoha_UtilsFFmpegSwsConvertFrame(instance->swsCtx, instance->pFrame,
																 &buffer, instance->pFrame->height, instance->pFrame->width);

		if (pFrameRGB == NULL)
			return KOTONOHA_SCENE_FATAL_ERROR;

		SDL_UpdateTexture(instance->texture, NULL, pFrameRGB->data[0], pFrameRGB->linesize[0]);

		av_free(buffer);
		av_frame_free(&pFrameRGB);
		av_frame_free(&instance->pFrame);
		instance->lastTime = currentTime;
	}

	return KOTONOHA_SCENE_DRAW;
}