#include <Kotonoha/utils/FFmpeg.h>
#include <errno.h>
#include <libavutil/error.h>

// Configura a decodificação por hardware, se disponível
static void setupHwDecode(struct ffmpegHwContext* hwCtx,
	const AVCodec** codec) {
	for (int i = 0;; i++) {
		const AVCodecHWConfig* config = avcodec_get_hw_config(*codec, i);
		if (!config)
			break;

		if (config->methods & AV_CODEC_HW_CONFIG_METHOD_HW_DEVICE_CTX &&
			!(av_hwdevice_ctx_create(&hwCtx->deviceRef, config->device_type, NULL,
				NULL, 0) < 0)) {
			hwCtx->hwPixFmt = config->pix_fmt;
			hwCtx->type = config->device_type;
			return;
		}
	}

	// Se não houver suporte a hardware, defina o formato de pixel como
	// AV_PIX_FMT_NONE
	hwCtx->hwPixFmt = AV_PIX_FMT_NONE;
	SDL_LogError(
		SDL_LOG_CATEGORY_APPLICATION,
		"O decodificador %s não suporta hardware aqui, voltando para Software",
		(*codec)->name);
}

int Kotonoha_UtilsFFmpegReadFrame(AVFormatContext* formatCtx,
	AVPacket* packet) {
	if (formatCtx == NULL || packet == NULL) {
		return AVERROR(EINVAL);
	}

	Kotonoha_IOMonitorOperation* operation =
		(Kotonoha_IOMonitorOperation*)formatCtx->interrupt_callback.opaque;
	if (operation == NULL) {
		return av_read_frame(formatCtx, packet);
	}
	Kotonoha_IOMonitorOperationBegin(operation);
	const int result = av_read_frame(formatCtx, packet);
	Kotonoha_IOMonitorOperationEnd(operation);
	if (result == AVERROR_EXIT) {
		Kotonoha_IOMonitorOperationSetStalled(
			operation, !Kotonoha_IOMonitorOperationIsCancelled(operation));
		return result;
	}
	if (result >= 0 ||
		(result < 0 && result != AVERROR(EAGAIN))) {
		Kotonoha_IOMonitorOperationSetStalled(operation, false);
	}
	return result;
}

int Kotonoha_UtilsFFmpegSeekFrame(AVFormatContext* formatCtx,
	Kotonoha_IOMonitorOperation* operation, int streamIndex,
	int64_t timestamp, int flags) {
	if (formatCtx == NULL) {
		return AVERROR(EINVAL);
	}

	Kotonoha_IOMonitorOperationBegin(operation);
	const int result = av_seek_frame(formatCtx, streamIndex, timestamp, flags);
	Kotonoha_IOMonitorOperationEnd(operation);
	if (result == AVERROR_EXIT) {
		Kotonoha_IOMonitorOperationSetStalled(
			operation, !Kotonoha_IOMonitorOperationIsCancelled(operation));
		return result;
	}
	Kotonoha_IOMonitorOperationSetStalled(operation, false);
	return result;
}

// Carrega o contexto FFmpeg e configura o decodificador
bool Kotonoha_UtilsFFmpegLoadMonitored(
	const char* path, AVFormatContext** formatCtx,
	AVCodecContext** codecCtx,
	enum AVMediaType mediaType,
	int* streamIndexExport, int startIndex,
	struct ffmpegHwContext* hwCtx,
	Kotonoha_IOMonitorOperation* ioOperation) {
	if (path == NULL || formatCtx == NULL || codecCtx == NULL ||
		ioOperation == NULL) {
		SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
			"Invalid FFmpeg load arguments");
		return false;
	}
	Kotonoha_IOMonitorSetMissing(ioOperation, false);

	AVDictionary* opts = NULL;
	bool isHttp = SDL_strstr(path, "http") != NULL;
	int openResult;
	do {
		*formatCtx = avformat_alloc_context();
		if (*formatCtx == NULL) {
			SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
				"Não foi possível alocar o contexto do arquivo '%s'", path);
			Kotonoha_IOMonitorOperationReset(ioOperation);
			return false;
		}
		(*formatCtx)->interrupt_callback.callback = Kotonoha_IOMonitorInterrupt;
		(*formatCtx)->interrupt_callback.opaque = ioOperation;

		if (isHttp) {
			av_dict_set(&opts, "user_agent", "KotonohaEngine/0.1", 0);
		}
		Kotonoha_IOMonitorOperationBegin(ioOperation);
		openResult = avformat_open_input(formatCtx, path, NULL, &opts);
		Kotonoha_IOMonitorOperationEnd(ioOperation);
		av_dict_free(&opts);
		if (openResult == AVERROR_EXIT &&
			!Kotonoha_IOMonitorOperationIsCancelled(ioOperation)) {
			if (*formatCtx != NULL) {
				avformat_close_input(formatCtx);
			}
			SDL_Delay(10);
			continue;
		}
		if (openResult == AVERROR(ENOENT) ||
			openResult == AVERROR(ENOTDIR)) {
			Kotonoha_IOMonitorSetMissing(ioOperation, true);
			if (Kotonoha_IOMonitorShouldRetryMissing(ioOperation) &&
				!Kotonoha_IOMonitorOperationIsCancelled(ioOperation)) {
				if (*formatCtx != NULL) {
					avformat_close_input(formatCtx);
				}
			}
		}
		break;
	} while (true);

	if (openResult < 0) {
		Kotonoha_IOMonitorOperationSetStalled(ioOperation, false);
		if (*formatCtx != NULL) {
			avformat_close_input(formatCtx);
		}
		if (openResult != AVERROR_EXIT &&
			!Kotonoha_IOMonitorOperationIsCancelled(ioOperation) &&
			!Kotonoha_IOMonitorIsIgnoredMissing(ioOperation)) {
			SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
				"Erro: Não foi possível abrir o arquivo '%s': %s",
				path, av_err2str(openResult));
		}
		return false;
	}
	// Obtém informações sobre o fluxo
	int streamInfoResult;
	do {
		Kotonoha_IOMonitorOperationBegin(ioOperation);
		streamInfoResult = avformat_find_stream_info(*formatCtx, NULL);
		Kotonoha_IOMonitorOperationEnd(ioOperation);
		if (streamInfoResult == AVERROR_EXIT &&
			!Kotonoha_IOMonitorOperationIsCancelled(ioOperation)) {
			SDL_Delay(10);
		}
	} while (streamInfoResult == AVERROR_EXIT &&
		!Kotonoha_IOMonitorOperationIsCancelled(ioOperation));

	if (streamInfoResult < 0) {
		if (streamInfoResult != AVERROR_EXIT) {
			Kotonoha_IOMonitorOperationSetStalled(ioOperation, false);
			SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
				"Erro: Não foi possível encontrar informações do fluxo: %s",
				av_err2str(streamInfoResult));
		}
		avformat_close_input(formatCtx);
		return false;
	}
	Kotonoha_IOMonitorSetMissing(ioOperation, false);
	Kotonoha_IOMonitorOperationSetStalled(ioOperation, false);

	// Encontra o índice do fluxo correspondente ao tipo de mídia especificado
	int streamIndex = -1;
	for (unsigned int i = startIndex; i < (*formatCtx)->nb_streams; i++) {
		if ((*formatCtx)->streams[i]->codecpar->codec_type == mediaType) {
			streamIndex = (int)i;
			break;
		}
	}

	if (streamIndex == -1) {
		SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
			"Erro: Nenhum fluxo do tipo %d encontrado", mediaType);
		avformat_close_input(formatCtx);
		return false;
	}

	// Obtém o codec adequado
	const AVCodec* codec = avcodec_find_decoder(
		(*formatCtx)->streams[streamIndex]->codecpar->codec_id);
	if (!codec) {
		SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
			"Erro: Codec não encontrado para codec_id %d",
			(*formatCtx)->streams[streamIndex]->codecpar->codec_id);
		avformat_close_input(formatCtx);
		return false;
	}

	// Configura a decodificação de hardware, se necessário
	if (true && hwCtx != NULL) {
		setupHwDecode(hwCtx, &codec);
	}

	// Aloca o contexto do codec
	*codecCtx = avcodec_alloc_context3(codec);
	if (!*codecCtx) {
		SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
			"Erro: Não foi possível alocar o contexto do codec");
		avformat_close_input(formatCtx);
		return false;
	}

	// Copia parâmetros do codec para o contexto do codec
	if (avcodec_parameters_to_context(
		*codecCtx, (*formatCtx)->streams[streamIndex]->codecpar) < 0) {
		SDL_LogError(
			SDL_LOG_CATEGORY_APPLICATION,
			"Erro: Não foi possível copiar parâmetros do codec para o contexto");
		avcodec_free_context(codecCtx);
		avformat_close_input(formatCtx);
		return false;
	}

	// Configura o contexto do dispositivo de hardware, se aplicável
	if (hwCtx != NULL && hwCtx->hwPixFmt != AV_PIX_FMT_NONE) {
		(*codecCtx)->hw_device_ctx = av_buffer_ref(hwCtx->deviceRef);
		(*codecCtx)->pix_fmt = hwCtx->hwPixFmt;
	}

	// Abre o codec
	if (avcodec_open2(*codecCtx, codec, NULL) < 0) {
		SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
			"Erro: Não foi possível abrir o codec");
		avcodec_free_context(codecCtx);
		avformat_close_input(formatCtx);
		return false;
	}

	// Exporta o índice do fluxo, se solicitado
	if (streamIndexExport != NULL) {
		*streamIndexExport = streamIndex;
	}

	return true;
}

bool Kotonoha_UtilsFFmpegLoad(const char* path, AVFormatContext** formatCtx,
	AVCodecContext** codecCtx, enum AVMediaType mediaType,
	int* streamIndexExport, int startIndex,
	struct ffmpegHwContext* hwCtx) {
	Kotonoha_IOMonitorOperation operation;
	Kotonoha_IOMonitorOperationInit(&operation, NULL);
	const bool loaded = Kotonoha_UtilsFFmpegLoadMonitored(
		path, formatCtx, codecCtx, mediaType, streamIndexExport, startIndex,
		hwCtx, &operation);
	if (loaded && formatCtx != NULL && *formatCtx != NULL) {
		(*formatCtx)->interrupt_callback.callback = NULL;
		(*formatCtx)->interrupt_callback.opaque = NULL;
	}
	Kotonoha_IOMonitorOperationReset(&operation);
	return loaded;
}

// Calcula o timestamp do pacote de mídia
Uint64 Kotonoha_UtilsFFmpegGetTime(AVStream* stream, Sint64 pts, Sint64 dts) {
	if (pts != AV_NOPTS_VALUE) {
		return (Uint64)(pts * av_q2d(stream->time_base) * 1000);
	}
	if (dts != AV_NOPTS_VALUE) {
		return (Uint64)(dts * av_q2d(stream->time_base) * 1000);
	}

	SDL_LogError(SDL_LOG_CATEGORY_ERROR, "Ambos PTS e DTS são inválidos\n");
	return 0;
}

struct SwsContext* Kotonoha_UtilsFFmpegCreateSwsContext(
	AVCodecContext* codecCtx, enum AVPixelFormat srcPixelFmt, int w, int h) {
	struct SwsContext* swsCtx =
		sws_getContext(codecCtx->width, codecCtx->height, srcPixelFmt, w, h,
			AV_PIX_FMT_RGBA, SWS_LANCZOS, NULL, NULL, NULL);
	if (!swsCtx) {
		SDL_LogError(SDL_LOG_CATEGORY_ERROR, "Erro ao criar o contexto sws\n");
	}
	return swsCtx;
}

// Converte o frame usando o contexto de conversão de formato
AVFrame* Kotonoha_UtilsFFmpegSwsConvertFrame(struct SwsContext* convertContext,
	AVFrame* sourceFrame,
	Uint8** buffer, int targetH,
	int targetW) {
	AVFrame* pFrameRGB = av_frame_alloc();
	if (!pFrameRGB) {
		SDL_LogError(0, "Error allocating frame\n");
		return NULL;
	}

	Uint8* frameBuffer = (Uint8*)av_malloc(
		av_image_get_buffer_size(AV_PIX_FMT_RGBA, targetW, targetH, 1));

	if (frameBuffer == NULL) {
		SDL_LogError(0, "Error to allocate buffer\n");
		av_frame_free(&pFrameRGB);
		return NULL;
	}

	if (av_image_fill_arrays(pFrameRGB->data, pFrameRGB->linesize, frameBuffer,
		AV_PIX_FMT_RGBA, targetW, targetH, 1) < 0) {
		SDL_LogError(0, "Error to fill frame\n");
		av_frame_free(&pFrameRGB);
		av_free(frameBuffer);
		return NULL;
	}

	sws_scale(convertContext, (const uint8_t* const*)sourceFrame->data,
		sourceFrame->linesize, 0, sourceFrame->height, pFrameRGB->data,
		pFrameRGB->linesize);
	*buffer = frameBuffer;
	pFrameRGB->height = targetH;
	pFrameRGB->width = targetW;
	return pFrameRGB;
}
