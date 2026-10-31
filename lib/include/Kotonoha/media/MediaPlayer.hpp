#pragma once

#include <Kotonoha/core/SDL.hpp>
#include <Kotonoha/media/AudioMixer.hpp>
#include <memory>
#include <string>

namespace Kotonoha {
class AssetProvider;
class Upscaling;

class MediaPlayer {
  std::unique_ptr<AudioMixer> ownedMixer;
  AudioMixer &mixer;
  struct State;
  std::unique_ptr<State> state;
  std::unique_ptr<MediaPlayer> soundtrack;
  Uint64 clockStart = 0, pausedAt = 0;
  void start(std::unique_ptr<State> next, const std::string &url,
             bool audioOnly);

public:
  MediaPlayer();
  explicit MediaPlayer(AudioMixer &mixer);
  ~MediaPlayer();
  MediaPlayer(const MediaPlayer &) = delete;
  MediaPlayer &operator=(const MediaPlayer &) = delete;
  // Video includes its embedded audio; audioOnly ignores video streams.
  void open(const std::string &path, bool audioOnly,
            const std::string &channelName = {});
  // Streams the provider's local file, or decodes the asset bytes in memory.
  void open(const AssetProvider &assets, const std::string &id, bool audioOnly,
            const std::string &channelName = {});
  // Plays a separate audio asset in sync with the open media (formats whose
  // soundtrack FFmpeg cannot demux, or dubbed tracks). Replaced by open().
  void openSoundtrack(const AssetProvider &assets, const std::string &id,
                      const std::string &channelName = {});
  void setChannel(const std::string &name);
  std::shared_ptr<AudioChannel> getChannel() const;
  float audioGain() const;
  double bufferedAudioSeconds() const;
  // Number of audio drift realignments since open (diagnostics).
  int audioCorrections() const;
  void stop();
  void setPaused(bool paused);
  // Timeline-driven: decodes up to `position` seconds (seeks on jumps).
  void update(RendererRef renderer, double position);
  // Self-timed: the position is the wall clock since open(), minus pauses.
  void update(RendererRef renderer);
  double elapsed() const;
  // Letterboxed into the UI space, or into `area`, keeping the display aspect
  // (pixel aspect ratio included); `stretch` fills `area` instead.
  void render(RendererRef renderer, Upscaling *upscaler = nullptr);
  void render(RendererRef renderer, const SDL_FRect &area,
              Upscaling *upscaler = nullptr, bool stretch = false);
  bool hasVideoFrame() const;
  bool isOpen() const;
  // True when nothing is open, or every frame was presented and the queued
  // audio (soundtrack included) drained.
  bool finished() const;
  // Container duration in seconds, negative when unknown or nothing is open.
  double duration() const;
};

} // namespace Kotonoha
