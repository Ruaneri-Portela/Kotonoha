#include <Kotonoha/workers/EmbeddedSubtitleExtractor.hpp>

#include <Kotonoha/core/Log.hpp>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavformat/avio.h>
#include <libavutil/error.h>
}

#include <algorithm>
#include <cinttypes>
#include <cmath>
#include <cstring>
#include <iomanip>
#include <sstream>

namespace Kotonoha {
namespace workers {
namespace {

struct ByteSource {
  const Uint8 *data = nullptr;
  size_t size = 0;
  size_t position = 0;
};

int readBytes(void *opaque, Uint8 *destination, int size) {
  auto *source = static_cast<ByteSource *>(opaque);
  if (!source || source->position >= source->size)
    return AVERROR_EOF;
  const size_t available = source->size - source->position;
  const size_t count = std::min<size_t>(size_t(size), available);
  std::memcpy(destination, source->data + source->position, count);
  source->position += count;
  return static_cast<int>(count);
}

int64_t seekBytes(void *opaque, int64_t offset, int whence) {
  auto *source = static_cast<ByteSource *>(opaque);
  if (!source)
    return AVERROR(EINVAL);
  if (whence == AVSEEK_SIZE)
    return static_cast<int64_t>(source->size);
  whence &= ~AVSEEK_FORCE;
  int64_t base = 0;
  if (whence == SEEK_SET)
    base = 0;
  else if (whence == SEEK_CUR)
    base = static_cast<int64_t>(source->position);
  else if (whence == SEEK_END)
    base = static_cast<int64_t>(source->size);
  else
    return AVERROR(EINVAL);
  const int64_t target = base + offset;
  if (target < 0 || target > static_cast<int64_t>(source->size))
    return AVERROR(EINVAL);
  source->position = static_cast<size_t>(target);
  return target;
}

std::string timestamp(double seconds) {
  const auto cs = static_cast<long long>(std::llround(seconds * 100));
  std::ostringstream result;
  result << cs / 360000 << ':' << std::setfill('0') << std::setw(2)
         << cs / 6000 % 60 << ':' << std::setw(2) << cs / 100 % 60 << '.'
         << std::setw(2) << cs % 100;
  return result.str();
}

std::string escapeAss(const char *text) {
  std::string result;
  if (!text)
    return result;
  for (const char *cursor = text; *cursor; ++cursor) {
    if (*cursor == '\r')
      continue;
    if (*cursor == '\n')
      result += "\\N";
    else {
      if (*cursor == '{' || *cursor == '}' || *cursor == '\\')
        result += '\\';
      result += *cursor;
    }
  }
  return result;
}

std::string errorText(int error) {
  char buffer[AV_ERROR_MAX_STRING_SIZE]{};
  av_strerror(error, buffer, sizeof(buffer));
  return buffer;
}

constexpr const char *kHeader =
    "[Script Info]\nScriptType: v4.00+\nPlayResX: 1280\nPlayResY: 720\n"
    "WrapStyle: 0\n[V4+ Styles]\n"
    "Format: Name, Fontname, Fontsize, PrimaryColour, SecondaryColour, "
    "OutlineColour, BackColour, Bold, Italic, Underline, StrikeOut, ScaleX, "
    "ScaleY, Spacing, Angle, BorderStyle, Outline, Shadow, Alignment, MarginL, "
    "MarginR, MarginV, Encoding\n"
    "Style: Default,Noto Sans JP,32,&H00FFFFFF,&H00FFFFFF,&H00000000,"
    "&H00000000,0,0,0,0,100,100,0,0,1,1.6,0,2,20,20,36,1\n"
    "[Events]\nFormat: Layer, Start, End, Style, Name, MarginL, MarginR, "
    "MarginV, Effect, Text\n";

} // namespace

EmbeddedSubtitleResult
extractEmbeddedSubtitles(const std::vector<Uint8> &mediaBytes) {
  EmbeddedSubtitleResult result;
  if (mediaBytes.empty()) {
    result.reason = "No media bytes to inspect for embedded subtitles";
    return result;
  }

  ByteSource source{mediaBytes.data(), mediaBytes.size(), 0};
  unsigned char *ioBuffer = static_cast<unsigned char *>(av_malloc(64 * 1024));
  AVIOContext *io = nullptr;
  AVFormatContext *format = nullptr;
  AVCodecContext *decoder = nullptr;
  AVPacket *packet = nullptr;
  std::string events;

  auto cleanup = [&] {
    if (packet)
      av_packet_free(&packet);
    if (decoder)
      avcodec_free_context(&decoder);
    if (format)
      avformat_close_input(&format);
    if (io) {
      // avio_context_free() owns the buffer passed to avio_alloc_context().
      avio_context_free(&io);
    } else {
      av_free(ioBuffer);
    }
  };

  if (!ioBuffer) {
    result.reason = "Cannot allocate FFmpeg subtitle I/O buffer";
    cleanup();
    return result;
  }
  io = avio_alloc_context(ioBuffer, 64 * 1024, 0, &source, readBytes, nullptr,
                          seekBytes);
  if (!io) {
    result.reason = "Cannot create FFmpeg subtitle I/O context";
    cleanup();
    return result;
  }
  format = avformat_alloc_context();
  if (!format) {
    result.reason = "Cannot allocate FFmpeg demuxer context";
    cleanup();
    return result;
  }
  format->pb = io;
  format->flags |= AVFMT_FLAG_CUSTOM_IO;

  int status = avformat_open_input(&format, nullptr, nullptr, nullptr);
  if (status < 0) {
    result.reason =
        "Cannot probe media for embedded subtitles: " + errorText(status);
    cleanup();
    return result;
  }
  status = avformat_find_stream_info(format, nullptr);
  if (status < 0) {
    result.reason = "Cannot read media streams: " + errorText(status);
    cleanup();
    return result;
  }

  int streamIndex = -1;
  for (unsigned i = 0; i < format->nb_streams; ++i)
    if (format->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_SUBTITLE) {
      streamIndex = static_cast<int>(i);
      break;
    }
  if (streamIndex < 0) {
    result.reason = "Media has no embedded subtitle stream";
    cleanup();
    return result;
  }

  AVStream *stream = format->streams[streamIndex];
  const AVCodec *codec = avcodec_find_decoder(stream->codecpar->codec_id);
  if (!codec) {
    result.decoderAvailable = false;
    result.reason = "FFmpeg has no subtitle decoder for codec " +
                    std::to_string(stream->codecpar->codec_id);
    SDL_LogWarn(LogMedia, "Embedded subtitles unavailable: %s",
                result.reason.c_str());
    cleanup();
    return result;
  }
  result.decoderAvailable = true;

  decoder = avcodec_alloc_context3(codec);
  packet = av_packet_alloc();
  if (!decoder || !packet ||
      avcodec_parameters_to_context(decoder, stream->codecpar) < 0 ||
      avcodec_open2(decoder, codec, nullptr) < 0) {
    result.decoderAvailable = false;
    result.reason =
        std::string("Cannot initialize subtitle decoder ") + codec->name;
    SDL_LogWarn(LogMedia, "Embedded subtitles unavailable: %s",
                result.reason.c_str());
    cleanup();
    return result;
  }

  const double streamTimeBase = av_q2d(stream->time_base);
  const bool rawAss = stream->codecpar->codec_id == AV_CODEC_ID_ASS ||
                      stream->codecpar->codec_id == AV_CODEC_ID_SSA;
  size_t eventCount = 0;
  while (av_read_frame(format, packet) >= 0 && eventCount < 2048 &&
         events.size() < 12ull * 1024 * 1024) {
    if (packet->stream_index == streamIndex && packet->data && packet->size) {
      double packetTime = 0.0;
      if (packet->pts != AV_NOPTS_VALUE)
        packetTime = double(packet->pts) * streamTimeBase;
      double packetDuration = packet->duration > 0
                                  ? double(packet->duration) * streamTimeBase
                                  : 3.0;
      if (rawAss) {
        std::string line(reinterpret_cast<const char *>(packet->data),
                         packet->size);
        while (!line.empty() && (line.back() == '\n' || line.back() == '\r'))
          line.pop_back();
        if (line.find("Dialogue:") != std::string::npos) {
          events += line + '\n';
          ++eventCount;
        }
      } else {
        AVSubtitle subtitle{};
        int decoded = 0;
        status = avcodec_decode_subtitle2(decoder, &subtitle, &decoded, packet);
        if (status >= 0 && decoded) {
          for (unsigned i = 0; i < subtitle.num_rects && eventCount < 2048;
               ++i) {
            AVSubtitleRect *rect = subtitle.rects[i];
            if (!rect || !rect->text)
              continue;
            const double start =
                packetTime + subtitle.start_display_time / 1000.0;
            const double end =
                packetTime + (subtitle.end_display_time > 0
                                  ? subtitle.end_display_time / 1000.0
                                  : packetDuration);
            events += "Dialogue: 0," + timestamp(start) + ',' +
                      timestamp(std::max(start + 0.01, end)) +
                      ",Default,,0,0,0,," + escapeAss(rect->text) + '\n';
            ++eventCount;
          }
        }
        avsubtitle_free(&subtitle);
      }
    }
    av_packet_unref(packet);
  }

  if (events.empty()) {
    result.reason = "Embedded subtitle stream contained no convertible events";
    cleanup();
    return result;
  }
  result.script = kHeader + events;
  result.found = true;
  result.reason = "Converted " + std::to_string(eventCount) +
                  " embedded subtitle events (" + codec->name + ")";
  SDL_LogInfo(LogMedia, "Embedded subtitles: %s", result.reason.c_str());
  cleanup();
  return result;
}

} // namespace workers
} // namespace Kotonoha
