#pragma once

#include <Kotonoha/core/SDL.hpp>
#include <Kotonoha/media/AudioMixer.hpp>
#include <atomic>
#include <memory>
#include <string>

namespace Kotonoha {
class AssetProvider;
class Upscaling;
namespace Filesystem { class FileHandle; }

class MediaPlayer {
  std::unique_ptr<AudioMixer> ownedMixer;
  AudioMixer &mixer;
  struct State;
  static void openDecoder(State &next, const std::string &url, bool audioOnly);
  std::unique_ptr<State> state;
  std::unique_ptr<MediaPlayer> soundtrack;
  Uint64 clockStart = 0, pausedAt = 0;
  double clockOffset = 0.0, playbackRate_ = 1.0;
  double bufferSeconds_ = -1.0;
  void start(std::unique_ptr<State> next, const std::string &url,
             bool audioOnly, bool prepared = false);

public:
  struct PreparedInput {
    ~PreparedInput();
    PreparedInput(const PreparedInput &) = delete;
    PreparedInput &operator=(const PreparedInput &) = delete;

  private:
    friend class MediaPlayer;
    explicit PreparedInput(std::unique_ptr<State> value);
    std::unique_ptr<State> state;
  };
  // CPU/I/O-only demux and decoder preparation. Safe for a bounded worker;
  // no SDL renderer, audio stream or mixer is touched until adoption.
  static std::unique_ptr<PreparedInput>
  prepareInput(const AssetProvider &assets, const std::string &id,
               bool audioOnly,
               std::shared_ptr<std::atomic_bool> cancelled = {});
  void adoptPreparedInput(std::unique_ptr<PreparedInput> input,
                          const std::string &channelName);
  MediaPlayer();
  explicit MediaPlayer(AudioMixer &mixer);
  ~MediaPlayer();
  MediaPlayer(const MediaPlayer &) = delete;
  MediaPlayer &operator=(const MediaPlayer &) = delete;
  // Video includes its embedded audio; audioOnly ignores video streams.
  // Game resources from local folders, ISO entries or other virtual mounts.
  // FFmpeg consumes this handle via custom AVIO, never a physical path.
  void open(std::unique_ptr<Filesystem::FileHandle> source, bool audioOnly,
            const std::string &channelName = {});
  void open(const std::string &path, bool audioOnly,
            const std::string &channelName = {});
  // Streams a provider local file or seekable source when available, otherwise
  // decodes the complete asset bytes in memory.
  void open(const AssetProvider &assets, const std::string &id, bool audioOnly,
            const std::string &channelName = {});
  // Opens/demuxes/constructs decoders without advancing the media clock or
  // starting the audio device. Call setPaused(false) at the semantic start.
  // This is intentionally generic so game runtimes can warm authored media
  // before an event boundary without inventing per-game decoder plumbing.
  void prepare(const AssetProvider &assets, const std::string &id,
               bool audioOnly, const std::string &channelName = {});
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
  // Generic live playback-rate control. The SDL audio stream consumes source
  // samples at the same ratio and the self-timed media clock stays continuous
  // across rate changes. Timeline-driven callers should advance their external
  // position at the same rate.
  void setPlaybackRate(double rate);
  double playbackRate() const { return playbackRate_; }
  // Optional decoder look-ahead target. A negative value preserves the engine
  // defaults (0.5s audio-only / 0.35s A/V); positive values let a host match a
  // title-specific media cache policy without leaking that policy into engine.
  void setBufferSeconds(double seconds);
  double bufferSeconds() const { return bufferSeconds_; }
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
