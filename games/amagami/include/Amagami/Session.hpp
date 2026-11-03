#pragma once
#include <Amagami/Source.hpp>
#include <Amagami/Vm.hpp>
#include <functional>
#include <list>

namespace Amagami {
struct Vibration {
  float fromX = 0, fromY = 0, targetX = 0, targetY = 0;
  float periodX = 0, periodY = 0;
  double elapsed = 0, transition = 0;
};
struct Visual {
  std::string archive, file, face;
  int cloth = 0, pose = 0, expression = 0, mouthExpression = 0;
  int eye = 0, mouth = 2;
  bool blink = false, manualMouth = false;
  double breathAge = -1;
  float breathSize = 10, breathDensity = 0.666666f, breathTime = 8;
  int breathDirection = 0;
  Vibration positionVibration, scaleVibration;
  float motionX = 0, motionY = 0, stretchX = 0, stretchY = 0;
  float nodX = 0, nodY = 0;
  double blinkFrame = 0, nextBlink = 240;
  bool visible = false;
  float x = 0, y = 0, scaleX = 1, scaleY = 1, alpha = 1, angle = 0;
};
struct ActorTransition {
  Visual previous;
  double remaining = 0, frames = 0;
};
struct ExpressionCue {
  size_t character = 0;
  std::string actor;
  int eyes = -1, mouth = -1, eyeFrame = -1, mouthFrame = -1;
  int cloth = -1, pose = -1;
  double fadeFrames = 30;
};
struct Dialogue {
  std::string speaker, text, original, voiceCode;
  int voice = -1;
  std::vector<ExpressionCue> cues;
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
  void revealDialogue(size_t characters);
  double presentationSeconds() const { return presentationFrames_ / 60; }
  bool waiting() const { return waitFrames_ > 0; }
  bool messageVisible() const { return messageVisible_; }
  bool messageCleared() const { return messageCleared_; }
  float stageFade() const { return fadeAlpha_; }
  bool whiteFade() const { return fadeWhite_; }
  void choose(size_t index);
  void save(const std::string &path) const;
  void load(const std::string &path);
  const Dialogue &dialogue() const { return dialogue_; }
  const std::vector<Answer> &choices() const { return choices_; }
  const std::map<std::string, Visual> &visuals() const { return visuals_; }
  const std::map<std::string, ActorTransition> &actorTransitions() const {
    return actorTransitions_;
  }
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
  bool realtime_ = false, messageVisible_ = false, messageCleared_ = true,
       fadeWhite_ = false;
  double presentationFrames_ = 0;
  double waitFrames_ = 0, fadeFrames_ = 0, fadeElapsed_ = 0;
  float fadeAlpha_ = 0, fadeFrom_ = 0, fadeTarget_ = 0;
  void fade(float target, double frames, bool white = false);
  struct Tween {
    std::string visual;
    enum Property { Position, Scale, Angle, Alpha } property;
    float fromX, fromY, toX, toY;
    double elapsed = 0, frames = 0;
    int curve = 0;
  };
  std::vector<Tween> tweens_;
  struct Fiber {
    std::shared_ptr<VmObject> handle;
    Vm vm;
    double wait = 0;
    bool killed = false;
  };
  std::list<Fiber> fibers_;
  Fiber *activeFiber_ = nullptr;
  Vm &executingVm() { return activeFiber_ ? activeFiber_->vm : vm_; }
  void animate(const std::string &, Tween::Property, float, float, double, int);
  void updateTweens(double frames);
  struct Nod {
    std::string actor;
    float x, y;
    double frames, elapsed = 0;
  };
  std::vector<Nod> nods_;
  std::string scenario_;
  Dialogue dialogue_;
  size_t revealedCues_ = 0;
  std::vector<Dialogue> history_;
  std::vector<Answer> choices_;
  std::map<std::string, Visual> visuals_;
  std::map<std::string, ActorTransition> actorTransitions_;
  float actorFadeFrames_ = 30;
  int breathAmount_ = 0;
  void transitionActor(const std::string &, const Visual &, double);
  void putBreath(const std::string &, float = 0, float = 0, float = 0);
  std::map<std::string, std::string> displayNames_;
  std::vector<int> inputs_;
  std::optional<VmValue> native(const VmValue &, const std::string &,
                                const std::vector<VmValue> &);
  void pump();
};
std::string DisplayText(const std::string &text);
} // namespace Amagami
