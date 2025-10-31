#include <Kotonoha/renders/AudioRender.h>

// Função auxiliar para liberar os recursos de áudio
static void cleanupAudioResources(AVPacket *packet, Uint8 **nonPlanarData, AVFrame *pFrame)
{
	if (packet)
		av_packet_unref(packet);
	if (nonPlanarData)
	{
		av_freep(&nonPlanarData[0]);
		av_free(nonPlanarData);
	}
	if (pFrame)
		av_frame_free(&pFrame);
}

// Função auxiliar para alocar o buffer de áudio
static int allocateAudioBuffer(struct Kotonoha_audioDecode *instance, int chuckSize, Uint8 **target,
							   int *size)
{
	*size = chuckSize * sizeof(float) * instance->specification.channels;
	*target = (Uint8 *)SDL_malloc(*size);

	if (!*target)
	{
		SDL_LogError(SDL_LOG_CATEGORY_ERROR, "Error allocating target buffer\n");
		return 0;
	}
	return 1;
}

// Gera ruído branco e armazena nos dados de áudio alvo
void genWhiteNoise(void *parms, Uint8 **target, size_t *size)
{
	(void)parms;
	const size_t bufferSize = 1024;
	float *data = (float *)SDL_malloc(bufferSize * sizeof(float));

	if (!data)
	{
		SDL_LogError(SDL_LOG_CATEGORY_ERROR, "Failed to allocate memory for white noise\n");
		return;
	}

	// Gera ruído branco
	for (size_t i = 0; i < bufferSize / sizeof(float); i++)
	{
		data[i] = (2.0f * ((float)rand() / (float)RAND_MAX) - 1.0f) * 0.01f;
	}

	*target = (Uint8 *)data;
	*size = bufferSize / sizeof(float);
}

// Inicializa o contexto de áudio a partir do caminho e especificações dadas
struct Kotonoha_audioDecode *Kotonoha_AudioInit(const char *path, SDL_AudioSpec specification)
{
	struct Kotonoha_audioDecode *audioDecode = (struct Kotonoha_audioDecode *)SDL_calloc(1, sizeof(struct Kotonoha_audioDecode));

	if (!audioDecode)
	{
		SDL_LogError(SDL_LOG_CATEGORY_ERROR, "calloc");
		return NULL;
	}

	// Carregar o contexto do arquivo de áudio com FFmpeg
	if (!Kotonoha_UtilsFFmpegLoad(path, &audioDecode->formatCtx, &audioDecode->codecCtx,
								  AVMEDIA_TYPE_AUDIO, &audioDecode->audioStreamIndex, 0, NULL))
	{
		SDL_free(audioDecode);
		return NULL;
	}

	audioDecode->specification = specification;
	audioDecode->swrCtx = swr_alloc();
	audioDecode->executions = 1;

	if (!audioDecode->swrCtx)
	{
		SDL_LogError(SDL_LOG_CATEGORY_ERROR, "Failed to allocate SwrContext\n");
		SDL_free(audioDecode);
		return NULL;
	}

	// Configurar o contexto de conversão de amostras
	AVChannelLayout layout = {0, audioDecode->specification.channels, 0, 0};
	if (swr_alloc_set_opts2(&audioDecode->swrCtx, &layout, AV_SAMPLE_FMT_FLT, audioDecode->specification.freq,
							&audioDecode->codecCtx->ch_layout, audioDecode->codecCtx->sample_fmt,
							audioDecode->codecCtx->sample_rate, 0, NULL) < 0)
	{
		SDL_LogError(SDL_LOG_CATEGORY_ERROR, "Failed to set SwrContext options\n");
		swr_free(&audioDecode->swrCtx);
		SDL_free(audioDecode);
		return NULL;
	}

	if (swr_init(audioDecode->swrCtx) < 0)
	{
		SDL_LogError(SDL_LOG_CATEGORY_ERROR, "Failed to initialize SwrContext\n");
		swr_free(&audioDecode->swrCtx);
		SDL_free(audioDecode);
		return NULL;
	}

	return audioDecode;
}

// Função para buscar um tempo específico no áudio
void Kotonoha_AudioSeek(struct Kotonoha_audioDecode *ctx, Sint64 time)
{
	if (av_seek_frame(ctx->formatCtx, ctx->audioStreamIndex, time, AVSEEK_FLAG_BACKWARD) < 0)
	{
		SDL_LogError(SDL_LOG_CATEGORY_ERROR, "SeekError time %d\n", (int)time);
		return;
	}
	ctx->start -= time;
	ctx->end -= time;
	ctx->executions++;
	avcodec_flush_buffers(ctx->codecCtx);
}

// Renderiza o áudio decodificado para o buffer de destino
int Kotonoha_AudioRender(void *data, Uint8 **target, int *size)
{
	struct Kotonoha_audioDecode *instance = (struct Kotonoha_audioDecode *)data;
	struct Kotonoha_time *time = NULL;
	int returnCode = -1;

	// Verifica se o tempo está pausado
	if (instance->tm != NULL && *instance->tm != NULL)
	{
		time = *instance->tm;
		if (time->isPaused)
			return 0;
	}

	AVPacket packet;

	while (av_read_frame(instance->formatCtx, &packet) >= 0)
	{
		returnCode = 1;
		if (packet.stream_index != instance->audioStreamIndex)
		{
			cleanupAudioResources(&packet, NULL, NULL);
			continue; // Ignora pacotes que não são de áudio
		}

		instance->audioTime = Kotonoha_UtilsFFmpegGetTime(instance->formatCtx->streams[instance->audioStreamIndex], packet.pts, packet.dts);

		// Verifica se o tempo atual é maior que o tempo do áudio
		Sint64 diff = instance->lastTime - (instance->audioTime * instance->executions);
		if (time != NULL && diff > 0)
		{
			cleanupAudioResources(&packet, NULL, NULL);
			continue;
		}

		// Envia o pacote para o codec
		if (avcodec_send_packet(instance->codecCtx, &packet) < 0)
		{
			SDL_LogError(SDL_LOG_CATEGORY_ERROR, "Error sending packet to codec\n");
			cleanupAudioResources(&packet, NULL, NULL);
			break;
		}

		AVFrame *pFrame = av_frame_alloc();
		if (!pFrame)
		{
			SDL_LogError(SDL_LOG_CATEGORY_ERROR, "Error allocating frame\n");
			cleanupAudioResources(&packet, NULL, NULL);
			break;
		}

		// Recebe o quadro decodificado
		if (avcodec_receive_frame(instance->codecCtx, pFrame) < 0)
		{
			SDL_LogError(SDL_LOG_CATEGORY_ERROR, "Error receiving frame\n");
			cleanupAudioResources(&packet, NULL, pFrame);
			break;
		}

		// Alocar espaço para áudio convertido
		Uint8 **nonPlanarData = NULL;
		if (av_samples_alloc_array_and_samples(&nonPlanarData, NULL, instance->specification.channels, pFrame->nb_samples, AV_SAMPLE_FMT_FLT, 0) < 0)
		{
			SDL_LogError(SDL_LOG_CATEGORY_ERROR, "Error allocating audio samples\n");
			cleanupAudioResources(&packet, nonPlanarData, pFrame);
			break;
		}

		// Converter áudio para o formato esperado
		int chuckSize = swr_convert(instance->swrCtx, (Uint8 **)nonPlanarData, pFrame->nb_samples,
									(const Uint8 **)pFrame->data, pFrame->nb_samples);
		av_frame_free(&pFrame);

		if (chuckSize < 0)
		{
			SDL_LogError(SDL_LOG_CATEGORY_ERROR, "Error converting audio samples\n");
			cleanupAudioResources(&packet, nonPlanarData, NULL);
			break;
		}

		// Copiar dados convertidos para o buffer de destino
		if (!allocateAudioBuffer(instance, chuckSize, target, size))
		{
			cleanupAudioResources(&packet, nonPlanarData, NULL);
			break;
		}

		SDL_memcpy(*target, nonPlanarData[0], *size);
		cleanupAudioResources(&packet, nonPlanarData, NULL);
		returnCode = 2;
		break; // Sai do loop após processar um pacote
	}
	return returnCode;
}

// Libera a memória associada ao contexto de áudio
void Kotonoha_AudioFree(void *data)
{
	if (data != NULL)
	{
		struct Kotonoha_audioDecode *audioDecode = data;
		swr_free(&audioDecode->swrCtx);
		avformat_close_input(&audioDecode->formatCtx);
		avcodec_free_context(&audioDecode->codecCtx);
		SDL_free(audioDecode);
	}
}
