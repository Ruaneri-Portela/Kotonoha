#include <Kotonoha/media/AudioMixer.hpp>
#include <Kotonoha/script/Timeline.hpp>

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <limits>
#include <sstream>
#include <stdexcept>

namespace Kotonoha {

bool timelineReached(double position, double target) {
  const double tolerance =
      8 * std::numeric_limits<double>::epsilon() *
      std::max({1.0, std::abs(position), std::abs(target)});
  return position >= target || target - position <= tolerance;
}

void TimelineScript::validate() const {
  if (!std::isfinite(duration) || duration <= 0)
    throw std::invalid_argument(
        "Timeline duration must be positive and finite");
  for (std::size_t i = 0; i < cues.size(); ++i) {
    const auto &cue = cues[i];
    if (!std::isfinite(cue.at) || cue.at < 0 || !std::isfinite(cue.duration) ||
        cue.duration <= 0 || cue.at >= duration ||
        !std::isfinite(cue.at + cue.duration) ||
        !timelineReached(duration, cue.at + cue.duration) ||
        ((cue.type == CueType::Video || cue.type == CueType::Audio) &&
         cue.value.empty()))
      throw std::invalid_argument("Invalid timeline cue " + std::to_string(i));
    if (cue.type != CueType::Video && cue.type != CueType::Audio &&
        cue.type != CueType::Subtitle && cue.type != CueType::Dialogue &&
        cue.type != CueType::Choice)
      throw std::invalid_argument("Invalid timeline cue type");
    if (!cue.channelName.empty()) {
      if (cue.type != CueType::Video && cue.type != CueType::Audio)
        throw std::invalid_argument(
            "Only audio/video cues accept an audio channel");
      AudioMixer::validateChannelName(cue.channelName);
    }
    if (cue.type == CueType::Choice) {
      if (cue.choices.empty() || (cue.timeout != ChoiceTimeout::Pause &&
                                  cue.timeout != ChoiceTimeout::Continue &&
                                  cue.timeout != ChoiceTimeout::Select))
        throw std::invalid_argument("Invalid timed choice");
      if (cue.timeout == ChoiceTimeout::Select && cue.timeoutTargetNodeId < -1)
        throw std::invalid_argument("Invalid timeout target");
      for (const auto &option : cue.choices)
        if (option.targetNodeId < -1)
          throw std::invalid_argument("Invalid timed choice option");
    }
    for (std::size_t j = 0; j < i; ++j) {
      const auto &other = cues[j];
      const bool sameAudioChannel =
          cue.type != CueType::Audio ||
          (cue.channelName.empty() ? "audio" : cue.channelName) ==
              (other.channelName.empty() ? "audio" : other.channelName);
      if (cue.type != CueType::Subtitle && cue.type == other.type &&
          sameAudioChannel &&
          !timelineReached(cue.at, other.at + other.duration) &&
          !timelineReached(other.at, cue.at + cue.duration))
        throw std::invalid_argument(
            "Overlapping cues on the same timeline track");
    }
  }
}

TimelineScript TimelineScript::load(const std::string &path) {
  std::ifstream file(path);
  if (!file)
    throw std::runtime_error("Cannot open timeline: " + path);
  TimelineScript script;
  bool hasDuration = false;
  std::string line;
  std::size_t number = 0;
  while (std::getline(file, line)) {
    ++number;
    std::istringstream input(line);
    std::string command;
    if (!(input >> command) || command[0] == '#')
      continue;
    auto fail = [&]() {
      throw std::runtime_error(path + ":" + std::to_string(number) +
                               ": invalid timeline syntax");
    };
    if (command == "duration") {
      if (hasDuration || !(input >> script.duration))
        fail();
      hasDuration = true;
    } else if (command == "at") {
      TimelineCue cue;
      std::string type;
      if (!(input >> cue.at >> type >> cue.duration))
        fail();
      if (type == "video")
        cue.type = CueType::Video;
      else if (type == "audio")
        cue.type = CueType::Audio;
      else if (type == "subtitle")
        cue.type = CueType::Subtitle;
      else if (type == "dialogue")
        cue.type = CueType::Dialogue;
      else if (type == "choice")
        cue.type = CueType::Choice;
      else
        fail();
      auto quoted = [&](std::string &text) {
        input >> std::ws;
        if (input.peek() != '"' || !(input >> std::quoted(text)))
          fail();
      };
      if (cue.type == CueType::Dialogue)
        quoted(cue.speaker);
      quoted(cue.value);
      if (cue.type == CueType::Choice) {
        std::string policy;
        if (!(input >> policy))
          fail();
        if (policy == "pause")
          cue.timeout = ChoiceTimeout::Pause;
        else if (policy == "continue")
          cue.timeout = ChoiceTimeout::Continue;
        else if (policy == "select") {
          cue.timeout = ChoiceTimeout::Select;
          if (!(input >> cue.timeoutTargetNodeId))
            fail();
        } else
          fail();
        int count = 0;
        if (!(input >> count) || count <= 0 || count > 10000)
          fail();
        for (int i = 0; i < count; ++i) {
          TimedChoiceOption option;
          quoted(option.label);
          if (!(input >> option.targetNodeId))
            fail();
          cue.choices.push_back(std::move(option));
        }
      }
      if (cue.type == CueType::Audio || cue.type == CueType::Video) {
        input >> std::ws;
        if (!input.eof() && input.peek() != '#') {
          std::string channel;
          if (!(input >> channel) || channel != "channel")
            fail();
          quoted(cue.channelName);
          if (cue.channelName.empty())
            fail();
        }
        cue.value = (std::filesystem::path(path).parent_path() / cue.value)
                        .lexically_normal()
                        .string();
      }
      script.cues.push_back(std::move(cue));
    } else {
      fail();
    }
    input >> std::ws;
    if (!input.eof() && input.peek() != '#')
      fail();
  }
  if (file.bad())
    throw std::runtime_error("Cannot read timeline: " + path);
  script.validate();
  std::stable_sort(
      script.cues.begin(), script.cues.end(),
      [](const TimelineCue &a, const TimelineCue &b) { return a.at < b.at; });
  return script;
}

} // namespace Kotonoha
