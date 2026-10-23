#include "MoviePlayer.hpp"

extern "C" {
#include <libavcodec/avcodec.h>
}

namespace ZeroNoTsukaima {

MoviePlayer::~MoviePlayer() { Close(); }

void MoviePlayer::Close() {
  if (audio_) SDL_DestroyAudioStream(audio_);
  if (texture_) SDL_DestroyTexture(texture_);
  if (parser_) av_parser_close(parser_);
  if (codec_) avcodec_free_context(&codec_);
  if (frame_) av_frame_free(&frame_);
  if (packet_) av_packet_free(&packet_);
  audio_ = nullptr;
  texture_ = nullptr;
  parser_ = nullptr;
  movie_ = PssMovie{};
  finished_ = true;
}

bool MoviePlayer::Open(const std::vector<uint8_t> &pss, SDL_Renderer *renderer, float volume) {
  Close();
  renderer_ = renderer;
  if (!DemuxPss(pss.data(), pss.size(), &movie_)) return false;
  const AVCodec *codec = avcodec_find_decoder(AV_CODEC_ID_MPEG2VIDEO);
  codec_ = codec ? avcodec_alloc_context3(codec) : nullptr;
  parser_ = av_parser_init(AV_CODEC_ID_MPEG2VIDEO);
  frame_ = av_frame_alloc();
  packet_ = av_packet_alloc();
  if (!codec_ || !parser_ || !frame_ || !packet_ || avcodec_open2(codec_, codec, nullptr) < 0) {
    Close();
    return false;
  }
  nextPacket_ = packetOffset_ = 0;
  flushed_ = pending_ = false;
  frameIndex_ = 0;
  fps_ = 30.0;
  duration_ = 0.0;
  audioBytes_ = 0;
  if (!movie_.pcm.empty() && movie_.rate > 0) {
    const SDL_AudioSpec spec{SDL_AUDIO_S16, movie_.channels, movie_.rate};
    audio_ = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, nullptr, nullptr);
    if (audio_) {
      audioBytes_ = int64_t(movie_.pcm.size() * sizeof(int16_t));
      SDL_SetAudioStreamGain(audio_, volume);
      SDL_PutAudioStreamData(audio_, movie_.pcm.data(), int(audioBytes_));
      SDL_FlushAudioStream(audio_);
      SDL_ResumeAudioStreamDevice(audio_);
      duration_ = double(movie_.pcm.size() / size_t(movie_.channels)) / movie_.rate;
    }
    std::vector<int16_t>().swap(movie_.pcm);
  }
  SDL_Log("[movie] %zu video packets, audio %.1f s @%d Hz", movie_.video.size(), duration_, movie_.rate);
  startTicks_ = SDL_GetTicksNS();
  finished_ = false;
  return true;
}

double MoviePlayer::Clock() const {
  if (audio_) {
    const int64_t queued = SDL_GetAudioStreamQueued(audio_);
    return double(audioBytes_ - queued) / (double(movie_.rate) * movie_.channels * 2.0);
  }
  return double(SDL_GetTicksNS() - startTicks_) / 1e9;
}

bool MoviePlayer::DecodeNext() {
  for (;;) {
    const int r = avcodec_receive_frame(codec_, frame_);
    if (r == 0) return true;
    if (r != AVERROR(EAGAIN)) return false;
    if (flushed_) return false;
    uint8_t *out = nullptr;
    int outSize = 0;
    if (nextPacket_ < movie_.video.size()) {
      const auto &pkt = movie_.video[nextPacket_];
      const int used = av_parser_parse2(parser_, codec_, &out, &outSize, pkt.data.data() + packetOffset_,
                                        int(pkt.data.size() - packetOffset_), AV_NOPTS_VALUE, AV_NOPTS_VALUE, 0);
      packetOffset_ += size_t(used < 0 ? pkt.data.size() : size_t(used));
      if (packetOffset_ >= pkt.data.size()) {
        ++nextPacket_;
        packetOffset_ = 0;
      }
    } else {
      av_parser_parse2(parser_, codec_, &out, &outSize, nullptr, 0, AV_NOPTS_VALUE, AV_NOPTS_VALUE, 0);
      if (outSize <= 0) {
        avcodec_send_packet(codec_, nullptr);
        flushed_ = true;
        continue;
      }
    }
    if (outSize > 0) {
      packet_->data = out;
      packet_->size = outSize;
      avcodec_send_packet(codec_, packet_);
    }
  }
}

void MoviePlayer::Upload() {
  if (frame_->format != AV_PIX_FMT_YUV420P) return;
  if (texture_ && (texture_->w != frame_->width || texture_->h != frame_->height)) {
    SDL_DestroyTexture(texture_);
    texture_ = nullptr;
  }
  if (!texture_)
    texture_ = SDL_CreateTexture(renderer_, SDL_PIXELFORMAT_IYUV, SDL_TEXTUREACCESS_STREAMING, frame_->width,
                                 frame_->height);
  if (texture_)
    SDL_UpdateYUVTexture(texture_, nullptr, frame_->data[0], frame_->linesize[0], frame_->data[1], frame_->linesize[1],
                         frame_->data[2], frame_->linesize[2]);
}

void MoviePlayer::Update() {
  if (finished_) return;
  const double clock = Clock();
  bool videoDone = false;
  for (;;) {
    if (!pending_) {
      if (!DecodeNext()) {
        videoDone = true;
        break;
      }
      pending_ = true;
      if (codec_->framerate.num > 0 && codec_->framerate.den > 0) fps_ = av_q2d(codec_->framerate);
    }
    if (double(frameIndex_) / fps_ > clock) break;
    Upload();
    ++frameIndex_;
    pending_ = false;
  }
  if (videoDone && (!audio_ || SDL_GetAudioStreamQueued(audio_) == 0)) finished_ = true;
}

void MoviePlayer::Draw(SDL_Renderer *renderer, const SDL_FRect &dst) const {
  if (texture_) SDL_RenderTexture(renderer, texture_, nullptr, &dst);
}

} // namespace ZeroNoTsukaima
