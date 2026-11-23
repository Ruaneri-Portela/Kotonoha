#pragma once

#include <string>
#include <vector>

namespace Kotonoha {
namespace workers {

enum class SubtitleSourceKind { None, Forced, Sidecar, Embedded, Default };

struct SubtitleSourceSelection {
  SubtitleSourceKind kind = SubtitleSourceKind::None;
  std::string script;
  // Only sidecar/embedded sources use their own clock (the video playback
  // clock). Forced and default sources follow the engine/scene clock.
  double clock = 0.0;
};

// Composes the four subtitle sources with a fixed priority:
//
//   forced (.ass for the active node/event)  >  sidecar (.ass next to the
//   opened video)  >  embedded (FFmpeg subtitle stream)  >  default
//   (script/VM cues)  >  none
//
// Only the winning script is loaded into libass. A lower-priority source is
// kept intact and takes over again as soon as the higher one is cleared, so
// the sources are composable rather than mutually destructive.
class SubtitleSourceDirector {
public:
  void setForced(std::string script);
  void clearForced();
  void setSidecar(std::string script, double videoSeconds);
  void clearSidecar();
  void setEmbedded(std::string script, double videoSeconds);
  void clearEmbedded();
  void setDefault(std::string script);
  void clearDefault();
  void setVideoClock(double seconds);

  SubtitleSourceSelection select() const;
  bool hasAny() const;

  // Same basename as the video, extension replaced with .ass (case-insensitive
  // extension rule). Returns both lowercase and uppercase candidates because
  // virtual providers are not required to be case-insensitive.
  static std::vector<std::string> sidecarIdsForVideo(
      const std::string &videoAssetId);

private:
  struct Slot {
    std::string script;
    bool present = false;
    double clock = 0.0;
  };
  Slot forced_, sidecar_, embedded_, default_;
};

const char *subtitleSourceName(SubtitleSourceKind kind);

} // namespace workers
} // namespace Kotonoha
