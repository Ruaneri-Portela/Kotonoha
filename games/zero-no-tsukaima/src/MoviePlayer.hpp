#pragma once
#include <ZeroNoTsukaima/Pss.hpp>

#include <Kotonoha/components/CanvasImage.hpp>
#include <Kotonoha/components/PcmPlayer.hpp>

#include <SDL3/SDL.h>

#include <cstdint>
#include <vector>

struct AVCodecContext;
struct AVCodecParserContext;
struct AVFrame;
struct AVPacket;

namespace ZeroNoTsukaima {

// Plays a demuxed .PSS movie: MPEG-2 video through libavcodec into an IYUV
// texture drawn through the engine upscaler (each decoded frame is upscaled
// once), PCM audio as a pipe of an engine Sound channel. Video frames follow
// the audio clock (wall clock when the movie has no audio).
class MoviePlayer {
public:
  MoviePlayer() = default;
  ~MoviePlayer();
  MoviePlayer(const MoviePlayer &) = delete;
  MoviePlayer &operator=(const MoviePlayer &) = delete;

  bool Open(const std::vector<uint8_t> &pss, SDL_Renderer *renderer, Kotonoha::Sound::Channel *audio, float volume);
  // Decodes up to the current clock; call once per frame.
  void Update();
  void Draw(SDL_Renderer *renderer, const SDL_FRect &dst);
  bool Finished() const { return finished_; }
  void Close();

private:
  bool DecodeNext(); // next frame into frame_, false at the end of the stream
  double Clock() const;
  void Upload(); // frame_ into image_

  PssMovie movie_;
  AVCodecContext *codec_ = nullptr;
  AVCodecParserContext *parser_ = nullptr;
  AVFrame *frame_ = nullptr;
  AVPacket *packet_ = nullptr;
  SDL_Renderer *renderer_ = nullptr;
  Kotonoha::CanvasImage image_;
  Kotonoha::PcmPlayer audio_;
  bool hasAudio_ = false;
  size_t nextPacket_ = 0, packetOffset_ = 0;
  bool flushed_ = false, pending_ = false, finished_ = true;
  int64_t frameIndex_ = 0;
  double fps_ = 30.0, duration_ = 0.0;
  uint64_t startTicks_ = 0;
};

} // namespace ZeroNoTsukaima
