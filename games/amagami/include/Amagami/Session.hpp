#pragma once
#include <Amagami/Source.hpp>
#include <Amagami/Vm.hpp>
#include <functional>

namespace Amagami {
struct Visual {
  std::string archive, file, face;
  int cloth = 0, pose = 0, expression = 0;
  bool visible = false;
  float x = 0, y = 0, scaleX = 1, scaleY = 1, alpha = 1;
};
struct Dialogue {
  std::string speaker, text, original, voiceCode;
  int voice = -1;
};
struct Answer {
  std::string text;
  int value;
};
class Session {
public:
  explicit Session(std::shared_ptr<Source> source);
  void start(const std::string &name = "n0dea_ev01A");
  void advance();
  void setRealtime(bool enabled) { realtime_ = enabled; }
  void tick(double seconds);
  bool waiting() const { return waitFrames_ > 0; }
  bool messageVisible() const { return messageVisible_ && !waiting(); }
  float stageFade() const { return fadeAlpha_; }
  bool whiteFade() const { return fadeWhite_; }
  void choose(size_t index);
  void save(const std::string &path) const;
  void load(const std::string &path);
  const Dialogue &dialogue() const { return dialogue_; }
  const std::vector<Answer> &choices() const { return choices_; }
  const std::map<std::string, Visual> &visuals() const { return visuals_; }
  bool ended() const { return ended_; }
  int musicTrack() const { return musicTrack_; }
  int musicGain() const { return musicGain_; }
  const std::string &name() const { return scenario_; }
  std::string location() const { return scenario_ + " | " + vm_.location(); }
  std::function<void(const std::string &, int)> sound;
  const std::vector<Dialogue> &history() const { return history_; }

private:
  std::shared_ptr<Source> source_;
  Vm vm_;
  VmValue self_;
  int block_ = 0;
  int musicTrack_ = -1, musicGain_ = 100;
  bool ended_ = true, replaying_ = false;
  bool realtime_ = false, messageVisible_ = false, fadeWhite_ = false;
  double waitFrames_ = 0, fadeFrames_ = 0, fadeElapsed_ = 0;
  float fadeAlpha_ = 0, fadeFrom_ = 0, fadeTarget_ = 0;
  void fade(float target, double frames, bool white = false);
  std::string scenario_;
  Dialogue dialogue_;
  std::vector<Dialogue> history_;
  std::vector<Answer> choices_;
  std::map<std::string, Visual> visuals_;
  std::map<std::string, std::string> displayNames_;
  std::vector<int> inputs_;
  std::optional<VmValue> native(const VmValue &, const std::string &,
                                const std::vector<VmValue> &);
  void pump();
};
std::string DisplayText(const std::string &text);
} // namespace Amagami
