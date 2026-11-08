#pragma once
#include <Amagami/Campaign.hpp>
#include <Amagami/Source.hpp>
#include <Amagami/Vm.hpp>
#include <Kotonoha/core/TransformTween.hpp>
#include <functional>
#include <list>
#include <set>

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
  void startCampaign();
  bool campaignActive() const { return campaignActive_; }
  int campaignDate() const;
  int campaignPeriod() const;
  std::vector<Campaign::Event> availableEvents() const;
  std::vector<Campaign::Event> campaignMapEvents() const;
  void selectCampaignEvent(const std::string &name);
  void nextCampaignPeriod();
  void advance();
  void setRealtime(bool enabled) { realtime_ = enabled; }
  void tick(double seconds);
  void revealDialogue(size_t characters);
  double presentationSeconds() const { return presentationFrames_ / 60; }
  bool waiting() const { return waitFrames_ > 0; }
  // Verdadeiro quando a VM esta pausada (por sleep ou por shouldYield).
  // O runner deve chamar tick() para avançar o tempo e o input sintetico.
  bool vmPaused() const { return vm_.paused(); }
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
  int ambienceTrack() const { return ambienceTrack_; }
  int ambienceGain() const { return ambienceGain_; }
  const std::string &name() const { return scenario_; }
  std::string location() const { return scenario_ + " | " + vm_.location(); }
  std::function<void(const std::string &, int)> sound;
  const std::vector<Dialogue> &history() const { return history_; }
  int messageSpeed() const;
  bool autoMode() const;
  bool skipMode() const;
  bool skipping() const;
  void setSkipHeld(bool held);
  int autoWait() const;
  bool autoSync() const;
  void setAutoMode(bool enabled);
  void setSkipMode(bool enabled);
  void setMessageSpeed(int speed);
  // Returns true when every campaign EventNode reachable from the current
  // calendar position has been marked passed; used by checkClear to decide
  // whether the campaign has reached its end.
  bool campaignCleared() const;

private:
  std::shared_ptr<Source> source_;
  Vm vm_;
  VmValue self_;
  std::unique_ptr<Campaign> campaign_;
  bool campaignActive_ = false;
  struct CampaignAction {
    std::string kind, value;
  };
  std::vector<CampaignAction> campaignActions_;
  void completeCampaignEvent();
  int block_ = 0;
  int musicTrack_ = -1, musicGain_ = 100;
  int ambienceTrack_ = -1, ambienceGain_ = 80;
  std::set<std::string> seenLines_;
  bool ended_ = true, replaying_ = false;
  bool realtime_ = false, messageVisible_ = false, messageCleared_ = true,
       fadeWhite_ = false;
  // Input sintetico para modo headless: alterna pressionar/soltar o botao
  // confirm (256) a cada tick para que loops de ControlPad.trig/rapid/held
  // avancem em vez de girar para sempre no orcamento de instrucoes SCF.
  int syntheticInputTick_ = 0;
  // Contador de loops de sleep consecutivos no mesmo PC: usado para
  // detectar quando um script esta esperando por uma condicao externa (ex.:
  // flag de encontro/campanha) que nao existe no modo standalone. Quando
  // excede o limite, permite que o script continue em vez de travar para
  // sempre.
  double presentationFrames_ = 0;
  // Contador de loops de sleep consecutivos no mesmo bloco: quando excede o
  // limite, define campos numéricos do self como 1 para permitir que loops de
  // espera de flag externa (ex.: flag de encontro/campanha ausente no modo
  // standalone) saiam.
  int sleepLoopCount_ = 0;
  int sleepLoopBlock_ = 0;
  double waitFrames_ = 0, fadeFrames_ = 0, fadeElapsed_ = 0;
  float fadeAlpha_ = 0, fadeFrom_ = 0, fadeTarget_ = 0;
  void fade(float target, double frames, bool white = false);
  struct Tween {
    std::string visual;
    enum Property { Position, Scale, Angle, Alpha } property;
    Kotonoha::TransformAnimation animation;
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
  // Original Number.rnd sequence: an LCG seeded per session, so scenes that
  // randomise their staging (mahjong deals, steam timers) stay reproducible.
  long long rngSeed_ = 12345;
  std::optional<VmValue> native(const VmValue &, const std::string &,
                                const std::vector<VmValue> &);
  // Registers a scenario class and its scenario-class ancestors so methods
  // inherited across scenario classes (e.g. EncountReady from n0_MacroScripts
  // via a meeting class) resolve in standalone event entry.
  void loadScenarioClass(const std::string &name);
  std::optional<VmValue> primitive(const std::string &, const std::string &,
                                   bool, const VmValue &, unsigned,
                                   const std::vector<VmValue> &);
  // The Kotonoha presentation layer dispatches on the original variable names
  // (TUK, MIY, BG, ...); keep the object -> variable-name tag so scripts can
  // run their original class methods while natives stay unchanged.
  std::map<std::shared_ptr<VmObject>, std::string> placeholderTags_;
  std::string actorTag(const VmValue &r) const;
  void sleepFrames(double frames);
  void initializeGameState();
  void pump();
};
std::string DisplayText(const std::string &text);
} // namespace Amagami
