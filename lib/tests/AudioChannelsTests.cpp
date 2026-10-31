#include <Kotonoha/core/Engine.hpp>
#include <Kotonoha/core/FileSystem.hpp>
#include <Kotonoha/debug/DebugOverlay.hpp>
#include <Kotonoha/media/AudioMixer.hpp>
#include <Kotonoha/media/MediaPlayer.hpp>
#include <Kotonoha/script/TimelinePlayer.hpp>

#include <imgui.h>
#include <imgui_internal.h>

#include <cmath>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <limits>
#include <sstream>
#include <stdexcept>

namespace {
using namespace Kotonoha;

void require(bool value, const char *message) {
  if (!value)
    throw std::runtime_error(std::string(message) + ": " + SDL_GetError());
}

template <typename F> void rejects(F operation, const char *message) {
  bool rejected = false;
  try {
    operation();
  } catch (const std::exception &) {
    rejected = true;
  }
  require(rejected, message);
}

struct TemporaryScript {
  std::filesystem::path path =
      std::filesystem::temp_directory_path() /
      ("kotonoha-audio-" + std::to_string(SDL_GetPerformanceCounter()) +
       ".timeline");
  ~TemporaryScript() {
    std::error_code error;
    std::filesystem::remove(path, error);
  }
};

void testChannels(AudioMixer &mixer) {
  for (const auto &name : {std::string{}, std::string(" "), std::string("x\n"),
                           std::string(129, 'a'), std::string("x\0y", 3)})
    rejects([&] { mixer.channel(name); }, "Reject invalid channel name");
  auto music = mixer.channel("music");
  require(mixer.channel("music") == music, "Names identify shared channels");
  require(mixer.channel("Music") != music, "Channel names are case-sensitive");
  music->setVolume(0.35f);
  music->setMuted(true);
  require(music->getVolume() == 0.35f && music->isMuted(),
          "Mute preserves chosen volume");
  music->setVolume(0.6f);
  music->setMuted(false);
  require(music->getVolume() == 0.6f,
          "Changing volume while muted is preserved");
  for (float invalid : {-0.1f, 1.1f, std::numeric_limits<float>::quiet_NaN(),
                        std::numeric_limits<float>::infinity()})
    rejects([&] { music->setVolume(invalid); },
            "Reject nonfinite/out-of-range volume");
  require(music->getVolume() == 0.6f, "Invalid volume preserves old settings");
}

void testParser() {
  TemporaryScript file;
  writeTextFileAtomically(
      file.path.string(),
      "duration 3\n"
      "at 0 audio 2 \"music.ogg\" channel \"background music\"\n"
      "at 0 audio 2 \"voice.ogg\" channel \"voice\"\n"
      "at 0 video 2 \"video.mp4\" channel \"video effects\" # comment\n"
      "at 2 audio 1 \"next.ogg\"\n");
  const auto script = TimelineScript::load(file.path.string());
  require(script.cues.size() == 4 &&
              script.cues[0].channelName == "background music" &&
              script.cues[1].channelName == "voice" &&
              script.cues[2].channelName == "video effects" &&
              script.cues[3].channelName.empty(),
          "Parse optional quoted channel names and preserve legacy syntax");
  for (const char *invalid :
       {"at 0 audio 2 \"x\" channel \"\"", "at 0 audio 2 \"x\" channel voice",
        "at 0 audio 2 \"x\" channel \"voice\" garbage",
        "at 0 subtitle 2 \"x\" channel \"voice\"",
        "at 0 audio 2 \"x\"\nat 0 audio 2 \"y\" channel \"audio\"",
        "at 0 audio 2 \"x\" channel \"voice\"\nat 0 audio 2 \"y\" channel "
        "\"voice\""}) {
    writeTextFileAtomically(file.path.string(),
                            std::string("duration 3\n") + invalid);
    rejects([&] { TimelineScript::load(file.path.string()); },
            "Reject invalid channel syntax and overlapping same-channel cues");
  }
}

void testDebug(Engine &engine) {
  auto channel = engine.GetAudio().channel("audio");
  channel->setVolume(0.5f);
  auto &debug = engine.GetDebug();
  debug.setVisible(true);
  auto frame = [&] { debug.renderMenu(engine.GetRenderer()); };
  frame();
  auto *window = ImGui::FindWindowByName("Kotonoha Debug [F10]");
  require(window != nullptr, "Find debug inspector");
  window->StateStorage.SetInt(window->GetID("Audio channels"), 1);
  frame();
  frame();
  const auto channelID = ImHashStr("audio", 0, window->ID);
  const auto muteID = ImHashStr("Mute", 0, channelID);
  const auto volumeID = ImHashStr("Volume", 0, channelID);
  ImGui::ActivateItemByID(muteID);
  frame();
  require(channel->isMuted(), "Debug UI mute checkbox controls named channel");
  ImGui::ActivateItemByID(muteID);
  frame();
  require(!channel->isMuted() && channel->getVolume() == 0.5f,
          "Debug UI unmute restores previous volume");
  ImGui::ActivateItemByID(volumeID);
  frame();
  auto &io = ImGui::GetIO();
  io.AddKeyEvent(ImGuiKey_RightArrow, true);
  frame();
  io.AddKeyEvent(ImGuiKey_RightArrow, false);
  frame();
  require(channel->getVolume() > 0.5f && channel->getVolume() <= 1,
          "Debug UI slider changes named channel volume");
  debug.setVisible(false);
}

void near(float actual, float expected, const char *message) {
  require(std::abs(actual - expected) < 0.0001f, message);
}

void testStreamGain(AudioMixer &mixer) {
  const SDL_AudioSpec spec{SDL_AUDIO_S16, 2, 44100};
  SDL_AudioStream *first = SDL_CreateAudioStream(&spec, &spec);
  SDL_AudioStream *second = SDL_CreateAudioStream(&spec, &spec);
  require(first && second, "Create PCM gain test streams");
  auto channel = mixer.channel("fade-test");
  channel->attach(first);
  channel->attach(second);
  channel->setStreamGain(first, 0.5f);
  channel->setVolume(0.6f);
  near(SDL_GetAudioStreamGain(first), 0.3f, "Per-stream gain composes with channel volume");
  near(SDL_GetAudioStreamGain(second), 0.6f, "Other streams keep their own gain");
  channel->setMuted(true);
  channel->setStreamGain(first, 0.25f);
  near(SDL_GetAudioStreamGain(first), 0, "Mute overrides per-stream gain");
  channel->setMuted(false);
  near(SDL_GetAudioStreamGain(first), 0.15f, "Unmute restores the per-stream gain");
  rejects([&] { channel->setStreamGain(first, -1); }, "Reject invalid per-stream gain");
  mixer.setMasterVolume(0.4f);
  near(SDL_GetAudioStreamGain(first), 0.06f, "Master composes with channel and stream gain");
  near(SDL_GetAudioStreamGain(second), 0.24f, "Master affects every stream");
  mixer.setMasterMuted(true);
  near(SDL_GetAudioStreamGain(first), 0, "Master mute affects live streams");
  channel->detach(second);
  auto future = mixer.channel("created-while-master-muted");
  future->setVolume(0.8f);
  future->attach(second);
  near(SDL_GetAudioStreamGain(second), 0, "Future channels inherit master mute");
  mixer.setMasterVolume(0.5f);
  near(SDL_GetAudioStreamGain(second), 0, "Volume does not cancel master mute");
  mixer.setMasterMuted(false);
  near(SDL_GetAudioStreamGain(second), 0.4f, "Unmute restores master and channel volume");
  channel->setMuted(true);
  mixer.setMasterVolume(1);
  near(SDL_GetAudioStreamGain(first), 0, "Master edits preserve independent channel mute");
  channel->setMuted(false);
  for (float invalid : {-0.1f, 1.1f, std::numeric_limits<float>::quiet_NaN()})
    rejects([&] { mixer.setMasterVolume(invalid); }, "Reject invalid master volume");
  near(SDL_GetAudioStreamGain(first), 0.15f, "Rejected master edits preserve gains");
  future->detach(second);
  channel->detach(first);
  channel->detach(second);
  SDL_DestroyAudioStream(first);
  SDL_DestroyAudioStream(second);
}

void testMedia(Engine &engine, const std::string &path) {
  auto &mixer = engine.GetAudio();
  auto music = mixer.channel("music");
  auto voice = mixer.channel("voice");
  music->setVolume(0.25f);
  music->setMuted(true);
  voice->setVolume(0.8f);
  MediaPlayer first(mixer), second(mixer), other(mixer);
  first.open(path, true, "music");
  second.open(path, false, "music");
  other.open(path, true, "voice");
  require(music->activeStreams() == 2 && voice->activeStreams() == 1,
          "Independent players share a named channel, including embedded video "
          "audio");
  near(first.audioGain(), 0, "New stream inherits existing mute");
  music->setMuted(false);
  near(first.audioGain(), 0.25f, "Unmute restores stream gain");
  near(second.audioGain(), 0.25f, "Unmute affects all matching streams");
  music->setVolume(0.45f);
  near(first.audioGain(), 0.45f,
       "Volume applies immediately to live audio stream");
  near(second.audioGain(), 0.45f, "Volume applies to embedded video audio");
  near(other.audioGain(), 0.8f, "Other channel is unaffected");
  first.setPaused(true);
  music->setMuted(true);
  near(first.audioGain(), 0, "Muting paused stream updates gain");
  music->setVolume(0.6f);
  near(first.audioGain(), 0, "Volume changes do not unmute stream");
  music->setMuted(false);
  near(first.audioGain(), 0.6f, "Muted volume edit applies on unmute");
  first.setPaused(false);
  first.setChannel("voice");
  require(music->activeStreams() == 1 && voice->activeStreams() == 2,
          "Reassigning channel transfers stream ownership");
  near(first.audioGain(), 0.8f, "Reassignment adopts destination volume");
  voice->setMuted(true);
  near(first.audioGain(), 0, "Reassigned stream follows new channel");
  near(other.audioGain(), 0, "Existing destination stream follows mute");
  near(second.audioGain(), 0.6f,
       "Old channel no longer affects reassigned stream");
  first.stop();
  require(voice->activeStreams() == 1, "Stopping stream detaches from channel");
  rejects([&] { first.audioGain(); }, "Stopped stream has no gain");
  first.open(path, true, "voice");
  near(first.audioGain(), 0,
       "Reopened stream inherits retained channel settings");
  rejects([&] { first.open(path + ".missing", true, "voice"); },
          "Opening missing media reports failure");
  require(voice->activeStreams() == 2, "Failed open preserves previous stream");
  first.stop();
  second.stop();
  other.stop();
  require(music->activeStreams() == 0 && voice->activeStreams() == 0,
          "All stream registrations are released");
}

void testTimeline(Engine &engine, const std::string &path) {
  auto &mixer = engine.GetAudio();
  TemporaryScript file;
  std::ostringstream text;
  text << "duration 3\nat 0 audio 1.5 " << std::quoted(path)
       << " channel \"music\"\nat 0 audio 2.5 " << std::quoted(path)
       << " channel \"voice\"\nat 0 video 1 " << std::quoted(path)
       << " channel \"video effects\"\n";
  writeTextFileAtomically(file.path.string(), text.str());
  auto script = TimelineScript::load(file.path.string());
  auto &timeline = engine.GetTimeline();
  timeline.play(script);
  require(mixer.channel("music")->activeStreams() == 1 &&
              mixer.channel("voice")->activeStreams() == 1 &&
              mixer.channel("video effects")->activeStreams() == 1,
          "Timeline plays simultaneous named audio tracks and video audio");
  timeline.setPaused(true);
  timeline.update(1);
  require(timeline.seconds() == 0, "Pausing timeline pauses every track");
  timeline.setPaused(false);
  timeline.update(2);
  require(mixer.channel("music")->activeStreams() == 0 &&
              mixer.channel("voice")->activeStreams() == 1 &&
              mixer.channel("video effects")->activeStreams() == 0,
          "Each named track expires independently");
  timeline.play(script, 0.2);
  require(mixer.channel("music")->activeStreams() == 1 &&
              mixer.channel("voice")->activeStreams() == 1,
          "Timeline restore reopens concurrent tracks without registrations "
          "leaking");
  timeline.stop();
  require(mixer.channel("music")->activeStreams() == 0 &&
              mixer.channel("voice")->activeStreams() == 0 &&
              mixer.channel("video effects")->activeStreams() == 0,
          "Stopping timeline detaches all channels");
}
} // namespace

int main(int argc, char **argv) {
  try {
    Runtime runtime("Audio channels tests", "1", "org.kotonoha.audio.tests");
    Engine engine(runtime);
    testChannels(engine.GetAudio());
    testStreamGain(engine.GetAudio());
    testParser();
    testDebug(engine);
    if (argc > 1) {
      testMedia(engine, argv[1]);
      testTimeline(engine, argv[1]);
    }
    std::cout
        << "Named audio channels, debug controls and timeline tests passed\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << "FAIL: " << error.what() << '\n';
    return 1;
  }
}
