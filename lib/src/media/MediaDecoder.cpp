#include <Kotonoha/media/MediaDecoder.hpp>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/channel_layout.h>
#include <libavutil/mem.h>
#include <libswresample/swresample.h>
#include <libswscale/swscale.h>
}

#include <algorithm>
#include <cstring>
#include <memory>
#include <stdexcept>
#include <string>

namespace Kotonoha {
namespace {

constexpr std::size_t maximumDecodedAudio = 1024ull * 1024 * 1024;

void check(int result, const char *operation) {
  if (result >= 0)
    return;
  char message[AV_ERROR_MAX_STRING_SIZE]{};
  av_strerror(result, message, sizeof(message));
  throw std::runtime_error(std::string(operation) + ": " + message);
}

struct FrameDeleter {
  void operator()(AVFrame *frame) const { av_frame_free(&frame); }
};
struct PacketDeleter {
  void operator()(AVPacket *packet) const { av_packet_free(&packet); }
};
struct CodecDeleter {
  void operator()(AVCodecContext *codec) const { avcodec_free_context(&codec); }
};
using Frame = std::unique_ptr<AVFrame, FrameDeleter>;
using Packet = std::unique_ptr<AVPacket, PacketDeleter>;
using Codec = std::unique_ptr<AVCodecContext, CodecDeleter>;

// libavformat over a byte vector (no file or network protocol involved).
class Input {
  const std::vector<Uint8> &bytes;
  std::size_t offset = 0;
  AVIOContext *io = nullptr;

  static int readBytes(void *opaque, uint8_t *buffer, int size) {
    auto &self = *static_cast<Input *>(opaque);
    const auto count = std::min<std::size_t>(std::size_t(size),
                                             self.bytes.size() - self.offset);
    if (count == 0)
      return AVERROR_EOF;
    std::memcpy(buffer, self.bytes.data() + self.offset, count);
    self.offset += count;
    return int(count);
  }

  static int64_t seekBytes(void *opaque, int64_t position, int whence) {
    auto &self = *static_cast<Input *>(opaque);
    const auto size = int64_t(self.bytes.size());
    if (whence & AVSEEK_SIZE)
      return size;
    int64_t base = 0;
    switch (whence & ~AVSEEK_FORCE) {
    case SEEK_SET: break;
    case SEEK_CUR: base = int64_t(self.offset); break;
    case SEEK_END: base = size; break;
    default: return AVERROR(EINVAL);
    }
    const int64_t target = base + position;
    if (target < 0 || target > size)
      return AVERROR(EINVAL);
    self.offset = std::size_t(target);
    return target;
  }

public:
  AVFormatContext *format = nullptr;

  explicit Input(const std::vector<Uint8> &data) : bytes(data) {
    if (bytes.empty())
      throw std::runtime_error("Media is empty");
    constexpr int bufferSize = 64 * 1024;
    auto *buffer = static_cast<uint8_t *>(av_malloc(bufferSize));
    if (!buffer)
      throw std::bad_alloc();
    io = avio_alloc_context(buffer, bufferSize, 0, this, readBytes, nullptr,
                            seekBytes);
    if (!io) {
      av_free(buffer);
      throw std::bad_alloc();
    }
    format = avformat_alloc_context();
    if (!format) {
      release();
      throw std::bad_alloc();
    }
    format->pb = io;
    format->flags |= AVFMT_FLAG_CUSTOM_IO;
    const int opened = avformat_open_input(&format, nullptr, nullptr, nullptr);
    if (opened < 0) {
      release();
      check(opened, "Open media");
    }
    const int probed = avformat_find_stream_info(format, nullptr);
    if (probed < 0) {
      release();
      check(probed, "Read media streams");
    }
  }
  ~Input() { release(); }
  Input(const Input &) = delete;
  Input &operator=(const Input &) = delete;

  void release() {
    avformat_close_input(&format);
    if (io) {
      av_freep(&io->buffer);
      avio_context_free(&io);
    }
  }

  double origin() const {
    return format->start_time == AV_NOPTS_VALUE
               ? 0
               : format->start_time / double(AV_TIME_BASE);
  }

  Codec open(AVMediaType type, int &index) const {
    const AVCodec *decoder = nullptr;
    index = av_find_best_stream(format, type, -1, -1, &decoder, 0);
    check(index, type == AVMEDIA_TYPE_VIDEO ? "Find picture stream"
                                            : "Find audio stream");
    Codec context(avcodec_alloc_context3(decoder));
    if (!context)
      throw std::bad_alloc();
    check(avcodec_parameters_to_context(context.get(),
                                        format->streams[index]->codecpar),
          "Read codec parameters");
    check(avcodec_open2(context.get(), decoder, nullptr), "Open codec");
    return context;
  }
};

// Feeds every packet of `index` to `codec`, calling `onFrame` for each
// decoded frame until it returns false or the stream ends.
template <typename OnFrame>
void decodeStream(Input &input, AVCodecContext &codec, int index,
                  OnFrame onFrame) {
  Packet packet(av_packet_alloc());
  Frame frame(av_frame_alloc());
  if (!packet || !frame)
    throw std::bad_alloc();
  bool more = true;
  auto receive = [&] {
    for (;;) {
      const int result = avcodec_receive_frame(&codec, frame.get());
      if (result == AVERROR(EAGAIN) || result == AVERROR_EOF)
        return;
      check(result, "Decode media");
      more = onFrame(*frame);
      av_frame_unref(frame.get());
      if (!more)
        return;
    }
  };
  while (more) {
    const int result = av_read_frame(input.format, packet.get());
    if (result == AVERROR_EOF)
      break;
    check(result, "Read media packet");
    if (packet->stream_index == index) {
      int sent = avcodec_send_packet(&codec, packet.get());
      if (sent == AVERROR(EAGAIN)) {
        receive();
        sent = more ? avcodec_send_packet(&codec, packet.get()) : 0;
      }
      av_packet_unref(packet.get());
      check(sent, "Submit media packet");
      receive();
    } else {
      av_packet_unref(packet.get());
    }
  }
  if (more) {
    const int flushed = avcodec_send_packet(&codec, nullptr);
    if (flushed != AVERROR_EOF)
      check(flushed, "Flush decoder");
    receive();
  }
}

} // namespace

namespace MediaDecoder {

SurfacePtr decodePicture(const std::vector<Uint8> &bytes, double seconds) {
  if (!(seconds >= 0))
    throw std::invalid_argument("Picture time must be non-negative");
  Input input(bytes);
  int index = -1;
  Codec codec = input.open(AVMEDIA_TYPE_VIDEO, index);
  const AVStream *stream = input.format->streams[index];
  const double origin = input.origin();
  if (seconds > 0) {
    const auto target = int64_t((seconds + origin) * AV_TIME_BASE);
    if (avformat_seek_file(input.format, -1, INT64_MIN, target, target, 0) >= 0)
      avcodec_flush_buffers(codec.get());
  }
  Frame best(av_frame_alloc());
  if (!best)
    throw std::bad_alloc();
  bool found = false;
  decodeStream(input, *codec, index, [&](AVFrame &frame) {
    av_frame_unref(best.get());
    av_frame_move_ref(best.get(), &frame);
    found = true;
    if (best->best_effort_timestamp == AV_NOPTS_VALUE)
      return seconds > 0;
    const double at =
        best->best_effort_timestamp * av_q2d(stream->time_base) - origin;
    return at + 1e-3 < seconds;
  });
  if (!found)
    throw std::runtime_error("Media has no decodable picture");
  SurfacePtr surface(
      SDL_CreateSurface(best->width, best->height, SDL_PIXELFORMAT_RGBA32));
  if (!surface)
    throw std::runtime_error(SDL_GetError());
  SwsContext *scaler = sws_getContext(
      best->width, best->height, AVPixelFormat(best->format), best->width,
      best->height, AV_PIX_FMT_RGBA, SWS_BILINEAR, nullptr, nullptr, nullptr);
  if (!scaler)
    throw std::runtime_error("Cannot create picture converter");
  uint8_t *planes[]{static_cast<uint8_t *>(surface->pixels)};
  const int strides[]{surface->pitch};
  const int rows = sws_scale(scaler, best->data, best->linesize, 0,
                             best->height, planes, strides);
  sws_freeContext(scaler);
  check(rows, "Convert picture");
  return surface;
}

AudioClip decodeAudio(const std::vector<Uint8> &bytes) {
  Input input(bytes);
  int index = -1;
  Codec codec = input.open(AVMEDIA_TYPE_AUDIO, index);
  AVChannelLayout source{};
  if (codec->ch_layout.order == AV_CHANNEL_ORDER_UNSPEC)
    av_channel_layout_default(&source, codec->ch_layout.nb_channels);
  else
    check(av_channel_layout_copy(&source, &codec->ch_layout),
          "Read channel layout");
  AVChannelLayout target{};
  if (source.nb_channels > 8)
    target = AV_CHANNEL_LAYOUT_STEREO;
  else
    check(av_channel_layout_copy(&target, &source), "Copy channel layout");
  const int rate = codec->sample_rate;
  SwrContext *resampler = nullptr;
  const int configured = swr_alloc_set_opts2(
      &resampler, &target, AV_SAMPLE_FMT_FLT, rate, &source,
      codec->sample_fmt, rate, 0, nullptr);
  av_channel_layout_uninit(&source);
  std::unique_ptr<SwrContext, void (*)(SwrContext *)> owned(
      resampler, [](SwrContext *context) { swr_free(&context); });
  check(configured, "Configure audio conversion");
  check(swr_init(resampler), "Initialize audio conversion");

  AudioClip clip;
  clip.spec = {SDL_AUDIO_F32, target.nb_channels, rate};
  const std::size_t frameBytes = sizeof(float) * std::size_t(target.nb_channels);
  av_channel_layout_uninit(&target);
  auto convert = [&](const uint8_t **data, int samples) {
    const int capacity = swr_get_out_samples(resampler, samples);
    check(capacity, "Size converted audio");
    if (capacity == 0)
      return 0;
    const std::size_t used = clip.data.size();
    if (used + std::size_t(capacity) * frameBytes > maximumDecodedAudio)
      throw std::runtime_error("Decoded audio exceeds 1 GiB");
    clip.data.resize(used + std::size_t(capacity) * frameBytes);
    uint8_t *output = clip.data.data() + used;
    const int count = swr_convert(resampler, &output, capacity, data, samples);
    check(count, "Convert audio");
    clip.data.resize(used + std::size_t(count) * frameBytes);
    return count;
  };
  decodeStream(input, *codec, index, [&](AVFrame &frame) {
    convert(const_cast<const uint8_t **>(frame.extended_data),
            frame.nb_samples);
    return true;
  });
  while (convert(nullptr, 0) > 0) {
  }
  if (clip.data.empty())
    throw std::runtime_error("Media has no decodable audio");
  return clip;
}

double duration(const std::vector<Uint8> &bytes) {
  Input input(bytes);
  return input.format->duration == AV_NOPTS_VALUE
             ? -1
             : input.format->duration / double(AV_TIME_BASE);
}

} // namespace MediaDecoder
} // namespace Kotonoha
