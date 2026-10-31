#include <Kotonoha/componentes/ChoiceQuestionScene.hpp>
#include <Kotonoha/componentes/VNTextBox.hpp>
#include <Kotonoha/componentes/SubtitleScene.hpp>
#include <Kotonoha/core/Engine.hpp>
#include <Kotonoha/core/UI.hpp>
#include <Kotonoha/script/TimelinePlayer.hpp>

#include <algorithm>
#include <cmath>
#include <set>
#include <stdexcept>

namespace Kotonoha {

TimelinePlayer::TimelinePlayer(Engine &engineRef)
    : engine(engineRef), video(engineRef.GetAudio()) {}
TimelinePlayer::~TimelinePlayer() = default;

void TimelinePlayer::play(TimelineScript value, double start,
                          std::vector<std::size_t> resolved,
                          std::optional<int> pending) {
  value.validate();
  std::stable_sort(
      value.cues.begin(), value.cues.end(),
      [](const TimelineCue &a, const TimelineCue &b) { return a.at < b.at; });
  if (!std::isfinite(start) || start < 0 || start > value.duration ||
      (pending && *pending < -1) ||
      std::set<std::size_t>(resolved.begin(), resolved.end()).size() !=
          resolved.size())
    throw std::invalid_argument("Invalid timeline restore position");
  for (auto index : resolved)
    if (index >= value.cues.size() ||
        value.cues[index].type != CueType::Choice ||
        !timelineReached(start, value.cues[index].at))
      throw std::invalid_argument("Invalid restored choice");
  for (std::size_t i = 0; i < value.cues.size(); ++i) {
    const auto &cue = value.cues[i];
    if (cue.type == CueType::Choice &&
        timelineReached(start, cue.at + cue.duration) &&
        !timelineReached(cue.at + cue.duration, start) &&
        std::find(resolved.begin(), resolved.end(), i) == resolved.end())
      throw std::invalid_argument(
          "Unresolved choice precedes restored position");
  }
  stop();
  GetVNTextBoxRuntime(engine, "", "", 30).hide();
  GetChoiceQuestionRuntime(engine).hide();
  script = std::move(value);
  std::vector<SubtitleCue> cues;
  for (const auto &cue : script.cues)
    if (cue.type == CueType::Subtitle)
      cues.push_back({cue.at, cue.at + cue.duration, cue.value, cue.speaker});
  if (!cues.empty()) {
    if (!subtitles)
      subtitles = std::make_unique<SubtitleScene>(engine);
    subtitles->setCues(cues);
  }
  position = start;
  next = 0;
  resolvedChoices = std::move(resolved);
  branch = pending;
  paused = false;
  running = true;
  applyPosition();
  synchronizeMediaPause();
}

void TimelinePlayer::stop() {
  video.stop();
  audio.clear();
  if (dialogueCue >= 0)
    GetVNTextBoxRuntime(engine, "", "", 30).hide();
  if (choiceCue >= 0)
    GetChoiceQuestionRuntime(engine).hide();
  videoCue = dialogueCue = choiceCue = -1;
  waitingForChoice = false;
  branch.reset();
  resolvedChoices.clear();
  running = false;
  if (subtitles) subtitles->clear();
}

void TimelinePlayer::synchronizeMediaPause() {
  video.setPaused(isPaused());
  for (auto &entry : audio)
    entry.second.player->setPaused(isPaused());
}

void TimelinePlayer::setPaused(bool value) {
  paused = value;
  synchronizeMediaPause();
}

void TimelinePlayer::showChoice(std::size_t index) {
  const auto &cue = script.cues[index];
  std::vector<ChoiceOption> options;
  for (const auto &option : cue.choices)
    options.push_back({option.label, option.targetNodeId});
  choiceCue = static_cast<int>(index);
  GetChoiceQuestionRuntime(engine).setQuestion(
      cue.value, options,
      [this](int target, const std::string &) { resolveChoice(target); });
}

void TimelinePlayer::resolveChoice(int target) {
  if (choiceCue < 0)
    throw std::logic_error("No timed choice is active");
  resolvedChoices.push_back(static_cast<std::size_t>(choiceCue));
  GetChoiceQuestionRuntime(engine).hide();
  choiceCue = -1;
  waitingForChoice = false;
  branch = target;
  synchronizeMediaPause();
}

void TimelinePlayer::applyPosition() {
  auto expired = [&](int index) {
    return index >= 0 &&
           timelineReached(position,
                           script.cues[index].at + script.cues[index].duration);
  };
  if (expired(videoCue)) {
    video.stop();
    videoCue = -1;
  }
  for (auto it = audio.begin(); it != audio.end();) {
    if (expired(static_cast<int>(it->second.cue)))
      it = audio.erase(it);
    else
      ++it;
  }
  if (expired(dialogueCue)) {
    GetVNTextBoxRuntime(engine, "", "", 30).hide();
    dialogueCue = -1;
  }
  auto expireChoice = [&]() {
    if (!expired(choiceCue))
      return;
    const auto &cue = script.cues[choiceCue];
    if (cue.timeout == ChoiceTimeout::Pause) {
      waitingForChoice = true;
    } else if (cue.timeout == ChoiceTimeout::Select) {
      resolveChoice(cue.timeoutTargetNodeId);
    } else {
      resolvedChoices.push_back(static_cast<std::size_t>(choiceCue));
      GetChoiceQuestionRuntime(engine).hide();
      choiceCue = -1;
    }
  };
  expireChoice();
  while (next < script.cues.size() &&
         timelineReached(position, script.cues[next].at)) {
    if (script.cues[next].type == CueType::Choice &&
        (waitingForChoice || branch))
      break;
    const auto index = next++;
    const auto &cue = script.cues[index];
    if (cue.type == CueType::Choice) {
      if (std::find(resolvedChoices.begin(), resolvedChoices.end(), index) ==
          resolvedChoices.end())
        showChoice(index);
      expireChoice();
      continue;
    }
    if (timelineReached(position, cue.at + cue.duration))
      continue;
    if (cue.type == CueType::Video) {
      video.open(engine.GetAssets(), cue.value, false, cue.channelName);
      videoCue = static_cast<int>(index);
    } else if (cue.type == CueType::Audio) {
      const auto name = cue.channelName.empty() ? "audio" : cue.channelName;
      auto player = std::make_unique<MediaPlayer>(engine.GetAudio());
      player->open(engine.GetAssets(), cue.value, true, name);
      audio.insert_or_assign(name, AudioTrack{std::move(player), index});
    } else if (cue.type == CueType::Dialogue) {
      auto &box = GetVNTextBoxRuntime(engine, "", "", 30);
      box.setDialogue(cue.speaker, cue.value);
      box.completeDialogue();
      box.show();
      dialogueCue = static_cast<int>(index);
    }
  }
  if (!waitingForChoice && !branch &&
      timelineReached(position, script.duration)) {
    position = script.duration;
    auto completedChoices = std::move(resolvedChoices);
    stop();
    resolvedChoices = std::move(completedChoices);
    return;
  }
  if (videoCue >= 0)
    video.update(engine.GetRenderer(),
                 std::max(0.0, position - script.cues[videoCue].at));
  for (auto &entry : audio)
    entry.second.player->update(
        engine.GetRenderer(),
        std::max(0.0, position - script.cues[entry.second.cue].at));
  synchronizeMediaPause();
}

void TimelinePlayer::update(double delta) {
  if (!std::isfinite(delta) || delta < 0)
    throw std::invalid_argument(
        "Timeline delta must be finite and non-negative");
  if (!running || isPaused())
    return;
  const double target = std::min(script.duration, position + delta);
  // Process every question deadline, even when a frame jumps over the whole
  // cue.
  for (std::size_t i = 0; i < script.cues.size(); ++i) {
    const auto &cue = script.cues[i];
    const double deadline = cue.at + cue.duration;
    if (cue.type != CueType::Choice || !timelineReached(target, deadline) ||
        std::find(resolvedChoices.begin(), resolvedChoices.end(), i) !=
            resolvedChoices.end())
      continue;
    position = deadline;
    applyPosition();
    if (!running || isPaused())
      return;
  }
  position = target;
  applyPosition();
}

void TimelinePlayer::renderVideo(RendererRef renderer) {
  video.render(renderer, &engine.GetUpscaler());
}

void TimelinePlayer::renderSubtitles(RendererRef renderer) {
  if (!subtitles || !running)
    return;
  subtitles->setPosition(position);
  subtitles->render(renderer, 1);
}

std::string TimelinePlayer::activeSubtitleText() const {
  std::string text;
  if (!running)
    return text;
  for (const auto &cue : script.cues) {
    if (cue.type == CueType::Subtitle && timelineReached(position, cue.at) &&
        !timelineReached(position, cue.at + cue.duration)) {
      if (!text.empty())
        text += "\n";
      text += cue.value;
    }
  }
  return text;
}

} // namespace Kotonoha
