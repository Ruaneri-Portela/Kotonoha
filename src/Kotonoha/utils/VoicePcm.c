#include <Kotonoha/utils/FFmpeg.h>
#include <Kotonoha/utils/VoicePcm.h>
#include <SDL3/SDL.h>

typedef struct Kotonoha_PcmBuffer {
  int16_t *samples;
  size_t count;
  size_t capacity;
} Kotonoha_PcmBuffer;

static bool KotonohaPcmReserve(Kotonoha_PcmBuffer *buffer, size_t additional) {
  const size_t required = buffer->count + additional;
  if (required <= buffer->capacity) return true;
  size_t capacity = buffer->capacity > 0 ? buffer->capacity : 16384;
  while (capacity < required) capacity *= 2;
  int16_t *samples =
      (int16_t *)SDL_realloc(buffer->samples, capacity * sizeof(int16_t));
  if (!samples) return false;
  buffer->samples = samples;
  buffer->capacity = capacity;
  return true;
}

static bool KotonohaPcmDecodeLoaded(AVFormatContext *format,
                                    AVCodecContext *codec, int stream,
                                    Sint16 **samples, size_t *sampleCount) {
  SwrContext *resampler =
      KotonohaFFmpegCreateAudioResampler(codec, 1, 44100, AV_SAMPLE_FMT_S16);
  AVFrame *frame = av_frame_alloc();
  AVPacket *packet = av_packet_alloc();
  Kotonoha_PcmBuffer pcm = {NULL, 0, 0};
  if (!resampler || !frame || !packet) goto cleanup;

  while (KotonohaFFmpegReadFrame(format, packet) >= 0) {
    if (packet->stream_index == stream && avcodec_send_packet(codec, packet) >= 0) {
      while (avcodec_receive_frame(codec, frame) == 0) {
        Uint8 *converted = NULL;
        int size = 0;
        const int convertedSamples = KotonohaFFmpegConvertAudioFrame(
            resampler, frame, 1, AV_SAMPLE_FMT_S16, &converted, &size);
        if (convertedSamples > 0 && converted) {
          if (!KotonohaPcmReserve(&pcm, (size_t)convertedSamples)) {
            SDL_free(converted);
            av_frame_unref(frame);
            goto cleanup;
          }
          SDL_memcpy(pcm.samples + pcm.count, converted, (size_t)size);
          pcm.count += (size_t)convertedSamples;
          SDL_free(converted);
        }
        av_frame_unref(frame);
      }
    }
    av_packet_unref(packet);
  }

cleanup:
  if (packet) av_packet_free(&packet);
  if (frame) av_frame_free(&frame);
  if (resampler) swr_free(&resampler);
  if (pcm.count == 0) {
    SDL_free(pcm.samples);
    return false;
  }
  *samples = pcm.samples;
  *sampleCount = pcm.count;
  return true;
}

bool KotonohaPcmDecodeVoiceInput(const Kotonoha_FFmpegInput *input,
                                 Sint16 **samples, size_t *sampleCount) {
  if (!input || !samples || !sampleCount) return false;
  *samples = NULL;
  *sampleCount = 0;
  AVFormatContext *format = NULL;
  AVCodecContext *codec = NULL;
  Kotonoha_FFmpegInputContext *inputContext = NULL;
  int stream = -1;
  if (!KotonohaFFmpegLoadInput(input, &format, &codec, AVMEDIA_TYPE_AUDIO,
                               &stream, 0, NULL, &inputContext)) {
    return false;
  }
  const bool okay =
      KotonohaPcmDecodeLoaded(format, codec, stream, samples, sampleCount);
  if (codec) avcodec_free_context(&codec);
  KotonohaFFmpegCloseInput(&format, &inputContext);
  return okay;
}

bool KotonohaPcmDecodeVoice(const char *path, Sint16 **samples,
                            size_t *sampleCount) {
  if (!path || !*path || !samples || !sampleCount) return false;
  *samples = NULL;
  *sampleCount = 0;

  Kotonoha_FFmpegInput input;
  const int resolved = KotonohaFFmpegResolveInput(path, &input);
  if (resolved > 0) return KotonohaPcmDecodeVoiceInput(&input, samples, sampleCount);
  if (resolved < 0) return false;

  AVFormatContext *format = NULL;
  AVCodecContext *codec = NULL;
  int stream = -1;
  if (!KotonohaFFmpegLoad(path, &format, &codec, AVMEDIA_TYPE_AUDIO, &stream, 0,
                          NULL)) {
    return false;
  }
  const bool okay =
      KotonohaPcmDecodeLoaded(format, codec, stream, samples, sampleCount);
  if (codec) avcodec_free_context(&codec);
  if (format) avformat_close_input(&format);
  return okay;
}
