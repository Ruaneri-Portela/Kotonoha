#include <Kotonoha/utils/VoicePcm.h>
#include <Kotonoha/utils/FFmpeg.h>

#include <SDL3/SDL.h>
#include <libavcodec/avcodec.h>
#include <libswresample/swresample.h>

#include <stdint.h>

struct Kotonoha_PcmBuffer {
	int16_t* samples;
	size_t count;
	size_t capacity;
};

static bool Kotonoha_PcmReserve(struct Kotonoha_PcmBuffer* buffer,
	size_t additional) {
	if (additional > SIZE_MAX - buffer->count) {
		return false;
	}
	const size_t required = buffer->count + additional;
	if (required <= buffer->capacity) {
		return true;
	}

	size_t capacity = buffer->capacity > 0 ? buffer->capacity : 4096;
	while (capacity < required) {
		if (capacity > SIZE_MAX / 2) {
			capacity = required;
			break;
		}
		capacity *= 2;
	}
	if (capacity > SIZE_MAX / sizeof(*buffer->samples)) {
		return false;
	}

	int16_t* samples = (int16_t*)SDL_realloc(
		buffer->samples, capacity * sizeof(*buffer->samples));
	if (samples == NULL) {
		return false;
	}
	buffer->samples = samples;
	buffer->capacity = capacity;
	return true;
}

static bool Kotonoha_PcmAppendConverted(struct SwrContext* resampler,
	const uint8_t** input, int inputSamples, struct Kotonoha_PcmBuffer* buffer) {
	int capacity = swr_get_out_samples(resampler, inputSamples);
	if (capacity < 0) {
		return false;
	}
	if (capacity == 0) {
		return true;
	}

	if (!Kotonoha_PcmReserve(buffer, (size_t)capacity)) {
		return false;
	}

	uint8_t* output = (uint8_t*)(buffer->samples + buffer->count);
	const int converted = swr_convert(resampler, &output, capacity,
		input, inputSamples);
	if (converted < 0) {
		return false;
	}
	buffer->count += (size_t)converted;
	return true;
}

static bool Kotonoha_DrainVoiceDecoder(AVCodecContext* codec,
	AVFrame* frame, struct SwrContext* resampler,
	struct Kotonoha_PcmBuffer* output) {
	for (;;) {
		const int result = avcodec_receive_frame(codec, frame);
		if (result == AVERROR(EAGAIN) || result == AVERROR_EOF) {
			return true;
		}
		if (result < 0) {
			return false;
		}

		const bool converted = Kotonoha_PcmAppendConverted(resampler,
			(const uint8_t**)frame->extended_data, frame->nb_samples, output);
		av_frame_unref(frame);
		if (!converted) {
			return false;
		}
	}
}

static bool Kotonoha_FlushVoiceResampler(struct SwrContext* resampler,
	struct Kotonoha_PcmBuffer* output) {
	for (;;) {
		const int capacity = swr_get_out_samples(resampler, 0);
		if (capacity < 0) {
			return false;
		}
		if (capacity == 0) {
			return true;
		}
		if (!Kotonoha_PcmReserve(output, (size_t)capacity)) {
			return false;
		}

		uint8_t* destination = (uint8_t*)(output->samples + output->count);
		const int converted = swr_convert(
			resampler, &destination, capacity, NULL, 0);
		if (converted < 0) {
			return false;
		}
		if (converted == 0) {
			return true;
		}
		output->count += (size_t)converted;
	}
}

bool Kotonoha_DecodeVoicePcm(const char* path, int16_t** samples,
	size_t* sampleCount, Kotonoha_IOMonitor* ioMonitor) {
	if (samples == NULL || sampleCount == NULL) {
		SDL_LogError(SDL_LOG_CATEGORY_AUDIO,
			"Voice PCM decoder received null output pointers");
		return false;
	}
	*samples = NULL;
	*sampleCount = 0;
	if (path == NULL || *path == '\0') {
		SDL_LogError(SDL_LOG_CATEGORY_AUDIO,
			"Voice PCM decoder received an empty path");
		return false;
	}

	AVFormatContext* format = NULL;
	AVCodecContext* codec = NULL;
	SwrContext* resampler = NULL;
	AVFrame* frame = NULL;
	AVPacket* packet = NULL;
	struct Kotonoha_PcmBuffer pcm = { NULL, 0, 0 };
	Kotonoha_IOMonitorOperation ioOperation;
	Kotonoha_IOMonitorOperationInit(&ioOperation, ioMonitor);
	const char* failure = NULL;
	int stream = -1;
	int readResult = 0;
	bool success = false;
	bool ignoredMissing = false;

	if (!Kotonoha_UtilsFFmpegLoadMonitored(path, &format, &codec,
			AVMEDIA_TYPE_AUDIO, &stream, 0, NULL, &ioOperation)) {
		failure = "open voice file";
		goto cleanup;
	}

	{
		AVChannelLayout mono;
		av_channel_layout_default(&mono, 1);
		const int result = swr_alloc_set_opts2(
			&resampler, &mono, AV_SAMPLE_FMT_S16, 44100,
			&codec->ch_layout, codec->sample_fmt,
			codec->sample_rate, 0, NULL);
		av_channel_layout_uninit(&mono);
		if (result < 0 || resampler == NULL || swr_init(resampler) < 0) {
			failure = "initialize voice PCM conversion";
			goto cleanup;
		}
	}

	frame = av_frame_alloc();
	packet = av_packet_alloc();
	if (frame == NULL || packet == NULL) {
		failure = "allocate voice PCM buffers";
		goto cleanup;
	}

	for (;;) {
		readResult = Kotonoha_UtilsFFmpegReadFrame(format, packet);
		if (readResult == AVERROR_EXIT &&
			SDL_GetAtomicInt(&ioOperation.cancelled) == 0) {
			continue;
		}
		if (readResult < 0) {
			break;
		}
		if (packet->stream_index == stream) {
			int result = avcodec_send_packet(codec, packet);
			if (result == AVERROR(EAGAIN)) {
				if (!Kotonoha_DrainVoiceDecoder(
						codec, frame, resampler, &pcm)) {
					av_packet_unref(packet);
					failure = "decode voice audio packets";
					goto cleanup;
				}
				result = avcodec_send_packet(codec, packet);
			}
			if (result < 0 || !Kotonoha_DrainVoiceDecoder(
					codec, frame, resampler, &pcm)) {
				av_packet_unref(packet);
				failure = "decode voice audio packets";
				goto cleanup;
			}
		}
		av_packet_unref(packet);
	}

	if (readResult != AVERROR_EOF) {
		failure = "read voice audio packets";
		goto cleanup;
	}

	{
		int result = avcodec_send_packet(codec, NULL);
		if (result == AVERROR(EAGAIN)) {
			if (!Kotonoha_DrainVoiceDecoder(
					codec, frame, resampler, &pcm)) {
				failure = "flush voice audio decoder";
				goto cleanup;
			}
			result = avcodec_send_packet(codec, NULL);
		}
		if ((result < 0 && result != AVERROR_EOF) ||
			!Kotonoha_DrainVoiceDecoder(codec, frame, resampler, &pcm) ||
			!Kotonoha_FlushVoiceResampler(resampler, &pcm)) {
			failure = "flush voice audio decoder";
			goto cleanup;
		}
	}

	if (pcm.count == 0) {
		failure = "decode voice audio samples";
		goto cleanup;
	}
	success = true;

cleanup:
	ignoredMissing = Kotonoha_IOMonitorIsIgnoredMissing(&ioOperation);
	av_packet_free(&packet);
	av_frame_free(&frame);
	swr_free(&resampler);
	avcodec_free_context(&codec);
	avformat_close_input(&format);
	Kotonoha_IOMonitorOperationReset(&ioOperation);

	if (!success) {
		SDL_free(pcm.samples);
		if (!ignoredMissing) {
			SDL_LogWarn(SDL_LOG_CATEGORY_AUDIO,
				"Could not %s for '%s'",
				failure != NULL ? failure : "decode voice PCM", path);
		}
		return false;
	}

	*samples = pcm.samples;
	*sampleCount = pcm.count;
	return true;
}
