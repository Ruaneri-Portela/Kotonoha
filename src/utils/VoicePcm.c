#include <Kotonoha/utils/FFmpeg.h>
#include <Kotonoha/utils/VoicePcm.h>
#include <SDL3/SDL.h>

typedef struct Kotonoha_PcmBuffer {
  int16_t *samples;
  size_t count;
  size_t capacity;
} Kotonoha_PcmBuffer;

static inline bool KotonohaPcmReserve(Kotonoha_PcmBuffer *buffer,
                                      size_t additional) {
  const size_t required = buffer->count + additional;
  if (required <= buffer->capacity)
    return true;

  size_t capacity = buffer->capacity > 0 ? buffer->capacity : 16384;
  while (capacity < required)
    capacity *= 2;

  int16_t *samples =
      (int16_t *)SDL_realloc(buffer->samples, capacity * sizeof(int16_t));
  if (!samples)
    return false;

  buffer->samples = samples;
  buffer->capacity = capacity;
  return true;
}

bool KotonohaPcmDecodeVoice(const char *path, Sint16 **samples,
                            size_t *sampleCount,
                            Kotonoha_IOMonitor *ioMonitor) {
  if (!samples || !sampleCount || !path || *path == '\0')
    return false;

  *samples = NULL;
  *sampleCount = 0;

  AVFormatContext *format = NULL;
  AVCodecContext *codec = NULL;
  SwrContext *resampler = NULL;
  AVFrame *frame = NULL;
  AVPacket *packet = NULL;
  Kotonoha_PcmBuffer pcm = {NULL, 0, 0};
  Kotonoha_IOMonitorOperation ioOperation;
  KotonohaIOMonitorOperationInit(&ioOperation, ioMonitor);
  int stream = -1;

  if (!KotonohaFFmpegLoadMonitored(path, &format, &codec, AVMEDIA_TYPE_AUDIO,
                                   &stream, 0, NULL, &ioOperation)) {
    return false;
  }

  resampler =
      KotonohaFFmpegCreateAudioResampler(codec, 1, 44100, AV_SAMPLE_FMT_S16);
  if (!resampler)
    goto cleanup;

  frame = av_frame_alloc();
  packet = av_packet_alloc();
  if (!frame || !packet)
    goto cleanup;

  while (KotonohaFFmpegReadFrame(format, packet) >= 0) {
    if (packet->stream_index == stream) {
      if (avcodec_send_packet(codec, packet) >= 0) {
        while (avcodec_receive_frame(codec, frame) == 0) {
          Uint8 *convertedBuf = NULL;
          int bufSize = 0;
          const int samplesConverted = KotonohaFFmpegConvertAudioFrame(
              resampler, frame, 1, AV_SAMPLE_FMT_S16, &convertedBuf, &bufSize);

          if (samplesConverted > 0 && convertedBuf) {
            if (KotonohaPcmReserve(&pcm, (size_t)samplesConverted)) {
              SDL_memcpy(pcm.samples + pcm.count, convertedBuf, bufSize);
              pcm.count += (size_t)samplesConverted;
            }
            SDL_free(convertedBuf);
          }
          av_frame_unref(frame);
        }
      }
    }
    av_packet_unref(packet);
  }

  *samples = pcm.samples;
  *sampleCount = pcm.count;

cleanup:
  if (packet)
    av_packet_free(&packet);
  if (frame)
    av_frame_free(&frame);
  if (resampler)
    swr_free(&resampler);
  if (codec)
    avcodec_free_context(&codec);
  if (format)
    avformat_close_input(&format);
  KotonohaIOMonitorOperationReset(&ioOperation);

  return pcm.count > 0;
}