#pragma once

#include <string>
#include <vector>

namespace Kotonoha {

bool timelineReached(double position, double target);

enum class ScriptMode { Standard, Timed };
enum class CueType { Video, Audio, Subtitle, Dialogue, Choice };
enum class ChoiceTimeout { Pause, Continue, Select };

struct TimedChoiceOption {
  std::string label;
  int targetNodeId = -1;
};

struct TimelineCue {
  double at = 0;
  double duration = 0;
  CueType type = CueType::Subtitle;
  std::string value;
  std::string speaker;
  std::vector<TimedChoiceOption> choices;
  ChoiceTimeout timeout = ChoiceTimeout::Pause;
  int timeoutTargetNodeId = -1;
  std::string channelName;
};

struct TimelineScript {
  double duration = 0;
  std::vector<TimelineCue> cues;

  void validate() const;
  static TimelineScript load(const std::string &path);
};

} // namespace Kotonoha
