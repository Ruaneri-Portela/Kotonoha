#include <Kotonoha/renders/VideoRender.h>
#include <Kotonoha/speciais/Upscaling.h>
#include <math.h>

static inline Uint64 u64_abs_diff(Uint64 a, Uint64 b) {
	return (a > b) ? (a - b) : (b - a);
}

static inline Uint64 Kotonoha_OrsSceneMsToFrame(Uint64 sceneMs) {
	return 1 + (sceneMs * 24) / 1000;
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

	const int seekResult = Kotonoha_UtilsFFmpegSeekFrame(
		instance->pFormatCtx, &instance->ioOperation,
		instance->videoStreamIndex, seekTs, AVSEEK_FLAG_BACKWARD);
	if (seekResult < 0) {
		if (seekResult == AVERROR_EXIT) {
			return;
		}
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
	instance->demuxEof = false;
	instance->decoderEof = false;
}

static void Kotonoha_VideoEnsureSync(struct Kotonoha_videoData* instance,
	Uint64 currentTimeMs,
	bool forceOnOpen) {
	if (!instance)
		return;

	if (instance->decoderEof && !forceOnOpen)
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
	if (instance->decoderEof)
		return false;

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
			instance->decoderEof = true;
			hasNewFrame = false;
			break;
		}
		else {
			SDL_LogError(SDL_LOG_CATEGORY_ERROR, "Cannot receive frame: %s\n",
				av_err2str(response));
			hasNewFrame = false;
			break;
		}

		if (instance->demuxEof) {
			instance->decoderEof = true;
			break;
		}

		response = Kotonoha_UtilsFFmpegReadFrame(
			instance->pFormatCtx, &packet);
		if (response < 0) {
			if (response == AVERROR_EOF) {
				instance->demuxEof = true;
				const int flushResponse =
					avcodec_send_packet(instance->pCodecCtx, NULL);
				if (flushResponse < 0 && flushResponse != AVERROR_EOF &&
					flushResponse != AVERROR(EAGAIN)) {
					SDL_LogError(SDL_LOG_CATEGORY_ERROR,
						"Cannot flush video decoder: %s\n",
						av_err2str(flushResponse));
				}
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

	if (!hasNewFrame) {
		av_frame_free(&instance->pFrame);
		return false;
	}

	return true;
}

// Inicializa o contexto de vídeo
static bool initializeVideoContext(struct Kotonoha_videoData* instance,
	const char* filename) {
	if (!Kotonoha_UtilsFFmpegLoadMonitored(filename, &instance->pFormatCtx,
		&instance->pCodecCtx, AVMEDIA_TYPE_VIDEO,
		&instance->videoStreamIndex, 0,
		instance->hwCtx, &instance->ioOperation)) {
		if (!Kotonoha_IOMonitorIsIgnoredMissing(&instance->ioOperation)) {
			SDL_LogError(SDL_LOG_CATEGORY_ERROR, "Failed to load video file: %s\n",
				filename);
		}
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
	Uint64 endTime,
	bool useOrsFrameTimeline,
	Uint64 orsEndFrame,
	Kotonoha_IOMonitor* ioMonitor) {
	struct Kotonoha_videoData* videoInstance =
		(struct Kotonoha_videoData*)SDL_calloc(1, sizeof(struct Kotonoha_videoData));
	if (!videoInstance) {
		SDL_LogError(SDL_LOG_CATEGORY_ERROR,
			"Failed to allocate video instance: %s\n", SDL_GetError());
		return NULL;
	}
	Kotonoha_IOMonitorOperationInit(&videoInstance->ioOperation, ioMonitor);

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
	videoInstance->useOrsFrameTimeline = useOrsFrameTimeline;
	videoInstance->orsEndFrame = orsEndFrame;

	bool inRange;
	Sint64 diff;
	Uint64 toSeek = Kotonoha_timeGetFromEvent(
		time, startTime, endTime, &inRange, &diff);

	Kotonoha_VideoEnsureSync(videoInstance, toSeek, true);
	if (videoInstance->ioOperation.stalled) {
		return videoInstance;
	}

	if (!renderVideo(videoInstance, toSeek)) {
		if (!videoInstance->ioOperation.stalled) {
			Kotonoha_VideoRenderShutdown(&videoInstance);
			return NULL;
		}
	}

	return videoInstance;
}

// Shutdown
void Kotonoha_VideoRenderShutdown(struct Kotonoha_videoData** instance) {
    if (!instance || !*instance) {
        return;
    }

    struct Kotonoha_videoData *videoData = *instance;

    if (videoData->pFrame) {
        av_frame_free(&videoData->pFrame);
    }

    if (videoData->pCodecCtx) {
        avcodec_free_context(&videoData->pCodecCtx);
    }

    if (videoData->pFormatCtx) {
        avformat_close_input(&videoData->pFormatCtx);
    }

    if (videoData->swsCtx) {
        sws_freeContext(videoData->swsCtx);
        videoData->swsCtx = NULL;
    }

    if (videoData->hwCtx) {
        if (videoData->hwCtx->deviceRef) {
            av_buffer_unref(&videoData->hwCtx->deviceRef);
        }

        SDL_free(videoData->hwCtx);
        videoData->hwCtx = NULL;
    }

    if (videoData->texture) {
        SDL_DestroyTexture(videoData->texture);
        videoData->texture = NULL;
    }

    if (videoData->extraTexture) {
        SDL_DestroyTexture(videoData->extraTexture);
        videoData->extraTexture = NULL;
    }

    Kotonoha_IOMonitorOperationReset(&videoData->ioOperation);

    SDL_free(videoData);
    *instance = NULL;
}

// Processa a renderização do vídeo
enum Kotonoha_Scene_Status Kotonoha_VideoRenderProcess(
    void *userData,
    SDL_Renderer *render)
{
    struct Kotonoha_videoData *instance =
        (struct Kotonoha_videoData *)userData;

    if (instance == NULL) {
        return KOTONOHA_SCENE_NULL;
    }

    if (instance->useOrsFrameTimeline) {
        const Uint64 sceneMs =
            Kotonoha_timeGet(instance->time);

        if (Kotonoha_OrsSceneMsToFrame(sceneMs) >=
            instance->orsEndFrame) {
            return KOTONOHA_SCENE_COMPLETE;
        }
    }

    bool inRange = false;
    Sint64 diff = 0;

    const Uint64 currentTime =
        Kotonoha_timeGetFromEvent(
            instance->time,
            instance->startTime,
            instance->endTime,
            &inRange,
            &diff
        );

    if (!inRange) {
        return (diff > 0)
            ? KOTONOHA_SCENE_COMPLETE
            : KOTONOHA_SCENE_NULL;
    }

    Kotonoha_VideoEnsureSync(instance, currentTime, false);

    if (instance->ioOperation.stalled) {
        return instance->texture != NULL
            ? KOTONOHA_SCENE_DRAW
            : KOTONOHA_SCENE_WAITING;
    }

    if (instance->pFrame == NULL &&
        !renderVideo(instance, currentTime)) {
        return instance->texture != NULL
            ? KOTONOHA_SCENE_DRAW
            : KOTONOHA_SCENE_WAITING;
    }

    if (currentTime < instance->videoTime) {
        return instance->texture != NULL
            ? KOTONOHA_SCENE_DRAW
            : KOTONOHA_SCENE_WAITING;
    }

#ifdef KOTONOHA_HAS_METALFX
    const float factor = Kotonoha_UpscalingFactor();
    const bool useMetalFX = factor > 1.0f;
#else
    const bool useMetalFX = false;
#endif

    /*
     * Textura de entrada:
     * sempre recebe o frame RGBA original vindo do FFmpeg.
     */
    if (instance->texture == NULL) {
        instance->texture = SDL_CreateTexture(
            render,
            SDL_PIXELFORMAT_RGBA32,
            SDL_TEXTUREACCESS_STREAMING,
            instance->pCodecCtx->width,
            instance->pCodecCtx->height
        );

        if (instance->texture == NULL) {
            SDL_LogError(
                SDL_LOG_CATEGORY_ERROR,
                "Failed to create video texture: %s",
                SDL_GetError()
            );

            return KOTONOHA_SCENE_FATAL_ERROR;
        }

        SDL_SetTextureBlendMode(
            instance->texture,
            SDL_BLENDMODE_BLEND
        );
    }

#ifdef KOTONOHA_HAS_METALFX
    if (useMetalFX && instance->extraTexture == NULL) {
        const int outputWidth = (int)lroundf(
            (float)instance->pCodecCtx->width * factor
        );

        const int outputHeight = (int)lroundf(
            (float)instance->pCodecCtx->height * factor
        );

        if (outputWidth <= instance->pCodecCtx->width ||
            outputHeight <= instance->pCodecCtx->height) {
            SDL_LogError(
                SDL_LOG_CATEGORY_ERROR,
                "Invalid MetalFX video size: %dx%d -> %dx%d",
                instance->pCodecCtx->width,
                instance->pCodecCtx->height,
                outputWidth,
                outputHeight
            );

            return KOTONOHA_SCENE_FATAL_ERROR;
        }

        instance->extraTexture = SDL_CreateTexture(
            render,
            SDL_PIXELFORMAT_BGRA32,
            SDL_TEXTUREACCESS_TARGET,
            outputWidth,
            outputHeight
        );

        if (instance->extraTexture == NULL) {
            SDL_LogError(
                SDL_LOG_CATEGORY_ERROR,
                "Failed to create MetalFX output texture: %s",
                SDL_GetError()
            );

            return KOTONOHA_SCENE_FATAL_ERROR;
        }

        SDL_SetTextureBlendMode(
            instance->extraTexture,
            SDL_BLENDMODE_BLEND
        );
    }
#endif

    if (instance->pFrame == NULL ||
        instance->pFrame->pict_type == AV_PICTURE_TYPE_NONE) {
        return instance->texture != NULL
            ? KOTONOHA_SCENE_DRAW
            : KOTONOHA_SCENE_WAITING;
    }

    AVFrame *cpuFrame =
        getCpuReadableFrame(instance->pFrame);

    if (cpuFrame == NULL) {
        SDL_LogError(
            SDL_LOG_CATEGORY_ERROR,
            "Failed to obtain CPU-readable frame from decoder output"
        );

        return KOTONOHA_SCENE_FATAL_ERROR;
    }

    if (instance->swsCtx == NULL) {
        instance->swsCtx =
            Kotonoha_UtilsFFmpegCreateSwsContext(
                instance->pCodecCtx,
                cpuFrame->format,
                cpuFrame->width,
                cpuFrame->height
            );

        if (instance->swsCtx == NULL) {
            SDL_LogError(
                SDL_LOG_CATEGORY_ERROR,
                "Cannot create sws context"
            );

            if (cpuFrame != instance->pFrame) {
                av_frame_free(&cpuFrame);
            }

            return KOTONOHA_SCENE_FATAL_ERROR;
        }
    }

    Uint8 *buffer = NULL;

    AVFrame *pFrameRGB =
        Kotonoha_UtilsFFmpegSwsConvertFrame(
            instance->swsCtx,
            cpuFrame,
            &buffer,
            cpuFrame->height,
            cpuFrame->width
        );

    if (pFrameRGB == NULL) {
        if (cpuFrame != instance->pFrame) {
            av_frame_free(&cpuFrame);
        }

        return KOTONOHA_SCENE_FATAL_ERROR;
    }

    if (!SDL_UpdateTexture(
            instance->texture,
            NULL,
            pFrameRGB->data[0],
            pFrameRGB->linesize[0]
        )) {
        SDL_LogError(
            SDL_LOG_CATEGORY_RENDER,
            "Failed to update video texture: %s",
            SDL_GetError()
        );

        av_free(buffer);
        av_frame_free(&pFrameRGB);

        if (cpuFrame != instance->pFrame) {
            av_frame_free(&cpuFrame);
        }

        return KOTONOHA_SCENE_FATAL_ERROR;
    }

#ifdef KOTONOHA_HAS_METALFX
    /*
     * MetalFX só executa quando factor > 1.0f.
     * Para factor <= 1.0f, instance->texture permanece como
     * a textura final usada para desenhar o vídeo.
     */
    if (useMetalFX) {
        if (instance->extraTexture == NULL) {
            SDL_LogError(
                SDL_LOG_CATEGORY_ERROR,
                "MetalFX enabled but output texture is null"
            );

            av_free(buffer);
            av_frame_free(&pFrameRGB);

            if (cpuFrame != instance->pFrame) {
                av_frame_free(&cpuFrame);
            }

            return KOTONOHA_SCENE_FATAL_ERROR;
        }

        if (!Kotonoha_MetalFXUpscaleSDLTextureInto(
                render,
                instance->texture,
                instance->extraTexture
            )) {
            SDL_LogError(
                SDL_LOG_CATEGORY_ERROR,
                "Failed to upscale video texture using MetalFX: %s",
                SDL_GetError()
            );

            av_free(buffer);
            av_frame_free(&pFrameRGB);

            if (cpuFrame != instance->pFrame) {
                av_frame_free(&cpuFrame);
            }

            return KOTONOHA_SCENE_FATAL_ERROR;
        }
    }
#endif

    av_free(buffer);
    av_frame_free(&pFrameRGB);

    if (cpuFrame != instance->pFrame) {
        av_frame_free(&cpuFrame);
    }

    av_frame_free(&instance->pFrame);

    instance->lastTime = currentTime;

    return KOTONOHA_SCENE_DRAW;
}