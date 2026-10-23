#include "MoviePlayer.hpp"

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavutil/imgutils.h>
#include <libswscale/swscale.h>
}

namespace ZeroNoTsukaima {

MoviePlayer::~MoviePlayer() { Close(); }

void MoviePlayer::Close() {
  audio_.Stop();
  hasAudio_ = false;
  image_.Reset(nullptr);
  if (parser_) av_parser_close(parser_);
  if (codec_) avcodec_free_context(&codec_);
  if (frame_) av_frame_free(&frame_);
  if (packet_) av_packet_free(&packet_);
  parser_ = nullptr;
  movie_ = PssMovie{};
  finished_ = true;
}

bool MoviePlayer::Open(const std::vector<uint8_t> &pss, SDL_Renderer *renderer, Kotonoha::Sound::Channel *audio,
                       float volume) {
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
  if (!movie_.pcm.empty() && movie_.rate > 0 && movie_.channels > 0) {
    duration_ = double(movie_.pcm.size() / size_t(movie_.channels)) / movie_.rate;
    Kotonoha::PcmPlayer::Clip clip;
    clip.samples = std::move(movie_.pcm);
    clip.channels = movie_.channels;
    clip.rate = movie_.rate;
    clip.gain = volume;
    hasAudio_ = audio_.Play(audio, std::move(clip));
    if (!hasAudio_) duration_ = 0.0;
  }
  std::vector<int16_t>().swap(movie_.pcm);
  SDL_Log("[movie] %zu video packets, audio %.1f s @%d Hz", movie_.video.size(), duration_, movie_.rate);
  startTicks_ = SDL_GetTicksNS();
  finished_ = false;
  return true;
}

double MoviePlayer::Clock() const {
  if (hasAudio_) return audio_.Position();
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
  if (!frame_ || frame_->width <= 0 || frame_->height <= 0) return;

  static SwsContext *swsCtx = nullptr;
  swsCtx = sws_getCachedContext(
      swsCtx, frame_->width, frame_->height,
      (enum AVPixelFormat)frame_->format, frame_->width, frame_->height,
      AV_PIX_FMT_RGBA, SWS_BILINEAR, NULL, NULL, NULL);
  if (!swsCtx) return;

  static AVFrame *pFrameRGB = nullptr;
  if (!pFrameRGB) pFrameRGB = av_frame_alloc();
  if (!pFrameRGB) return;

  const int bufferSize = av_image_get_buffer_size(AV_PIX_FMT_RGBA, frame_->width, frame_->height, 1);
  static std::vector<uint8_t> rgbBuffer;
  if (rgbBuffer.size() < size_t(bufferSize)) {
    rgbBuffer.resize(bufferSize);
  }

  av_image_fill_arrays(pFrameRGB->data, pFrameRGB->linesize, rgbBuffer.data(),
                       AV_PIX_FMT_RGBA, frame_->width, frame_->height, 1);

  sws_scale(swsCtx, (const uint8_t *const *)frame_->data, frame_->linesize,
            0, frame_->height, pFrameRGB->data, pFrameRGB->linesize);

  SDL_Texture *tex = image_.Source();
  if (tex && (tex->w != frame_->width || tex->h != frame_->height)) tex = nullptr;
  if (!tex) {
    tex = SDL_CreateTexture(renderer_, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STREAMING,
                            frame_->width, frame_->height);
    if (tex) SDL_SetTextureBlendMode(tex, SDL_BLENDMODE_BLEND);
    image_.Reset(tex);
  }
  if (!tex) return;

  SDL_UpdateTexture(tex, nullptr, pFrameRGB->data[0], pFrameRGB->linesize[0]);
  image_.Touch();
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
  if (videoDone && (!hasAudio_ || !audio_.Playing())) finished_ = true;
}

void MoviePlayer::Draw(SDL_Renderer *renderer, const SDL_FRect &dst) { image_.Draw(renderer, nullptr, &dst); }

} // namespace ZeroNoTsukaima
