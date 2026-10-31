#pragma once

#include <Kotonoha/media/MediaPlayer.hpp>
#include <Kotonoha/script/Timeline.hpp>
#include <map>
#include <optional>

namespace Kotonoha {

class Engine;
class SubtitleScene;

class TimelinePlayer {
  Engine &engine;
  TimelineScript script;
  MediaPlayer video;
  std::unique_ptr<SubtitleScene> subtitles;
  struct AudioTrack {
    std::unique_ptr<MediaPlayer> player;
    std::size_t cue;
  };
  std::map<std::string, AudioTrack> audio;
  double position = 0;
  std::size_t next = 0;
  int videoCue = -1, dialogueCue = -1;
  int choiceCue = -1;
  bool running = false, paused = false;
  bool waitingForChoice = false;
  std::vector<std::size_t> resolvedChoices;
  std::optional<int> branch;
  void synchronizeMediaPause();
  void showChoice(std::size_t index);
  void resolveChoice(int target);
  void applyPosition();

public:
  explicit TimelinePlayer(Engine &engineRef);
  ~TimelinePlayer();
  void play(TimelineScript script, double position = 0,
            std::vector<std::size_t> resolved = {},
            std::optional<int> pendingBranch = std::nullopt);
  void stop();
  void setPaused(bool value);
  void update(double delta);
  void renderVideo(RendererRef renderer);
  void renderSubtitles(RendererRef renderer);
  std::string activeSubtitleText() const;
  bool isPlaying() const { return running; }
  bool hasVideoFrame() const { return video.hasVideoFrame(); }
  bool isPaused() const {
    return paused || waitingForChoice || branch.has_value();
  }
  bool isWaitingForChoice() const { return waitingForChoice; }
  const std::vector<std::size_t> &getResolvedChoices() const {
    return resolvedChoices;
  }
  std::optional<int> pendingBranch() const { return branch; }
  double seconds() const { return position; }
};

} // namespace Kotonoha
