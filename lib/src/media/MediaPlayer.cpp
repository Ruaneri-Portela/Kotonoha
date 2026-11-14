#include <Kotonoha/core/AssetProvider.hpp>
#include <Kotonoha/Filesystem/Filesystem.hpp>
#include <Kotonoha/Filesystem/Adapters/BrowserBlobAdapter.hpp>
#include <Kotonoha/Filesystem/Sources/SDLFileSource.hpp>
#include <Kotonoha/core/UI.hpp>
#include <Kotonoha/media/FFmpegSupport.hpp>
#include <Kotonoha/media/MediaPlayer.hpp>
#include <Kotonoha/media/upscaling/Upscaler.hpp>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/mem.h>
#include <libswresample/swresample.h>
#include <libswscale/swscale.h>
}

#include <algorithm>
#include <atomic>
#include <cctype>
#include <chrono>
#include <climits>
#include <cmath>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <filesystem>
#include <mutex>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

#ifdef __EMSCRIPTEN__
#include <emscripten/emscripten.h>
#endif

namespace Kotonoha {
namespace {
std::atomic<Uint64> nextTextureSerial{0};

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
using Frame = std::unique_ptr<AVFrame, FrameDeleter>;

class HandleAssetStream final : public AssetStream {
  std::unique_ptr<Filesystem::FileHandle> file_;

public:
  explicit HandleAssetStream(std::unique_ptr<Filesystem::FileHandle> file)
      : file_(std::move(file)) {
    if (!file_)
      throw std::invalid_argument("Media file handle is null");
  }
  std::uint64_t size() const override {
    const auto n = file_->size();
    if (n < 0)
      throw std::runtime_error("Media file size is unknown");
    return static_cast<std::uint64_t>(n);
  }
  std::uint64_t tell() const override {
    const auto n = file_->tell();
    if (n < 0)
      throw std::runtime_error("Cannot tell media file position");
    return static_cast<std::uint64_t>(n);
  }
  std::size_t read(Uint8 *destination, std::size_t count) override {
    const auto n = file_->read(destination, count);
    if (n < 0)
      throw std::runtime_error("Media I/O read failed");
    return static_cast<std::size_t>(n);
  }
  bool seek(std::uint64_t offset) override {
    if (offset > static_cast<std::uint64_t>(INT64_MAX))
      return false;
    return file_->seek(static_cast<std::int64_t>(offset),
                       Filesystem::SeekOrigin::Begin) ==
           static_cast<std::int64_t>(offset);
  }
};
} // namespace

struct MediaPlayer::State {
  std::shared_ptr<std::atomic_bool> cancelled;
  static int interrupt(void *opaque) {
    const auto *self = static_cast<const State *>(opaque);
    return self->cancelled && self->cancelled->load() ? 1 : 0;
  }
  std::vector<Uint8> bytes;
  std::unique_ptr<AssetStream> assetStream;
  std::size_t offset = 0;
  AVIOContext *io = nullptr;
  AVFormatContext *format = nullptr;
  AVCodecContext *video = nullptr, *audio = nullptr;
  AVPacket *packet = nullptr;
  Frame frame{av_frame_alloc()};
  SwsContext *scaler = nullptr;
  SwrContext *resampler = nullptr;
  SDLHandle<SDL_AudioStream, SDL_DestroyAudioStream> stream;
  std::shared_ptr<AudioChannel> channel;
  TexturePtr texture;
  SDL_PixelFormat textureFormat = SDL_PIXELFORMAT_UNKNOWN;
  bool yuvTextureUnavailable = false;
  Uint64 textureSerial = 0;
#ifdef __EMSCRIPTEN__
  std::unique_ptr<Filesystem::BrowserBlobAdapter> webVirtualBlob;
  bool webVideo = false;
  bool webAudioOnly = false;
  bool webVideoStarted = false;
  bool webVideoUrlMode = false;
  bool webVideoBlobMode = false;
  bool webVideoReady = false;
  bool webVideoFailed = false;
  int webVideoSession = 0;
  std::uint64_t webVideoSize = 0;
  std::uint64_t webVideoFed = 0;
  std::string webVideoId, webVideoMime, webVideoUrl;
  std::string webFallbackPath, webFallbackId;
  std::unique_ptr<Filesystem::FileHandle> webVideoFile;

  static std::string mimeForMediaPath(const std::string &path,
                                      bool audioOnly) {
    std::string ext = std::filesystem::path(path).extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) {
      return static_cast<char>(std::tolower(c));
    });
    if (ext == ".mp4" || ext == ".m4v")
      return audioOnly ? "audio/mp4" : "video/mp4";
    if (ext == ".webm")
      return audioOnly ? "audio/webm" : "video/webm";
    if (ext == ".ogv")
      return "video/ogg";
    if (ext == ".ogg" || ext == ".oga" || ext == ".opus")
      return "audio/ogg";
    if (ext == ".mp3")
      return "audio/mpeg";
    if (ext == ".wav")
      return "audio/wav";
    if (ext == ".m4a" || ext == ".aac")
      return "audio/mp4";
    if (ext == ".flac")
      return "audio/flac";
    if (ext == ".mov")
      return "video/quicktime";
    if (ext == ".mpg" || ext == ".mpeg")
      return "video/mpeg";
    if (ext == ".avi")
      return "video/x-msvideo";
    return {};
  }

  static bool canPlayWebMedia(const std::string &mime) {
    return !mime.empty() &&
           EM_ASM_INT(
               {
                 return typeof KotonohaMediaCanPlay == "function" &&
                                KotonohaMediaCanPlay(UTF8ToString($0))
                            ? 1
                            : 0;
               },
               mime.c_str()) != 0;
  }

  // Only a path rooted in /game-assets is guaranteed to be an HTTP asset.
  // Arbitrary AssetProvider IDs may refer to archive members: never turn
  // those into HTTP URLs without a verified localPath mapping.
  static std::string webUrlForPath(const std::string &path) {
    if (path.empty() || path.find('\0') != std::string::npos)
      return {};
    const auto input = std::filesystem::path(path).lexically_normal();
    std::filesystem::path relative;
    if (input.is_absolute()) {
      relative = input.lexically_relative("/game-assets");
    } else {
      relative = input;
      if (!relative.empty() && *relative.begin() == "game-assets")
        relative = relative.lexically_relative("game-assets");
      else
        return {}; // Not an unambiguous public URL.
    }
    if (relative.empty() || relative == "." || relative.is_absolute())
      return {};
    for (const auto &part : relative)
      if (part == "..")
        return {};
    // Encode URL-sensitive bytes while preserving directory separators.
    // The browser resolves this URL relative to the document/base URL.
    const std::string raw = relative.generic_string();
    constexpr char hex[] = "0123456789ABCDEF";
    std::string encoded;
    encoded.reserve(raw.size());
    for (unsigned char c : raw) {
      if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
          (c >= '0' && c <= '9') || c == '/' || c == '-' || c == '_' ||
          c == '.' || c == '~') {
        encoded.push_back(static_cast<char>(c));
      } else {
        encoded.push_back('%');
        encoded.push_back(hex[c >> 4]);
        encoded.push_back(hex[c & 15]);
      }
    }
    return "game-assets/" + encoded;
  }

  bool configureWebVideoUrl(const std::string &path, bool audioOnly) {
    const std::string mime = mimeForMediaPath(path, audioOnly);
    if (!canPlayWebMedia(mime))
      return false;
    const std::string url = webUrlForPath(path);
    if (url.empty())
      return false;
    webVideo = true;
    webAudioOnly = mime.compare(0, 6, "audio/") == 0;
    webVideoUrlMode = true;
    webVideoId = path;
    webVideoMime = mime;
    webVideoUrl = url;
    SDL_Log("[Kotonoha] Web video: %s -> %s", path.c_str(), url.c_str());
    return true;
  }

  // Archive-backed videos use a chunked AssetProvider read and a Blob URL.
  // The browser cannot play until the entire Blob has been assembled.
  bool configureWebVideo(const AssetProvider &assets, const std::string &id,
                         bool audioOnly) {
    // A browser-compatible sidecar can replace a legacy cutscene without
    // changing the game's asset IDs or its native playback path.
    const std::string sidecar = id + ".webm";
    if (!audioOnly && !canPlayWebMedia(mimeForMediaPath(id, false)) &&
        canPlayWebMedia("video/webm") && assets.contains(sidecar))
      return configureWebVideo(assets, sidecar, false);
    const std::string local = assets.localPath(id);
    if (!local.empty() && configureWebVideoUrl(local, audioOnly))
      return true;
    const std::string mime = mimeForMediaPath(id, audioOnly);
    if (!canPlayWebMedia(mime))
      return false;
    auto source = assets.openStream(id);
    std::uint64_t size = source ? source->size() : 0;
    if (source && size) {
      assetStream = std::move(source);
    } else {
      bytes = assets.read(id);
      size = bytes.size();
      offset = 0;
      if (size > 0)
        webVideoBlobMode = true;
    }
    if (!size) {
      // A deferred manifest entry is represented by a zero-byte MEMFS file.
      // Its logical ID may be relative even when localPath() is empty.
      const auto relative = std::filesystem::path(id).lexically_normal();
      bool safe = !relative.empty() && !relative.is_absolute() &&
                  relative != "." && id.find('\0') == std::string::npos;
      for (const auto &part : relative)
        if (part == "..")
          safe = false;
      if (safe) {
        const auto candidate = std::filesystem::path("/game-assets") / relative;
        if (std::filesystem::is_regular_file(candidate))
          return configureWebVideoUrl(candidate.generic_string(), audioOnly);
      }
      return false;
    }
    webVideo = true;
    webAudioOnly = mime.compare(0, 6, "audio/") == 0;
    webVideoId = id;
    webVideoMime = mime;
    webVideoSize = size;
    return true;
  }

  bool configureWebVideo(const std::string &path, bool audioOnly) {
    if (!audioOnly && !canPlayWebMedia(mimeForMediaPath(path, false)) &&
        canPlayWebMedia("video/webm") &&
        std::filesystem::is_regular_file(path + ".webm"))
      return configureWebVideo(path + ".webm", false);
    if (configureWebVideoUrl(path, audioOnly))
      return true;
    const std::string mime = mimeForMediaPath(path, audioOnly);
    if (!canPlayWebMedia(mime))
      return false;
    std::unique_ptr<Filesystem::FileHandle> file;
    try {
      file = std::make_unique<Filesystem::SDLFileSource>(
          std::filesystem::path(path));
    } catch (const std::exception &) {
      return false;
    }
    const auto end = file->size();
    if (end <= 0)
      return false;
    webVideo = true;
    webAudioOnly = mime.compare(0, 6, "audio/") == 0;
    webVideoId = path;
    webVideoMime = mime;
    webVideoSize = static_cast<std::uint64_t>(end);
    webVideoFile = std::move(file);
    return true;
  }

  bool beginWebVideo() {
    if (!webVideo)
      return false;
    if (webVideoStarted)
      return true;
    const int session =
        webVideoUrlMode
            ? EM_ASM_INT(
                  {
                    return typeof KotonohaMediaBeginUrl == "function"
                               ? KotonohaMediaBeginUrl(UTF8ToString($0), $1)
                               : 0;
                  },
                  webVideoUrl.c_str(), webAudioOnly ? 1 : 0)
            : webVideoBlobMode
                ? EM_ASM_INT(
                      {
                        return typeof KotonohaMediaBeginBlob == "function"
                                   ? KotonohaMediaBeginBlob(HEAPU8.subarray($0, $0 + $1),
                                                            UTF8ToString($2), $3)
                                   : 0;
                      },
                      bytes.data(), static_cast<int>(bytes.size()),
                      webVideoMime.c_str(), webAudioOnly ? 1 : 0)
                : EM_ASM_INT(
                      {
                        return typeof KotonohaMediaBegin == "function"
                                   ? KotonohaMediaBegin(UTF8ToString($0),
                                                        UTF8ToString($1), $2, $3)
                                   : 0;
                      },
                      webVideoId.c_str(), webVideoMime.c_str(),
                      static_cast<int>(
                          std::min<std::uint64_t>(webVideoSize, INT_MAX)),
                      webAudioOnly ? 1 : 0);
    if (session <= 0) {
      webVideoFailed = true;
      return false;
    }
    webVideoSession = session;
    webVideoStarted = true;
    webVideoReady = webVideoUrlMode; // Blob mode will be set later in serviceWebVideo
    return true;
  }

  std::size_t readWebVideo(Uint8 *buffer, std::size_t size) {
    if (webVideoFile) {
      const auto amount = webVideoFile->read(buffer, size);
      if (amount < 0)
        throw std::runtime_error("Cannot read browser media");
      return static_cast<std::size_t>(amount);
    }
    if (assetStream)
      return assetStream->read(buffer, size);
    if (offset >= bytes.size())
      return 0;
    const auto count = std::min(size, bytes.size() - offset);
    std::copy_n(bytes.data() + offset, count, buffer);
    offset += count;
    return count;
  }

  void serviceWebVideo() {
    if (!webVideo || webVideoReady || webVideoFailed)
      return;
    if (!beginWebVideo())
      return;
    if (webVideoBlobMode) {
      if (EM_ASM_INT({
            return typeof KotonohaMediaReady == "function" &&
                   KotonohaMediaReady($0) ? 1 : 0;
          }, webVideoSession)) {
        webVideoReady = true;
      }
      return;
    }
    constexpr std::size_t budgetMax = 8u * 1024u * 1024u;
    constexpr std::size_t chunkMax = 2u * 1024u * 1024u;
    std::size_t budget = budgetMax;
    std::vector<Uint8> chunk;
    while (budget && webVideoFed < webVideoSize) {
      const auto want = static_cast<std::size_t>(std::min<std::uint64_t>(
          std::min(budget, chunkMax), webVideoSize - webVideoFed));
      chunk.resize(want);
      std::size_t count = 0;
      try {
        count = readWebVideo(chunk.data(), chunk.size());
      } catch (const std::exception &) {
        webVideoFailed = true;
        return;
      }
      if (!count) {
        webVideoFailed = true;
        return;
      }
      const bool accepted =
          EM_ASM_INT(
              {
                return typeof KotonohaMediaAppend == "function" &&
                               KotonohaMediaAppend($0,
                                                   HEAPU8.subarray($1, $1 + $2))
                           ? 1
                           : 0;
              },
              webVideoSession, chunk.data(), static_cast<int>(count)) != 0;
      if (!accepted) {
        webVideoFailed = true;
        return;
      }
      webVideoFed += count;
      budget -= count;
    }
    if (webVideoFed >= webVideoSize) {
      const bool finished =
          EM_ASM_INT(
              {
                return typeof KotonohaMediaFinish == "function" &&
                               KotonohaMediaFinish($0)
                           ? 1
                           : 0;
              },
              webVideoSession) != 0;
      if (!finished) {
        webVideoFailed = true;
        return;
      }
      webVideoReady = true;
      webVideoFile.reset();
    }
  }

  bool webVideoError() const {
    return webVideoFailed ||
           (webVideoStarted && EM_ASM_INT(
                                   {
                                     return typeof KotonohaMediaFailed ==
                                                        "function" &&
                                                    KotonohaMediaFailed($0)
                                                ? 1
                                                : 0;
                                   },
                                   webVideoSession) != 0);
  }

  void endWebVideo() {
    if (!webVideoStarted)
      return;
    EM_ASM(
        {
          if (typeof KotonohaMediaStop == "function")
            KotonohaMediaStop($0);
        },
        webVideoSession);
    webVideoStarted = false;
    webVideoReady = false;
    webVideoSession = 0;
  }
#endif
  int videoIndex = -1, audioIndex = -1;
  int width = 0, height = 0;
  double aspect = 1; // display width / height
  bool eof = false;
  double origin = 0;
  double nextVideoTime = 0, nextAudioTime = 0;
  double lastPosition = -1;
  // Media time at the end of the queued audio; negative until aligned.
  double queuedEnd = -1;
  int audioCorrections = 0;
  // Audio-bearing media is decoded by a producer, never by SDL's audio
  // callback.  The mutex also serializes explicit timeline seeks.
  mutable std::mutex mutex;
  bool threaded = false, paused = true, closing = false;
  double decodeRate = 1.0, targetBuffer = 0.5;
  std::condition_variable wake;
  std::thread decoderThread;
  std::string failure;
  struct VideoFrame {
    double at;
    Frame frame;
  };
  std::deque<VideoFrame> frames;

  ~State() {
#ifdef __EMSCRIPTEN__
    endWebVideo();
#endif
    if (cancelled)
      cancelled->store(true);
    if (stream)
      SDL_SetAudioStreamGetCallback(stream.get(), nullptr, nullptr);
    {
      std::lock_guard<std::mutex> lock(mutex);
      closing = true;
    }
    wake.notify_one();
    if (decoderThread.joinable())
      decoderThread.join();
    if (channel && stream)
      channel->detach(stream.get());
    stream.reset();
    swr_free(&resampler);
    sws_freeContext(scaler);
    avcodec_free_context(&video);
    avcodec_free_context(&audio);
    av_packet_free(&packet);
    avformat_close_input(&format);
    if (io) {
      av_freep(&io->buffer);
      avio_context_free(&io);
    }
  }

  static int readMemory(void *opaque, uint8_t *buffer, int size) {
    auto &self = *static_cast<State *>(opaque);
    if (!buffer || size <= 0 || self.offset > self.bytes.size())
      return AVERROR(EINVAL);
    const auto count = std::min<std::size_t>(static_cast<std::size_t>(size),
                                             self.bytes.size() - self.offset);
    if (count == 0)
      return AVERROR_EOF;
    std::copy_n(self.bytes.data() + self.offset, count, buffer);
    self.offset += count;
    return static_cast<int>(count);
  }

  static int64_t seekMemory(void *opaque, int64_t position, int whence) {
    auto &self = *static_cast<State *>(opaque);
    if (self.bytes.size() > static_cast<std::size_t>(INT64_MAX))
      return AVERROR(EINVAL);
    const auto size = static_cast<int64_t>(self.bytes.size());
    if (whence & AVSEEK_SIZE)
      return size;
    int64_t base = 0;
    switch (whence & ~AVSEEK_FORCE) {
    case SEEK_SET:
      break;
    case SEEK_CUR:
      base = static_cast<int64_t>(self.offset);
      break;
    case SEEK_END:
      base = size;
      break;
    default:
      return AVERROR(EINVAL);
    }
    if ((position > 0 && base > INT64_MAX - position) ||
        (position < 0 && base < INT64_MIN - position))
      return AVERROR(EINVAL);
    const int64_t target = base + position;
    if (target < 0 || target > size)
      return AVERROR(EINVAL);
    self.offset = static_cast<std::size_t>(target);
    return target;
  }

  static int readAssetStream(void *opaque, uint8_t *buffer, int size) {
    auto &self = *static_cast<State *>(opaque);
    if (!self.assetStream || !buffer || size <= 0)
      return AVERROR(EINVAL);
    try {
      const auto count =
          self.assetStream->read(buffer, static_cast<std::size_t>(size));
      if (count == 0)
        return AVERROR_EOF;
      if (count > static_cast<std::size_t>(size) ||
          count > static_cast<std::size_t>(INT_MAX))
        return AVERROR(EIO);
      return static_cast<int>(count);
    } catch (...) {
      return AVERROR(EIO);
    }
  }

  static int64_t seekAssetStream(void *opaque, int64_t position, int whence) {
    auto &self = *static_cast<State *>(opaque);
    if (!self.assetStream)
      return AVERROR(EINVAL);
    try {
      const std::uint64_t size = self.assetStream->size();
      if (whence & AVSEEK_SIZE) {
        if (size > static_cast<std::uint64_t>(INT64_MAX))
          return AVERROR(EINVAL);
        return static_cast<int64_t>(size);
      }
      std::uint64_t base = 0;
      switch (whence & ~AVSEEK_FORCE) {
      case SEEK_SET:
        break;
      case SEEK_CUR:
        base = self.assetStream->tell();
        break;
      case SEEK_END:
        base = size;
        break;
      default:
        return AVERROR(EINVAL);
      }
      if (base > static_cast<std::uint64_t>(INT64_MAX))
        return AVERROR(EINVAL);
      const auto signedBase = static_cast<int64_t>(base);
      if ((position > 0 && signedBase > INT64_MAX - position) ||
          (position < 0 && signedBase < INT64_MIN - position))
        return AVERROR(EINVAL);
      const int64_t target = signedBase + position;
      if (target < 0 || static_cast<std::uint64_t>(target) > size ||
          !self.assetStream->seek(static_cast<std::uint64_t>(target)))
        return AVERROR(EINVAL);
      return target;
    } catch (...) {
      return AVERROR(EIO);
    }
  }

  void attachMemory(std::vector<Uint8> data) {
    constexpr int bufferSize = 64 * 1024;
    bytes = std::move(data);
    auto *buffer = static_cast<uint8_t *>(av_malloc(bufferSize));
    if (!buffer)
      throw std::bad_alloc();
    io = avio_alloc_context(buffer, bufferSize, 0, this, readMemory, nullptr,
                            seekMemory);
    if (!io) {
      av_free(buffer);
      throw std::bad_alloc();
    }
    format = avformat_alloc_context();
    if (!format)
      throw std::bad_alloc();
    format->pb = io;
    format->flags |= AVFMT_FLAG_CUSTOM_IO;
  }

  void attachAssetStream(std::unique_ptr<AssetStream> stream) {
    constexpr int bufferSize = 64 * 1024;
    if (!stream)
      throw std::invalid_argument("Seekable media stream is null");
    assetStream = std::move(stream);
    auto *buffer = static_cast<uint8_t *>(av_malloc(bufferSize));
    if (!buffer)
      throw std::bad_alloc();
    io = avio_alloc_context(buffer, bufferSize, 0, this, readAssetStream,
                            nullptr, seekAssetStream);
    if (!io) {
      av_free(buffer);
      throw std::bad_alloc();
    }
    format = avformat_alloc_context();
    if (!format)
      throw std::bad_alloc();
    format->pb = io;
    format->flags |= AVFMT_FLAG_CUSTOM_IO;
  }

  AVCodecContext *decoder(int &index, AVMediaType type, bool required) {
    const AVCodec *codec = nullptr;
    index = av_find_best_stream(format, type, -1, -1, &codec, 0);
    if (index < 0) {
      if (required)
        check(index, "Required media stream not found");
      if (index != AVERROR_STREAM_NOT_FOUND)
        check(index, "Find media decoder");
      return nullptr;
    }
    AVCodecContext *context = avcodec_alloc_context3(codec);
    if (!context)
      throw std::bad_alloc();
    try {
      check(avcodec_parameters_to_context(context,
                                          format->streams[index]->codecpar),
            "Read codec parameters");
      context->pkt_timebase = format->streams[index]->time_base;
      check(avcodec_open2(context, codec, nullptr), "Open codec");
    } catch (...) {
      avcodec_free_context(&context);
      throw;
    }
    return context;
  }

  double timestamp(int index, double fallback) const {
    if (frame->best_effort_timestamp == AV_NOPTS_VALUE)
      return fallback;
    return frame->best_effort_timestamp *
               av_q2d(format->streams[index]->time_base) -
           origin;
  }

  double queuedAudio() const {
    if (!stream)
      return 0;
    const int bytes = SDL_GetAudioStreamQueued(stream.get());
    if (bytes < 0)
      throw std::runtime_error(SDL_GetError());
    return bytes / (48000.0 * 2 * sizeof(float));
  }

  void putAudio(const std::vector<float> &samples, std::size_t offset = 0) {
    if (offset >= samples.size())
      return;
    if (!SDL_PutAudioStreamData(
            stream.get(), samples.data() + offset,
            static_cast<int>((samples.size() - offset) * sizeof(float))))
      throw std::runtime_error(SDL_GetError());
  }

  void receive(AVCodecContext *decoder, bool isVideo, double position) {
    for (;;) {
      const int result = avcodec_receive_frame(decoder, frame.get());
      if (result == AVERROR(EAGAIN) || result == AVERROR_EOF)
        return;
      check(result, "Decode media");
      if (isVideo) {
        const double at = timestamp(videoIndex, nextVideoTime);
        const double rate = av_q2d(
            av_guess_frame_rate(format, format->streams[videoIndex], nullptr));
        nextVideoTime = at + (rate > 0 ? 1.0 / rate : 1.0 / 30);
        Frame copy{av_frame_clone(frame.get())};
        if (!copy)
          throw std::bad_alloc();
        frames.push_back({at, std::move(copy)});
        while (frames.size() > 1 && frames[1].at <= position)
          frames.pop_front();
      } else {
        const double at = timestamp(audioIndex, nextAudioTime);
        nextAudioTime =
            at + frame->nb_samples / static_cast<double>(audio->sample_rate);
        const int capacity = static_cast<int>(av_rescale_rnd(
            swr_get_delay(resampler, audio->sample_rate) + frame->nb_samples,
            48000, audio->sample_rate, AV_ROUND_UP));
        std::vector<float> samples(static_cast<std::size_t>(capacity) * 2);
        uint8_t *output = reinterpret_cast<uint8_t *>(samples.data());
        std::vector<const uint8_t *> input(
            av_sample_fmt_is_planar(audio->sample_fmt)
                ? audio->ch_layout.nb_channels
                : 1);
        for (std::size_t i = 0; i < input.size(); ++i)
          input[i] = frame->extended_data[i];
        const int count = swr_convert(resampler, &output, capacity,
                                      input.data(), frame->nb_samples);
        check(count, "Resample audio");
        samples.resize(static_cast<std::size_t>(count) * 2);
        // Establish the queue origin on open/seek. Once PCM is queued, its
        // position follows the number of samples sent to SDL; resetting that
        // origin after a render hitch splices the following packet against a
        // different clock and produces a click at loud sample boundaries.
        const double expected = position + queuedAudio();
        if (queuedEnd < 0) {
          queuedEnd = expected;
        }
        const double gap = at - queuedEnd;
        // Container packet timestamps can differ slightly from the number of
        // PCM frames emitted by the resampler. The old playback path queued
        // each decoded frame contiguously; splicing silence or dropping PCM
        // for a few milliseconds here creates an audible edge at loud peaks.
        // Only repair a real timeline discontinuity or a seek offset.
        constexpr double timestampDiscontinuitySeconds = 0.05;
        if (std::abs(gap) > timestampDiscontinuitySeconds)
          ++audioCorrections;
        if (gap > timestampDiscontinuitySeconds) {
          // Preserve stream offsets without allocating unbounded silence.
          if (gap > 30)
            throw std::runtime_error("Audio timestamp gap exceeds 30 seconds");
          const auto silence = static_cast<std::size_t>(gap * 48000);
          putAudio(std::vector<float>(silence * 2));
          queuedEnd += silence / 48000.0;
        }
        const auto skipped =
            gap < -timestampDiscontinuitySeconds
                ? static_cast<std::size_t>(std::max(
                      0.0, std::min(-gap * 48000, static_cast<double>(count))))
                : 0u;
        putAudio(samples, skipped * 2);
        queuedEnd += (static_cast<std::size_t>(count) - skipped) / 48000.0;
      }
      av_frame_unref(frame.get());
    }
  }

  void decode(AVCodecContext *decoder, bool isVideo, const AVPacket *data,
              double position) {
    int result = avcodec_send_packet(decoder, data);
    if (result == AVERROR(EAGAIN)) {
      receive(decoder, isVideo, position);
      result = avcodec_send_packet(decoder, data);
    }
    check(result, "Submit media packet");
    receive(decoder, isVideo, position);
  }

  double playhead() const { return queuedEnd - queuedAudio(); }

  // Decodes until video and audio have headroom past position.
  void fill(double position, double audioTarget) {
    int packets = 0;
    for (; !eof && packets < 512; ++packets) {
      const bool videoReady =
          video && !frames.empty() && frames.back().at > position + 0.1;
      const bool audioReady = audio && queuedAudio() >= audioTarget;
      // Both streams need headroom. Stopping at the first ready video frame
      // starves audio in containers whose audio packets follow a video cluster.
      if ((!video || videoReady) && (!audio || audioReady))
        break;
      // Bound buffering for interleaved streams with severely unequal
      // timestamps.
      if (frames.size() > 300 || queuedAudio() > 5)
        throw std::runtime_error(
            "Media stream interleaving exceeds buffer limits");
      const int result = av_read_frame(format, packet);
      if (result == AVERROR_EOF) {
        eof = true;
        if (video)
          decode(video, true, nullptr, position);
        if (audio) {
          decode(audio, false, nullptr, position);
          for (;;) {
            std::vector<float> tail(4096 * 2);
            uint8_t *output = reinterpret_cast<uint8_t *>(tail.data());
            const int count = swr_convert(resampler, &output, 4096, nullptr, 0);
            check(count, "Drain audio resampler");
            if (!count)
              break;
            tail.resize(static_cast<std::size_t>(count) * 2);
            putAudio(tail);
          }
          if (!SDL_FlushAudioStream(stream.get()))
            throw std::runtime_error(SDL_GetError());
        }
        break;
      }
      check(result, "Read media packet");
      if (packet->stream_index == videoIndex)
        decode(video, true, packet, position);
      else if (packet->stream_index == audioIndex)
        decode(audio, false, packet, position);
      av_packet_unref(packet);
    }
    if (packets == 512 && !eof)
      throw std::runtime_error(
          "Media cannot reach the requested time within the decode budget");
  }

  static void SDLCALL feed(void *userdata, SDL_AudioStream *, int, int) {
    // The real-time callback only wakes the decoder; it does no FFmpeg work.
    static_cast<State *>(userdata)->wake.notify_one();
  }

  void startDecoder() {
    threaded = true;
    decoderThread = std::thread([this] {
      std::unique_lock<std::mutex> lock(mutex);
      while (!closing) {
        // Prime a prepared input while the audio device is paused.  After a
        // seek, nextAudioTime identifies the new queue origin.
        if ((!paused || queuedEnd < 0) && !eof && failure.empty()) {
          try {
            const double position = queuedEnd < 0 ? nextAudioTime : playhead();
            fill(position, std::min(4.0, targetBuffer * decodeRate));
          } catch (const std::exception &error) {
            failure = error.what();
          }
        }
        wake.wait_for(lock, std::chrono::milliseconds(10));
      }
    });
  }

  void upload(RendererRef renderer, AVFrame *image) {
    // GLES2 can sample planar 4:2:0 directly. Keep DVD MPEG frames in their
    // decoder format so the GPU performs YUV conversion during rendering.
    // Other matrices/ranges retain the established swscale path.
    const bool directYuv =
        image->format == AV_PIX_FMT_YUV420P && !(image->width & 1) &&
        !(image->height & 1) &&
        (image->color_range == AVCOL_RANGE_UNSPECIFIED ||
         image->color_range == AVCOL_RANGE_MPEG) &&
        (image->colorspace == AVCOL_SPC_UNSPECIFIED ||
         image->colorspace == AVCOL_SPC_BT470BG ||
         image->colorspace == AVCOL_SPC_SMPTE170M) &&
        image->linesize[0] > 0 && image->linesize[1] > 0 &&
        image->linesize[2] > 0;
    const SDL_PixelFormat wanted =
        directYuv && !yuvTextureUnavailable ? SDL_PIXELFORMAT_IYUV
                                            : SDL_PIXELFORMAT_RGBA32;
    if (!texture || width != image->width || height != image->height ||
        textureFormat != wanted) {
      width = image->width;
      height = image->height;
      const double sar = image->sample_aspect_ratio.num > 0 &&
                                 image->sample_aspect_ratio.den > 0
                             ? av_q2d(image->sample_aspect_ratio)
                             : 1.0;
      aspect = width * sar / height;
      texture.reset(SDL_CreateTexture(&renderer, wanted,
                                      SDL_TEXTUREACCESS_STREAMING, width, height));
      textureFormat = wanted;
      if (!texture && directYuv) {
        yuvTextureUnavailable = true;
        texture.reset(SDL_CreateTexture(&renderer, SDL_PIXELFORMAT_RGBA32,
                                        SDL_TEXTUREACCESS_STREAMING, width,
                                        height));
        textureFormat = SDL_PIXELFORMAT_RGBA32;
      }
      if (!texture)
        throw std::runtime_error(SDL_GetError());
      if (textureFormat == SDL_PIXELFORMAT_IYUV)
        SDL_Log("[Kotonoha] Direct YUV420P video texture");
    }
    if (directYuv && textureFormat == SDL_PIXELFORMAT_IYUV) {
      if (!SDL_UpdateYUVTexture(texture.get(), nullptr, image->data[0],
                                image->linesize[0], image->data[1],
                                image->linesize[1], image->data[2],
                                image->linesize[2]))
        throw std::runtime_error(SDL_GetError());
      textureSerial = ++nextTextureSerial;
      return;
    }
    scaler = sws_getCachedContext(
        scaler, width, height, static_cast<AVPixelFormat>(image->format), width,
        height, AV_PIX_FMT_RGBA, SWS_BILINEAR, nullptr, nullptr, nullptr);
    if (!scaler)
      throw std::runtime_error("Cannot create video pixel converter");
    std::vector<uint8_t> pixels(static_cast<std::size_t>(width) * height * 4);
    uint8_t *planes[]{pixels.data()};
    int strides[]{width * 4};
    check(sws_scale(scaler, image->data, image->linesize, 0, height, planes,
                    strides),
          "Convert video frame");
    if (!SDL_UpdateTexture(texture.get(), nullptr, pixels.data(), width * 4))
      throw std::runtime_error(SDL_GetError());
    textureSerial = ++nextTextureSerial;
  }
};

MediaPlayer::MediaPlayer()
    : ownedMixer(std::make_unique<AudioMixer>()), mixer(*ownedMixer) {}
MediaPlayer::MediaPlayer(AudioMixer &value) : mixer(value) {}
MediaPlayer::~MediaPlayer() = default;

void MediaPlayer::open(std::unique_ptr<Filesystem::FileHandle> source,
                       bool audioOnly, const std::string &channelName) {
  open(std::move(source), {}, audioOnly, channelName);
}

void MediaPlayer::open(std::unique_ptr<Filesystem::FileHandle> source,
                       const std::string &logicalName, bool audioOnly,
                       const std::string &channelName) {
  if (!source)
    throw std::invalid_argument("Media input FileHandle is null");
  auto next = std::make_unique<State>();
  next->channel = mixer.channel(
      channelName.empty() ? (audioOnly ? "audio" : "video") : channelName);
#ifdef __EMSCRIPTEN__
  const auto mime = State::mimeForMediaPath(logicalName, audioOnly);
  const auto length = source->size();
  if (!logicalName.empty() && State::canPlayWebMedia(mime) &&
      length >= 0 && length <= (64ll << 20)) {
    next->webVirtualBlob = std::make_unique<Filesystem::BrowserBlobAdapter>(
        std::move(source), mime);
    next->webVideo = true;
    next->webAudioOnly = mime.compare(0, 6, "audio/") == 0;
    next->webVideoUrlMode = true;
    next->webVideoId = logicalName;
    next->webVideoMime = mime;
    next->webVideoUrl = next->webVirtualBlob->url();
    next->webVideoSize = static_cast<std::uint64_t>(length);
    start(std::move(next), {}, audioOnly, false);
    return;
  }
#else
  (void)logicalName;
#endif
  next->attachAssetStream(
      std::make_unique<HandleAssetStream>(std::move(source)));
  start(std::move(next), {}, audioOnly, false);
}

void MediaPlayer::open(const std::string &path, bool audioOnly,
                       const std::string &channelName) {
  auto next = std::make_unique<State>();
  next->channel = mixer.channel(
      channelName.empty() ? (audioOnly ? "audio" : "video") : channelName);
#ifdef __EMSCRIPTEN__
  std::string effectivePath = path;
  if (effectivePath.find("/") != 0 && effectivePath.find("game-assets/") != 0) {
    effectivePath = "/game-assets/" + effectivePath;
  }
  if (next->configureWebVideo(effectivePath, audioOnly)) {
    next->webFallbackPath = effectivePath;
    start(std::move(next), {}, audioOnly, false);
    return;
  }
#endif
  if (!std::filesystem::is_regular_file(path))
    throw std::runtime_error("Media must be a local file: " + path);
  next->attachAssetStream(std::make_unique<HandleAssetStream>(
      std::make_unique<Filesystem::SDLFileSource>(std::filesystem::path(path))));
  start(std::move(next), {}, audioOnly, false);
}

void MediaPlayer::open(const AssetProvider &assets, const std::string &id,
                       bool audioOnly, const std::string &channelName) {
  auto next = std::make_unique<State>();
  next->channel = mixer.channel(
      channelName.empty() ? (audioOnly ? "audio" : "video") : channelName);
#ifdef __EMSCRIPTEN__
  if (next->configureWebVideo(assets, id, audioOnly)) {
    next->webFallbackPath = assets.localPath(id);
    next->webFallbackId = id;
    start(std::move(next), {}, audioOnly, false);
    return;
  }
#endif
  const std::string path = assets.localPath(id);
  if (!path.empty() && std::filesystem::is_regular_file(path)) {
    next->attachAssetStream(std::make_unique<HandleAssetStream>(
        std::make_unique<Filesystem::SDLFileSource>(
            std::filesystem::path(path))));
    start(std::move(next), {}, audioOnly, false);
    return;
  }
  if (auto source = assets.openStream(id))
    next->attachAssetStream(std::move(source));
  else
    next->attachMemory(assets.read(id));
  start(std::move(next), {}, audioOnly, false);
}

void MediaPlayer::prepare(const AssetProvider &assets, const std::string &id,
                          bool audioOnly, const std::string &channelName) {
  auto next = std::make_unique<State>();
  next->channel = mixer.channel(
      channelName.empty() ? (audioOnly ? "audio" : "video") : channelName);
#ifdef __EMSCRIPTEN__
  if (next->configureWebVideo(assets, id, audioOnly)) {
    next->webFallbackPath = assets.localPath(id);
    next->webFallbackId = id;
    start(std::move(next), {}, audioOnly, true);
    return;
  }
#endif
  const std::string path = assets.localPath(id);
  if (!path.empty() && std::filesystem::is_regular_file(path)) {
    next->attachAssetStream(std::make_unique<HandleAssetStream>(
        std::make_unique<Filesystem::SDLFileSource>(
            std::filesystem::path(path))));
    start(std::move(next), {}, audioOnly, true);
    return;
  }
  if (auto source = assets.openStream(id))
    next->attachAssetStream(std::move(source));
  else
    next->attachMemory(assets.read(id));
  start(std::move(next), {}, audioOnly, true);
}

MediaPlayer::PreparedInput::PreparedInput(std::unique_ptr<State> value)
    : state(std::move(value)) {}
MediaPlayer::PreparedInput::~PreparedInput() = default;

void MediaPlayer::openDecoder(State &next, const std::string &url,
                              bool audioOnly) {
  if (!next.format)
    next.format = avformat_alloc_context();
  if (!next.format)
    throw std::bad_alloc();
  next.format->interrupt_callback = {State::interrupt, &next};
  AVDictionary *options = nullptr;
  if (!next.io)
    check(av_dict_set(&options, "protocol_whitelist", "file", 0),
          "Configure local media access");
  const int opened = avformat_open_input(
      &next.format, next.io ? nullptr : url.c_str(), nullptr, &options);
  av_dict_free(&options);
  check(opened, "Open media");
  detail::initializeMediaLog();
  check(avformat_find_stream_info(next.format, nullptr), "Read media streams");
  if (next.format->start_time != AV_NOPTS_VALUE)
    next.origin = next.format->start_time / static_cast<double>(AV_TIME_BASE);
  if (!audioOnly)
    next.video = next.decoder(next.videoIndex, AVMEDIA_TYPE_VIDEO, true);
  next.audio = next.decoder(next.audioIndex, AVMEDIA_TYPE_AUDIO, audioOnly);
  next.packet = av_packet_alloc();
  if (!next.packet || !next.frame)
    throw std::bad_alloc();
  if (next.audio) {
    AVChannelLayout stereo = AV_CHANNEL_LAYOUT_STEREO;
    check(swr_alloc_set_opts2(&next.resampler, &stereo, AV_SAMPLE_FMT_FLT,
                              48000, &next.audio->ch_layout,
                              next.audio->sample_fmt, next.audio->sample_rate,
                              0, nullptr),
          "Configure audio conversion");
    check(swr_init(next.resampler), "Initialize audio conversion");
  }
}

std::unique_ptr<MediaPlayer::PreparedInput>
MediaPlayer::prepareInput(const AssetProvider &assets, const std::string &id,
                          bool audioOnly,
                          std::shared_ptr<std::atomic_bool> cancelled) {
  auto next = std::make_unique<State>();
  next->cancelled = std::move(cancelled);
#ifdef __EMSCRIPTEN__
  if (next->configureWebVideo(assets, id, audioOnly)) {
    next->webFallbackPath = assets.localPath(id);
    next->webFallbackId = id;
    return std::unique_ptr<PreparedInput>(new PreparedInput(std::move(next)));
  }
#endif
  const std::string path = assets.localPath(id);
  if (!path.empty() && std::filesystem::is_regular_file(path)) {
    next->attachAssetStream(std::make_unique<HandleAssetStream>(
        std::make_unique<Filesystem::SDLFileSource>(
            std::filesystem::path(path))));
    openDecoder(*next, {}, audioOnly);
  } else {
    if (auto stream = assets.openStream(id))
      next->attachAssetStream(std::move(stream));
    else
      next->attachMemory(assets.read(id));
    openDecoder(*next, {}, audioOnly);
  }
  return std::unique_ptr<PreparedInput>(new PreparedInput(std::move(next)));
}

void MediaPlayer::adoptPreparedInput(std::unique_ptr<PreparedInput> input,
                                     const std::string &channelName) {
  if (!input || !input->state)
    throw std::invalid_argument("Prepared media is empty");
  auto next = std::move(input->state);
  // The speculative cancellation token belongs to the scene worker.  Once
  // adopted, the player owns a fresh token that interrupts FFmpeg on stop.
  next->cancelled = std::make_shared<std::atomic_bool>(false);
  bool isVideo = next->video != nullptr;
#ifdef __EMSCRIPTEN__
  isVideo = isVideo || (next->webVideo && !next->webAudioOnly);
#endif
  next->channel = mixer.channel(
      channelName.empty() ? (isVideo ? "video" : "audio") : channelName);
#ifdef __EMSCRIPTEN__
  if (next->webVideo) {
    if (!next->beginWebVideo())
      next->webVideoFailed = true;

    // Configure the newly created browser session before exposing it as active.
    // A prepared input must remain paused until setPaused(false) is called.
    EM_ASM(
        {
          if (typeof KotonohaMediaSetRate == "function")
            KotonohaMediaSetRate($0, $1);
          if (typeof KotonohaMediaSetPaused == "function")
            KotonohaMediaSetPaused($0, 1);
        },
        next->webVideoSession, playbackRate_);

    soundtrack.reset();
    state = std::move(next);

    clockStart = SDL_GetTicksNS();
    pausedAt = clockStart;
    clockOffset = 0.0;

    return;
  }
#endif
  if (next->audio) {
    const SDL_AudioSpec spec{SDL_AUDIO_F32, 2, 48000};
    next->stream.reset(SDL_OpenAudioDeviceStream(
        SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, nullptr, nullptr));
    if (!next->stream)
      throw std::runtime_error(SDL_GetError());
    if (!SDL_SetAudioStreamFrequencyRatio(next->stream.get(),
                                          static_cast<float>(playbackRate_)))
      throw std::runtime_error(SDL_GetError());
    next->channel->attach(next->stream.get());
    next->decodeRate = playbackRate_;
    next->targetBuffer = bufferSeconds_ > 0.0 ? bufferSeconds_ : 0.5;
#ifndef __EMSCRIPTEN__
    next->startDecoder();
    if (!SDL_SetAudioStreamGetCallback(next->stream.get(), State::feed,
                                       next.get()))
      throw std::runtime_error(SDL_GetError());
#endif
  }
  soundtrack.reset();
  state = std::move(next);
  clockStart = SDL_GetTicksNS();
  pausedAt = clockStart;
  clockOffset = 0.0;
}

void MediaPlayer::start(std::unique_ptr<State> next, const std::string &url,
                        bool audioOnly, bool prepared) {
#ifdef __EMSCRIPTEN__
  if (!next->webVideo)
#endif
    openDecoder(*next, url, audioOnly);
  auto input =
      std::unique_ptr<PreparedInput>(new PreparedInput(std::move(next)));
  const std::string channelName =
      input->state->channel ? input->state->channel->getName() : std::string();
  adoptPreparedInput(std::move(input), channelName);
  if (!prepared)
    setPaused(false);
}

void MediaPlayer::openSoundtrack(const AssetProvider &assets,
                                 const std::string &id,
                                 const std::string &channelName) {
  if (!state)
    throw std::logic_error("Open media before its soundtrack");
  auto next = std::make_unique<MediaPlayer>(mixer);
  next->setPlaybackRate(playbackRate_);
  if (bufferSeconds_ > 0.0)
    next->setBufferSeconds(bufferSeconds_);
  next->open(assets, id, true,
             channelName.empty() ? state->channel->getName() : channelName);
  if (pausedAt)
    next->setPaused(true);
  soundtrack = std::move(next);
}

void MediaPlayer::stop() {
  soundtrack.reset();
  state.reset();
  pausedAt = 0;
  clockOffset = 0.0;
}

double MediaPlayer::elapsed() const {
  if (!state)
    return 0.0;

#ifdef __EMSCRIPTEN__
  if (state->webVideo && state->webVideoStarted) {
    const double time = EM_ASM_DOUBLE(
        {
          const callback = globalThis["KotonohaMediaTime"];

          if (typeof callback != "function")
            return -1.0;

          return callback($0);
        },
        state->webVideoSession);

    if (std::isfinite(time) && time >= 0.0)
      return time;
  }
#endif

  const Uint64 now = pausedAt ? pausedAt : SDL_GetTicksNS();

  return clockOffset + (now - clockStart) / 1e9 * playbackRate_;
}

void MediaPlayer::update(RendererRef renderer) { update(renderer, elapsed()); }

void MediaPlayer::setChannel(const std::string &name) {
  auto channel = mixer.channel(name);
  if (!state)
    throw std::logic_error("Open media before assigning its audio channel");
  if (state->channel == channel)
    return;
  if (state->stream) {
    channel->attach(state->stream.get());
    state->channel->detach(state->stream.get());
  }
  state->channel = std::move(channel);
  if (soundtrack)
    soundtrack->setChannel(name);
}

std::shared_ptr<AudioChannel> MediaPlayer::getChannel() const {
  return state ? state->channel : nullptr;
}

float MediaPlayer::audioGain() const {
#ifdef __EMSCRIPTEN__
  if (state && state->webVideo)
    return mixer.isMasterMuted() || state->channel->isMuted()
               ? 0.0f
               : mixer.getMasterVolume() * state->channel->getUserVolume() *
                     state->channel->getVolume();
#endif
  if (!state || !state->stream)
    throw std::logic_error("Media has no audio stream");
  const float gain = SDL_GetAudioStreamGain(state->stream.get());
  if (gain < 0)
    throw std::runtime_error(SDL_GetError());
  return gain;
}

void MediaPlayer::setPaused(bool paused) {
  if (paused == (pausedAt != 0))
    return;
  if (state && paused && !pausedAt)
    pausedAt = SDL_GetTicksNS();
  else if (state && !paused && pausedAt) {
    clockStart += SDL_GetTicksNS() - pausedAt;
    pausedAt = 0;
  }
  if (soundtrack)
    soundtrack->setPaused(paused);
#ifdef __EMSCRIPTEN__
  if (state && state->webVideo && state->webVideoStarted)
    EM_ASM(
        {
          if (typeof KotonohaMediaSetPaused == "function")
            KotonohaMediaSetPaused($0, $1);
        },
        state->webVideoSession, paused ? 1 : 0);
#endif
  if (state && state->threaded) {
    {
      std::lock_guard<std::mutex> lock(state->mutex);
      state->paused = paused;
    }
    state->wake.notify_one();
  }
  if (state && state->stream) {
    const bool ok = paused ? SDL_PauseAudioStreamDevice(state->stream.get())
                           : SDL_ResumeAudioStreamDevice(state->stream.get());
    if (!ok)
      throw std::runtime_error(SDL_GetError());
  }
}

void MediaPlayer::setPlaybackRate(double rate) {
  if (!std::isfinite(rate) || rate < 0.01 || rate > 100.0)
    throw std::invalid_argument(
        "Media playback rate must be between 0.01 and 100");
  if (rate == playbackRate_)
    return;

  // Close the previous self-timed clock segment before changing its slope so
  // live rate changes never jump the visual/media position.
  if (state) {
    const Uint64 now = pausedAt ? pausedAt : SDL_GetTicksNS();
    clockOffset += (now - clockStart) / 1e9 * playbackRate_;
    clockStart = now;
    if (pausedAt)
      pausedAt = now;
    if (state->stream && !SDL_SetAudioStreamFrequencyRatio(
                             state->stream.get(), static_cast<float>(rate)))
      throw std::runtime_error(SDL_GetError());
#ifdef __EMSCRIPTEN__
    if (state->webVideo && state->webVideoStarted)
      EM_ASM(
          {
            if (typeof KotonohaMediaSetRate == "function")
              KotonohaMediaSetRate($0, $1);
          },
          state->webVideoSession, rate);
#endif
    if (state->threaded) {
      {
        std::lock_guard<std::mutex> lock(state->mutex);
        state->decodeRate = rate;
      }
      state->wake.notify_one();
    }
  }
  if (soundtrack)
    soundtrack->setPlaybackRate(rate);
  playbackRate_ = rate;
}

void MediaPlayer::setBufferSeconds(double seconds) {
  if (!std::isfinite(seconds) || seconds < 0.05 || seconds > 60.0)
    throw std::invalid_argument(
        "Media buffer target must be between 0.05 and 60 seconds");
  bufferSeconds_ = seconds;
  if (state && state->threaded) {
    {
      std::lock_guard<std::mutex> lock(state->mutex);
      state->targetBuffer = seconds;
    }
    state->wake.notify_one();
  }
  if (soundtrack)
    soundtrack->setBufferSeconds(seconds);
}

void MediaPlayer::update(RendererRef renderer, double position) {
  if (!std::isfinite(position) || position < 0)
    throw std::invalid_argument(
        "Media position must be finite and non-negative");
  if (!state)
    return;
  if (soundtrack)
    soundtrack->update(renderer, position);
  auto &s = *state;
#ifdef __EMSCRIPTEN__
  if (s.webVideo) {
    s.serviceWebVideo();
    if (s.webVideoError()) {
      auto next = std::make_unique<State>();
      const std::string channelName =
          s.channel ? s.channel->getName() : "video";
      std::string localPath = s.webFallbackPath;
      if (localPath.empty() || !std::filesystem::is_regular_file(localPath) ||
          std::filesystem::file_size(localPath) == 0) {
        localPath.clear();
        if (s.assetStream && s.assetStream->seek(0))
          next->attachAssetStream(std::move(s.assetStream));
        else if (!s.bytes.empty())
          next->attachMemory(std::move(s.bytes));
        else
          throw std::runtime_error(
              "HTML5 video failed and local media bytes are unavailable: " +
              (s.webFallbackId.empty() ? s.webVideoId : s.webFallbackId));
      }
      openDecoder(*next, localPath, s.webAudioOnly);
      const bool wasPaused = pausedAt != 0;
      adoptPreparedInput(
          std::unique_ptr<PreparedInput>(new PreparedInput(std::move(next))),
          channelName);
      clockOffset = position;
      if (!wasPaused)
        setPaused(false);
      SDL_Log("[Kotonoha] HTML5 video failed; decoding locally with FFmpeg");
      update(renderer, position);
      return;
    }
    if (s.webVideoStarted)
      EM_ASM(
          {
            if (typeof KotonohaMediaSetGain == "function")
              KotonohaMediaSetGain($0, $2);
            if (typeof KotonohaMediaSync == "function")
              KotonohaMediaSync($0, $1);
          },
          s.webVideoSession, position, audioGain());
    if (s.webVideoStarted && !s.webAudioOnly) {
      const int width = EM_ASM_INT({
        return typeof KotonohaMediaVideoWidth == "function"
                   ? KotonohaMediaVideoWidth($0) : 0;
      }, s.webVideoSession);
      const int height = EM_ASM_INT({
        return typeof KotonohaMediaVideoHeight == "function"
                   ? KotonohaMediaVideoHeight($0) : 0;
      }, s.webVideoSession);
      if (width > 0 && height > 0) {
        if (width > 4096 || height > 4096) {
          s.webVideoFailed = true;
          return;
        }
        if (!s.texture || s.width != width || s.height != height) {
          s.texture.reset(SDL_CreateTexture(&renderer, SDL_PIXELFORMAT_RGBA32,
                                            SDL_TEXTUREACCESS_STREAMING, width,
                                            height));
          if (!s.texture)
            throw std::runtime_error(SDL_GetError());
          s.width = width;
          s.height = height;
          s.aspect = static_cast<double>(width) / height;
        }
        std::vector<Uint8> pixels(static_cast<std::size_t>(width) * height * 4);
        const bool copied = EM_ASM_INT({
          return typeof KotonohaMediaCopyVideoFrame == "function" &&
                 KotonohaMediaCopyVideoFrame($0,
                     HEAPU8.subarray($1, $1 + $2)) ? 1 : 0;
        }, s.webVideoSession, pixels.data(), static_cast<int>(pixels.size())) != 0;
        if (copied) {
          if (!SDL_UpdateTexture(s.texture.get(), nullptr, pixels.data(),
                                 width * 4))
            throw std::runtime_error(SDL_GetError());
          s.textureSerial = ++nextTextureSerial;
        }
      }
    }
    return;
  }
#endif
  std::lock_guard<std::mutex> lock(s.mutex);
  if (!s.failure.empty()) {
    const auto failure = std::move(s.failure);
    s.failure.clear();
    s.eof = true;
    throw std::runtime_error(failure);
  }
  bool jump;
  bool forwardCatchup = false;
  if (s.threaded && s.queuedEnd >= 0) {
    // The audio thread keeps playing through frame hitches; resync only when
    // the timeline really diverges from the audible position.
    const double head = s.playhead();
    jump = position < head - 0.3 || (!s.eof && position > head + 0.3);
    forwardCatchup = position > head + 0.3;
  } else {
    // Small offsets catch up by dropping frames/samples instead of seeking.
    jump = (s.lastPosition < 0 && position > 0.5) ||
           (s.lastPosition >= 0 &&
            (position < s.lastPosition || position - s.lastPosition > 0.5));
    forwardCatchup = position > s.lastPosition;
  }
  if (jump) {
    const auto target =
        static_cast<int64_t>((position + s.origin) * AV_TIME_BASE);
    check(avformat_seek_file(s.format, -1, INT64_MIN, target, target, 0),
          "Seek media to timeline position");
    if (s.video)
      avcodec_flush_buffers(s.video);
    if (s.audio) {
      avcodec_flush_buffers(s.audio);
      swr_close(s.resampler);
      check(swr_init(s.resampler), "Reset audio resampler after seek");
      if (!SDL_ClearAudioStream(s.stream.get()))
        throw std::runtime_error(SDL_GetError());
    }
    av_packet_unref(s.packet);
    s.frames.clear();
    // A forward catch-up seek can briefly have no decoded replacement frame.
    // Keep the last drawable texture until upload() replaces it, matching the
    // old Canvas WAITING/DRAW_LAST retention during video starvation.  A
    // backward seek still invalidates the old visual immediately.
    if (!forwardCatchup)
      s.texture.reset();
    s.nextVideoTime = s.nextAudioTime = position;
    s.queuedEnd = -1;
    s.eof = false;
  }
  s.lastPosition = position;
  const double lookAhead =
      bufferSeconds_ > 0.0 ? bufferSeconds_ : (s.threaded ? 0.5 : 0.35);
  if (s.threaded) {
    // An audio/video decoder thread may not have run before the first render
    // update. Preserve the original first-frame contract: bootstrap a bounded
    // buffer synchronously once, under the same mutex as the producer, rather
    // than briefly showing an empty frame or relying on scheduler timing.
    if (s.video && s.queuedEnd < 0 && !s.eof)
      s.fill(position, std::min(4.0, 0.75 * playbackRate_));
    s.wake.notify_one();
  } else {
    s.fill(position, lookAhead);
  }
  Frame latest;
  while (!s.frames.empty() && s.frames.front().at <= position) {
    latest = std::move(s.frames.front().frame);
    s.frames.pop_front();
  }
  if (latest)
    s.upload(renderer, latest.get());
}

bool MediaPlayer::hasVideoFrame() const {
  if (!state)
    return false;
#ifdef __EMSCRIPTEN__
  if (state->webVideo && state->webAudioOnly)
    return false;
  if (state->webVideo)
    return state->texture != nullptr;
#endif
  return state->texture != nullptr;
}

double MediaPlayer::bufferedAudioSeconds() const {
  if (!state)
    return 0;
#ifdef __EMSCRIPTEN__
  if (state->webVideo)
    return 0;
#endif
  std::lock_guard<std::mutex> lock(state->mutex);
  return state->queuedAudio();
}

int MediaPlayer::audioCorrections() const {
  if (!state)
    return 0;
#ifdef __EMSCRIPTEN__
  if (state->webVideo)
    return 0;
#endif
  std::lock_guard<std::mutex> lock(state->mutex);
  return state->audioCorrections;
}

bool MediaPlayer::isOpen() const { return state != nullptr; }

bool MediaPlayer::finished() const {
  if (!state)
    return true;
#ifdef __EMSCRIPTEN__
  if (state->webVideo) {
    if (state->webVideoError())
      return false; // update() still needs to try the local decoder.
    if (!state->webVideoReady)
      return false;
    const bool ended =
        EM_ASM_INT(
            {
              return typeof KotonohaMediaFinished == "function" &&
                             KotonohaMediaFinished($0)
                         ? 1
                         : 0;
            },
            state->webVideoSession) != 0;
    return ended && (!soundtrack || soundtrack->finished());
  }
#endif
  std::unique_lock<std::mutex> lock(state->mutex);
  if (!(state->eof && state->frames.empty() &&
        (!state->stream || state->queuedAudio() <= 0)))
    return false;
  lock.unlock();
  return !soundtrack || soundtrack->finished();
}

double MediaPlayer::duration() const {
#ifdef __EMSCRIPTEN__
  if (state && state->webVideo) {
    const double value = EM_ASM_DOUBLE(
        {
          return typeof KotonohaMediaDuration == "function"
                     ? KotonohaMediaDuration($0)
                     : -1.0;
        },
        state->webVideoSession);
    return std::isfinite(value) && value > 0 ? value : -1;
  }
#endif
  if (!state || state->format->duration == AV_NOPTS_VALUE)
    return -1;
  return state->format->duration / static_cast<double>(AV_TIME_BASE);
}

void MediaPlayer::render(RendererRef renderer, Upscaling *upscaler) {
  render(renderer, SDL_FRect{0, 0, UI::width, UI::height}, upscaler);
}

void MediaPlayer::render(RendererRef renderer, const SDL_FRect &area,
                         Upscaling *upscaler, bool stretch) {
  if (!hasVideoFrame())
    return;
  if (!upscaler)
    upscaler = Upscaling::forRenderer(renderer);
  float w = area.w, h = area.w / static_cast<float>(state->aspect);
  if (stretch) {
    h = area.h;
  } else if (h > area.h) {
    h = area.h;
    w = area.h * static_cast<float>(state->aspect);
  }
  const SDL_FRect rect{area.x + (area.w - w) / 2, area.y + (area.h - h) / 2, w,
                       h};
  auto &texture = upscaler
                      ? upscaler->process(*state->texture, state->textureSerial)
                      : *state->texture;
  if (!SDL_RenderTexture(&renderer, &texture, nullptr, &rect))
    throw std::runtime_error(SDL_GetError());
}

} // namespace Kotonoha
