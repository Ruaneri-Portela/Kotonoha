#include <Kotonoha/media/SaveImage.hpp>

#include <Kotonoha/core/SDL.hpp>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/error.h>
#include <libavutil/opt.h>
#include <libswscale/swscale.h>
}

#include <memory>

namespace Kotonoha {
namespace {

struct FormatDeleter {
  void operator()(AVFormatContext *context) const {
    if (context->pb)
      avio_closep(&context->pb);
    avformat_free_context(context);
  }
};
struct CodecDeleter {
  void operator()(AVCodecContext *context) const {
    avcodec_free_context(&context);
  }
};
struct FrameDeleter {
  void operator()(AVFrame *frame) const { av_frame_free(&frame); }
};
struct PacketDeleter {
  void operator()(AVPacket *packet) const { av_packet_free(&packet); }
};
struct ScalerDeleter {
  void operator()(SwsContext *context) const { sws_freeContext(context); }
};

using Format = std::unique_ptr<AVFormatContext, FormatDeleter>;
using Codec = std::unique_ptr<AVCodecContext, CodecDeleter>;
using Frame = std::unique_ptr<AVFrame, FrameDeleter>;
using Packet = std::unique_ptr<AVPacket, PacketDeleter>;
using Scaler = std::unique_ptr<SwsContext, ScalerDeleter>;

void logFFmpegError(int result, const char *operation, const char *path) {
  char message[AV_ERROR_MAX_STRING_SIZE]{};
  av_strerror(result, message, sizeof(message));
  SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
               "SaveImage: %s failed for '%s': %s", operation,
               path ? path : "(null)", message);
}

struct SurfaceLock {
  SDL_Surface *surface = nullptr;
  bool locked = false;

  explicit SurfaceLock(SDL_Surface *value) : surface(value) {
    locked = SDL_MUSTLOCK(surface) && SDL_LockSurface(surface);
  }
  ~SurfaceLock() {
    if (locked)
      SDL_UnlockSurface(surface);
  }
  bool ok() const { return !SDL_MUSTLOCK(surface) || locked; }
};

} // namespace

bool SaveImage(SDL_Surface *surface, const char *path, ImageFormat format) {
  if (!surface || !path || surface->w <= 0 || surface->h <= 0 ||
      surface->pitch <= 0 || !surface->pixels) {
    SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                 "SaveImage: invalid surface or path");
    return false;
  }

  // SDL handles palettes, color keys and every packed format; swscale then
  // performs the actual codec pixel conversion from a known RGBA source.
  SurfacePtr rgba(SDL_ConvertSurface(surface, SDL_PIXELFORMAT_RGBA32));
  if (!rgba || rgba->w <= 0 || rgba->h <= 0 || rgba->pitch <= 0) {
    SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                 "SaveImage: cannot normalize surface to RGBA: %s",
                 SDL_GetError());
    return false;
  }

  const AVCodecID codecId =
      format == ImageFormat::Jpeg ? AV_CODEC_ID_MJPEG : AV_CODEC_ID_PNG;
  // MJPEG uses full-range YUV; PNG keeps the straight RGBA alpha.
  const AVPixelFormat targetFormat =
      format == ImageFormat::Jpeg ? AV_PIX_FMT_YUV420P : AV_PIX_FMT_RGBA;

  const AVCodec *codec = avcodec_find_encoder(codecId);
  if (!codec) {
    SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                 "SaveImage: FFmpeg encoder %d is unavailable",
                 static_cast<int>(codecId));
    return false;
  }

  AVFormatContext *rawFormat = nullptr;
  int result =
      avformat_alloc_output_context2(&rawFormat, nullptr, "image2", path);
  if (result < 0 || !rawFormat) {
    logFFmpegError(result, "output context allocation", path);
    return false;
  }
  Format output(rawFormat);
  // image2 would otherwise treat the filename as a numbered sequence.
  result = av_opt_set(output->priv_data, "update", "1", 0);
  if (result < 0) {
    logFFmpegError(result, "image2 update option", path);
    return false;
  }

  AVStream *stream = avformat_new_stream(output.get(), codec);
  if (!stream) {
    SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                 "SaveImage: cannot create output stream for '%s'", path);
    return false;
  }

  Codec encoder(avcodec_alloc_context3(codec));
  if (!encoder) {
    SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                 "SaveImage: cannot allocate encoder context for '%s'", path);
    return false;
  }
  encoder->width = rgba->w;
  encoder->height = rgba->h;
  encoder->pix_fmt = targetFormat;
  encoder->time_base = {1, 1};
  if (format == ImageFormat::Jpeg)
    encoder->color_range = AVCOL_RANGE_JPEG;

  result = avcodec_open2(encoder.get(), codec, nullptr);
  if (result < 0) {
    logFFmpegError(result, "encoder initialization", path);
    return false;
  }
  result = avcodec_parameters_from_context(stream->codecpar, encoder.get());
  if (result < 0) {
    logFFmpegError(result, "stream parameter copy", path);
    return false;
  }
  stream->time_base = encoder->time_base;

  result = avio_open(&output->pb, path, AVIO_FLAG_WRITE);
  if (result < 0) {
    logFFmpegError(result, "file open", path);
    return false;
  }
  result = avformat_write_header(output.get(), nullptr);
  if (result < 0) {
    logFFmpegError(result, "container header", path);
    return false;
  }

  Frame frame(av_frame_alloc());
  if (!frame) {
    SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                 "SaveImage: cannot allocate frame for '%s'", path);
    return false;
  }
  frame->format = targetFormat;
  frame->width = rgba->w;
  frame->height = rgba->h;
  frame->pts = 0;
  result = av_frame_get_buffer(frame.get(), 32);
  if (result < 0) {
    logFFmpegError(result, "frame buffer allocation", path);
    return false;
  }

  const int width = rgba->w, height = rgba->h;
  Scaler scaler(sws_getContext(width, height, AV_PIX_FMT_RGBA, width, height,
                               targetFormat, SWS_BILINEAR, nullptr, nullptr,
                               nullptr));
  if (!scaler) {
    SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                 "SaveImage: cannot create RGBA converter for '%s'", path);
    return false;
  }

  SurfaceLock lock(rgba.get());
  if (!lock.ok()) {
    SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                 "SaveImage: cannot lock source surface: %s", SDL_GetError());
    return false;
  }
  const uint8_t *sourcePlanes[]{static_cast<const uint8_t *>(rgba->pixels)};
  const int sourceStrides[]{rgba->pitch};
  result = sws_scale(scaler.get(), sourcePlanes, sourceStrides, 0, height,
                     frame->data, frame->linesize);
  if (result != height) {
    SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                 "SaveImage: pixel conversion produced %d of %d rows", result,
                 height);
    return false;
  }

  result = avcodec_send_frame(encoder.get(), frame.get());
  if (result < 0) {
    logFFmpegError(result, "frame encoding", path);
    return false;
  }

  Packet packet(av_packet_alloc());
  if (!packet) {
    SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                 "SaveImage: cannot allocate packet for '%s'", path);
    return false;
  }
  bool wrotePacket = false;
  for (;;) {
    result = avcodec_receive_packet(encoder.get(), packet.get());
    if (result == AVERROR(EAGAIN) || result == AVERROR_EOF)
      break;
    if (result < 0) {
      logFFmpegError(result, "encoded packet read", path);
      return false;
    }
    av_packet_rescale_ts(packet.get(), encoder->time_base, stream->time_base);
    result = av_interleaved_write_frame(output.get(), packet.get());
    av_packet_unref(packet.get());
    if (result < 0) {
      logFFmpegError(result, "packet muxing", path);
      return false;
    }
    wrotePacket = true;
  }
  if (!wrotePacket) {
    SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                 "SaveImage: encoder produced no image for '%s'", path);
    return false;
  }
  result = av_write_trailer(output.get());
  if (result < 0) {
    logFFmpegError(result, "container trailer", path);
    return false;
  }
  // A successful write must also report a failed flush or file close.
  // FormatDeleter remains responsible for close on every earlier error path.
  result = avio_closep(&output->pb);
  if (result < 0) {
    logFFmpegError(result, "output file close", path);
    return false;
  }
  return true;
}

bool SavePng(SDL_Surface *surface, const char *path) {
  return SaveImage(surface, path, ImageFormat::Png);
}

bool SaveJpeg(SDL_Surface *surface, const char *path) {
  return SaveImage(surface, path, ImageFormat::Jpeg);
}

} // namespace Kotonoha
