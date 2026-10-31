#include <Kotonoha/core/AssetProvider.hpp>
#include <Kotonoha/core/UI.hpp>
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
#include <cmath>
#include <deque>
#include <filesystem>
#include <mutex>
#include <stdexcept>
#include <vector>

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

} // namespace

struct MediaPlayer::State {
  std::vector<Uint8> bytes;
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
  Uint64 textureSerial = 0;
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
  // Audio-only media is refilled from SDL's audio thread so frame hitches do
  // not starve the device; the mutex guards the decoder state.
  mutable std::mutex mutex;
  bool threaded = false;
  std::string failure;
  struct VideoFrame {
    double at;
    Frame frame;
  };
  std::deque<VideoFrame> frames;

  ~State() {
    if (stream)
      SDL_SetAudioStreamGetCallback(stream.get(), nullptr, nullptr);
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
    const auto count = std::min<std::size_t>(
        static_cast<std::size_t>(size), self.bytes.size() - self.offset);
    if (count == 0)
      return AVERROR_EOF;
    std::copy_n(self.bytes.data() + self.offset, count, buffer);
    self.offset += count;
    return static_cast<int>(count);
  }

  static int64_t seekMemory(void *opaque, int64_t position, int whence) {
    auto &self = *static_cast<State *>(opaque);
    const auto size = static_cast<int64_t>(self.bytes.size());
    if (whence & AVSEEK_SIZE)
      return size;
    int64_t base = 0;
    switch (whence & ~AVSEEK_FORCE) {
    case SEEK_SET: break;
    case SEEK_CUR: base = static_cast<int64_t>(self.offset); break;
    case SEEK_END: base = size; break;
    default: return AVERROR(EINVAL);
    }
    const int64_t target = base + position;
    if (target < 0 || target > size)
      return AVERROR(EINVAL);
    self.offset = static_cast<std::size_t>(target);
    return target;
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
        // Keep the queue continuous: realign only on open/seek or real drift,
        // never on per-frame clock jitter, which would chop the waveform.
        const double expected = position + queuedAudio();
        if (queuedEnd < 0 || std::abs(queuedEnd - expected) > 0.15) {
          if (queuedEnd >= 0)
            ++audioCorrections;
          queuedEnd = expected;
        }
        const double gap = at - queuedEnd;
        if (gap > 0.002) {
          // Preserve stream offsets without allocating unbounded silence.
          if (gap > 30)
            throw std::runtime_error("Audio timestamp gap exceeds 30 seconds");
          const auto silence = static_cast<std::size_t>(gap * 48000);
          putAudio(std::vector<float>(silence * 2));
          queuedEnd += silence / 48000.0;
        }
        const auto skipped = static_cast<std::size_t>(std::max(
            0.0, std::min(-gap * 48000, static_cast<double>(count))));
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
      // Bound buffering for interleaved streams with severely unequal timestamps.
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
    auto &self = *static_cast<State *>(userdata);
    // Never block the audio thread; the main thread may hold the decoder.
    std::unique_lock<std::mutex> lock(self.mutex, std::try_to_lock);
    if (!lock.owns_lock() || self.queuedEnd < 0 || self.eof || !self.failure.empty())
      return;
    try {
      self.fill(self.playhead(), 0.5);
    } catch (const std::exception &error) {
      self.failure = error.what();
    }
  }

  void upload(RendererRef renderer, AVFrame *image) {
    if (!texture || width != image->width || height != image->height) {
      width = image->width;
      height = image->height;
      const double sar = image->sample_aspect_ratio.num > 0 &&
                                 image->sample_aspect_ratio.den > 0
                             ? av_q2d(image->sample_aspect_ratio)
                             : 1.0;
      aspect = width * sar / height;
      texture.reset(SDL_CreateTexture(&renderer, SDL_PIXELFORMAT_RGBA32,
                                      SDL_TEXTUREACCESS_STREAMING, width,
                                      height));
      if (!texture)
        throw std::runtime_error(SDL_GetError());
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

void MediaPlayer::open(const std::string &path, bool audioOnly,
                       const std::string &channelName) {
  auto channel = mixer.channel(
      channelName.empty() ? (audioOnly ? "audio" : "video") : channelName);
  if (!std::filesystem::is_regular_file(path))
    throw std::runtime_error("Media must be a local file: " + path);
  auto next = std::make_unique<State>();
  next->channel = std::move(channel);
  start(std::move(next), path, audioOnly);
}

void MediaPlayer::open(const AssetProvider &assets, const std::string &id,
                       bool audioOnly, const std::string &channelName) {
  const std::string path = assets.localPath(id);
  if (!path.empty() && std::filesystem::is_regular_file(path))
    return open(path, audioOnly, channelName);
  auto next = std::make_unique<State>();
  next->channel = mixer.channel(
      channelName.empty() ? (audioOnly ? "audio" : "video") : channelName);
  next->attachMemory(assets.read(id));
  start(std::move(next), {}, audioOnly);
}

void MediaPlayer::start(std::unique_ptr<State> next, const std::string &url,
                        bool audioOnly) {
  AVDictionary *options = nullptr;
  check(av_dict_set(&options, "protocol_whitelist", "file", 0),
        "Configure local media access");
  const int opened = avformat_open_input(
      &next->format, next->io ? nullptr : url.c_str(), nullptr, &options);
  av_dict_free(&options);
  check(opened, "Open media");
  check(avformat_find_stream_info(next->format, nullptr), "Read media streams");
  if (next->format->start_time != AV_NOPTS_VALUE)
    next->origin = next->format->start_time / static_cast<double>(AV_TIME_BASE);
  if (!audioOnly)
    next->video = next->decoder(next->videoIndex, AVMEDIA_TYPE_VIDEO, true);
  next->audio = next->decoder(next->audioIndex, AVMEDIA_TYPE_AUDIO, audioOnly);
  next->packet = av_packet_alloc();
  if (!next->packet || !next->frame)
    throw std::bad_alloc();
  if (next->audio) {
    AVChannelLayout stereo = AV_CHANNEL_LAYOUT_STEREO;
    check(swr_alloc_set_opts2(&next->resampler, &stereo, AV_SAMPLE_FMT_FLT,
                              48000, &next->audio->ch_layout,
                              next->audio->sample_fmt, next->audio->sample_rate,
                              0, nullptr),
          "Configure audio conversion");
    check(swr_init(next->resampler), "Initialize audio conversion");
    const SDL_AudioSpec spec{SDL_AUDIO_F32, 2, 48000};
    next->stream.reset(SDL_OpenAudioDeviceStream(
        SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, nullptr, nullptr));
    if (!next->stream)
      throw std::runtime_error(SDL_GetError());
    next->channel->attach(next->stream.get());
    if (!next->video) {
      next->threaded = true;
      if (!SDL_SetAudioStreamGetCallback(next->stream.get(), State::feed, next.get()))
        throw std::runtime_error(SDL_GetError());
    }
    if (!SDL_ResumeAudioStreamDevice(next->stream.get()))
      throw std::runtime_error(SDL_GetError());
  }
  soundtrack.reset();
  state = std::move(next);
  clockStart = SDL_GetTicksNS();
  pausedAt = 0;
}

void MediaPlayer::openSoundtrack(const AssetProvider &assets,
                                 const std::string &id,
                                 const std::string &channelName) {
  if (!state)
    throw std::logic_error("Open media before its soundtrack");
  auto next = std::make_unique<MediaPlayer>(mixer);
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
}

double MediaPlayer::elapsed() const {
  if (!state)
    return 0;
  const Uint64 now = pausedAt ? pausedAt : SDL_GetTicksNS();
  return (now - clockStart) / 1e9;
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
  if (state && state->stream) {
    const bool ok = paused ? SDL_PauseAudioStreamDevice(state->stream.get())
                           : SDL_ResumeAudioStreamDevice(state->stream.get());
    if (!ok)
      throw std::runtime_error(SDL_GetError());
  }
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
  std::lock_guard<std::mutex> lock(s.mutex);
  if (!s.failure.empty()) {
    const auto failure = std::move(s.failure);
    s.failure.clear();
    s.eof = true;
    throw std::runtime_error(failure);
  }
  bool jump;
  if (s.threaded && s.queuedEnd >= 0) {
    // The audio thread keeps playing through frame hitches; resync only when
    // the timeline really diverges from the audible position.
    const double head = s.playhead();
    jump = position < head - 0.3 || (!s.eof && position > head + 0.3);
  } else {
    // Small offsets catch up by dropping frames/samples instead of seeking.
    jump = (s.lastPosition < 0 && position > 0.5) ||
           (s.lastPosition >= 0 &&
            (position < s.lastPosition || position - s.lastPosition > 0.5));
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
    s.texture.reset();
    s.nextVideoTime = s.nextAudioTime = position;
    s.queuedEnd = -1;
    s.eof = false;
  }
  s.lastPosition = position;
  if (s.threaded && s.queuedEnd >= 0)
    s.fill(s.playhead(), 0.5);
  else
    s.fill(position, s.threaded ? 0.5 : 0.35);
  Frame latest;
  while (!s.frames.empty() && s.frames.front().at <= position) {
    latest = std::move(s.frames.front().frame);
    s.frames.pop_front();
  }
  if (latest)
    s.upload(renderer, latest.get());
}

bool MediaPlayer::hasVideoFrame() const { return state && state->texture; }

double MediaPlayer::bufferedAudioSeconds() const {
  if (!state)
    return 0;
  std::lock_guard<std::mutex> lock(state->mutex);
  return state->queuedAudio();
}

int MediaPlayer::audioCorrections() const {
  if (!state)
    return 0;
  std::lock_guard<std::mutex> lock(state->mutex);
  return state->audioCorrections;
}

bool MediaPlayer::isOpen() const { return state != nullptr; }

bool MediaPlayer::finished() const {
  if (!state)
    return true;
  std::unique_lock<std::mutex> lock(state->mutex);
  if (!(state->eof && state->frames.empty() &&
        (!state->stream || state->queuedAudio() <= 0)))
    return false;
  lock.unlock();
  return !soundtrack || soundtrack->finished();
}

double MediaPlayer::duration() const {
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
