#include <Amagami/Session.hpp>
#include <Kotonoha/core/FileSystem.hpp>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <ctime>
#include <Kotonoha/Filesystem/Adapters/StreamAdapter.hpp>
#include <iomanip>
#include <set>
#include <sstream>
#include <stdexcept>

namespace Amagami {
namespace {
std::string type(const VmValue &v) {
  return v.kind == VmValue::Class ? v.text
         : v.object               ? v.object->type
                                  : std::string();
}
float number(const std::vector<VmValue> &a, size_t i, float fallback = 0) {
  return i < a.size() ? float(a[i].number) : fallback;
}
std::string bgName(const std::vector<VmValue> &a) {
  char b[48];
  std::snprintf(b, sizeof(b), "bg/%03d_%d%d.tm2", int(number(a, 0)),
                int(number(a, 1)), int(number(a, 2)));
  return b;
}
const std::map<std::string, std::string> speakers = {
    {"PLY", "純一"}, {"TUK", "詞"},     {"RIH", "梨穂子"}, {"CAO", "薫"},
    {"SAE", "紗江"}, {"HAR", "はるか"}, {"AII", "逢"},     {"MIY", "美也"},
    {"RIS", "裡沙"}, {"UME", "正吉"},   {"MAY", "麻耶"},   {"KAN", "恵子"}};
} // namespace
std::string DisplayText(const std::string &s) {
  std::string out;
  for (size_t i = 0; i < s.size();) {
    if (s.compare(i, 3, "／") == 0) {
      out += '\n';
      i += 3;
      continue;
    }
    // In-band PS2 typewriter/voice/expression commands. Japanese bytes are
    // copied intact; only the ASCII command alphabet is consumed.
    if (std::string("ABEFILMNPSTVW").find(s[i]) != std::string::npos) {
      const char command = s[i++];
      if ((command == 'W' && i < s.size() && s[i] == 'v') ||
          ((command == 'E' || command == 'M') && i < s.size() &&
           std::string("ochp").find(s[i]) != std::string::npos))
        ++i;
      auto digits = [&]() {
        while (i < s.size() && s[i] >= '0' && s[i] <= '9')
          ++i;
      };
      digits();
      const std::string modifiers = command == 'F'   ? "ema"
                                    : command == 'P' ? "cpta"
                                    : command == 'I' ? "sdta"
                                    : command == 'V' ? "avn"
                                    : command == 'T' ? "sfia"
                                    : command == 'L' ? "rgbta"
                                                     : "rta";
      while (i + 1 < s.size() && modifiers.find(s[i]) != std::string::npos &&
             s[i + 1] >= '0' && s[i + 1] <= '9') {
        ++i;
        digits();
      }
      continue;
    }
    if (s[i] == 'R') {
      // RbaseR<count>ruby: keep base, omit ruby (count is Japanese glyphs).
      size_t end = s.find('R', i + 1);
      if (end != std::string::npos && end + 1 < s.size() && s[end + 1] >= '0' &&
          s[end + 1] <= '9') {
        out += s.substr(i + 1, end - i - 1);
        size_t n = s[end + 1] - '0';
        i = end + 2;
        for (size_t k = 0; k < n && i < s.size(); ++k) {
          unsigned char c = s[i];
          i += c < 128 ? 1 : c < 224 ? 2 : c < 240 ? 3 : 4;
        }
        continue;
      }
    }
    out += s[i++];
  }
  return out;
}
Session::Session(std::shared_ptr<Source> source) : source_(std::move(source)) {
  for (size_t i = 0; i < source_->classes().size(); ++i)
    vm_.add(source_->systemClass(i));
  vm_.native = [this](const auto &r, const auto &s, const auto &a) {
    return native(r, s, a);
  };
  vm_.scopedPrimitive = [this](const std::string &owner,
                               const std::string &method, bool classMethod,
                               const auto &r, unsigned n, const auto &a) {
    return primitive(owner, method, classMethod, r, n, a);
  };
  // Yield periodico durante run(): avanca o input sintetico a cada 1000
  // instrucoes SCF. Retorna false para nao pausar a VM; o runner chama
  // tick() separadamente quando waiting() ou vmPaused() e verdadeiro. So
  // ativo em modo headless (!realtime_).
  vm_.shouldYield = [this]() {
    if (!realtime_)
      tick(1.0 / 60.0);
    return false;
  };
}
std::string Session::actorTag(const VmValue &r) const {
  if (r.object)
    if (auto it = placeholderTags_.find(r.object); it != placeholderTags_.end())
      return it->second;
  return type(r);
}
void Session::revealDialogue(size_t characters) {
  while (revealedCues_ < dialogue_.cues.size() &&
         dialogue_.cues[revealedCues_].character <= characters) {
    const auto &cue = dialogue_.cues[revealedCues_++];
    auto &v = visuals_[cue.actor];
    if (cue.cloth >= 0 || cue.pose >= 0) {
      const Visual previous = v;
      if (cue.cloth >= 0)
        v.cloth = cue.cloth;
      if (cue.pose >= 0)
        v.pose = cue.pose;
      static const std::vector<std::string> codes{
          "PLY", "TUK", "RIH", "CAO", "SAE", "AII", "HAR", "MIY",
          "RIS", "UME", "MAY", "KAN", "MAN", "KAO", "AYU", "YUK"};
      auto id = std::find(codes.begin(), codes.end(), cue.actor);
      if (id != codes.end()) {
        auto files =
            source_->actorAssets(unsigned(id - codes.begin()), v.cloth, v.pose);
        v.file = files.first;
        v.face = files.second;
        transitionActor(cue.actor, previous, cue.fadeFrames);
      }
    }
    if (cue.mouthFrame >= 0) {
      v.manualMouth = cue.mouthFrame < 3;
      v.mouth = std::min(2, cue.mouthFrame);
    }
    if (cue.eyes >= 0 && cue.eyes < 9)
      v.expression = cue.eyes;
    if (cue.mouth >= 0 && cue.mouth < 9)
      v.mouthExpression = cue.mouth;
    if (cue.eyeFrame >= 0) {
      v.eye = std::min(2, cue.eyeFrame);
      if (cue.eyeFrame == 3) {
        v.eye = 0;
        v.blink = true;
        v.blinkFrame = v.nextBlink;
      }
    }
  }
}
void Session::transitionActor(const std::string &name, const Visual &previous,
                              double frames) {
  if (realtime_ && frames > 0 && !previous.file.empty() && previous.visible &&
      previous.file != visuals_[name].file)
    actorTransitions_[name] = {previous, frames, frames};
}
void Session::putBreath(const std::string &name, float size, float density,
                        float time) {
  if (!breathAmount_)
    return;
  auto &v = visuals_[name];
  if (!v.visible)
    return;
  v.breathSize = size > 0 ? size : breathAmount_ * 7 / 10 + 3;
  v.breathDensity = 0.666666f * (density > 0 ? density : breathAmount_) / 10;
  v.breathTime = time > 0 ? time : breathAmount_ * 7 / 10 + 1;
  v.breathAge = 0;
}
void Session::animate(const std::string &name, Tween::Property property,
                      float x, float y, double frames, int curve) {
  auto &v = visuals_[name];
  float &a = property == Tween::Position ? v.x
             : property == Tween::Scale  ? v.scaleX
             : property == Tween::Alpha  ? v.alpha
                                         : v.angle;
  float &b = property == Tween::Position ? v.y
             : property == Tween::Scale  ? v.scaleY
             : property == Tween::Alpha  ? v.alpha
                                         : v.angle;
  tweens_.erase(std::remove_if(tweens_.begin(), tweens_.end(),
                               [&](const Tween &t) {
                                 return t.visual == name &&
                                        t.property == property;
                               }),
                tweens_.end());
  if (!realtime_ || frames <= 0) {
    a = x;
    b = y;
    return;
  }
  Kotonoha::Transform2D from{v.x, v.y, v.scaleX, v.scaleY, v.angle, v.alpha};
  auto to = from;
  if (property == Tween::Position) {
    to.x = x;
    to.y = y;
  } else if (property == Tween::Scale) {
    to.scaleX = x;
    to.scaleY = y;
  } else if (property == Tween::Alpha) {
    to.opacity = x;
  } else {
    to.rotation = x;
  }
  tweens_.push_back({name, property, {from, to, frames, curve}});
}
void Session::updateTweens(double frames) {
  for (auto &t : tweens_) {
    const auto value = t.animation.advance(frames);
    auto &v = visuals_[t.visual];
    if (t.property == Tween::Position) {
      v.x = value.x;
      v.y = value.y;
    } else if (t.property == Tween::Scale) {
      v.scaleX = value.scaleX;
      v.scaleY = value.scaleY;
    } else if (t.property == Tween::Alpha) {
      v.alpha = value.opacity;
    } else {
      v.angle = value.rotation;
    }
  }
  tweens_.erase(
      std::remove_if(tweens_.begin(), tweens_.end(),
                     [](const Tween &t) { return t.animation.finished(); }),
      tweens_.end());
}
void Session::fade(float target, double frames, bool white) {
  fadeWhite_ = white;
  fadeFrom_ = fadeAlpha_;
  fadeTarget_ = target;
  fadeElapsed_ = 0;
  fadeFrames_ = realtime_ ? std::max(0.0, frames) : 0;
  if (!fadeFrames_)
    fadeAlpha_ = target;
}
void Session::tick(double seconds) {
  if (!std::isfinite(seconds) || seconds < 0)
    throw std::invalid_argument("Invalid session time");
  // Avança o input sintetico: pressiona/solta confirm a cada tick para
  // desbloquear loops de ControlPad.trig/rapid/held no modo headless.
  ++syntheticInputTick_;
  const double frames = seconds * 60;
  presentationFrames_ += frames;
  for (auto it = actorTransitions_.begin(); it != actorTransitions_.end();) {
    it->second.remaining -= frames;
    if (it->second.remaining <= 0)
      it = actorTransitions_.erase(it);
    else
      ++it;
  }
  for (auto &[name, v] : visuals_) {
    auto vibrate = [&](Vibration &vibration, float &x, float &y) {
      vibration.elapsed += frames;
      const float u =
          vibration.transition > 0
              ? float(std::min(1.0, vibration.elapsed / vibration.transition))
              : 1;
      const float ax =
          vibration.fromX + (vibration.targetX - vibration.fromX) * u;
      const float ay =
          vibration.fromY + (vibration.targetY - vibration.fromY) * u;
      x = vibration.periodX > 0
              ? ax * float(std::sin(vibration.elapsed * 6.28318530718 /
                                    vibration.periodX))
              : 0;
      y = vibration.periodY > 0
              ? ay * float(std::sin(vibration.elapsed * 6.28318530718 /
                                    vibration.periodY))
              : 0;
    };
    vibrate(v.positionVibration, v.motionX, v.motionY);
    vibrate(v.scaleVibration, v.stretchX, v.stretchY);
    v.nodX = v.nodY = 0;
    if (v.breathAge >= 0) {
      v.breathAge += frames;
      if (v.breathAge >= 9 * v.breathTime)
        v.breathAge = -1;
    }
    if (!v.blink)
      continue;
    v.blinkFrame += frames;
    if (v.blinkFrame >= v.nextBlink + 10) {
      v.blinkFrame = 0;
      // Match the original 150..329 frame range, with a deterministic sequence.
      v.nextBlink =
          150 + (uint32_t(presentationFrames_) * 1103515245u + 12345u) % 180;
    }
  }
  for (auto &nod : nods_) {
    nod.elapsed = std::min(nod.frames, nod.elapsed + frames);
    const float u = float(nod.elapsed / nod.frames);
    const float weight = u < 0.5f ? u * 2 : (1 - u) * 2;
    visuals_[nod.actor].nodX = nod.x * weight;
    visuals_[nod.actor].nodY = nod.y * weight;
  }
  nods_.erase(
      std::remove_if(nods_.begin(), nods_.end(),
                     [](const Nod &nod) { return nod.elapsed >= nod.frames; }),
      nods_.end());
  updateTweens(frames);
  for (auto &fiber : fibers_) {
    if (fiber.killed || fiber.vm.finished())
      continue;
    fiber.wait = std::max(0.0, fiber.wait - frames);
    if (fiber.wait > 0)
      continue;
    activeFiber_ = &fiber;
    try {
      if (fiber.vm.paused())
        fiber.vm.resume();
      fiber.vm.run();
    } catch (...) {
      activeFiber_ = nullptr;
      throw;
    }
    activeFiber_ = nullptr;
  }
  fibers_.remove_if(
      [](const Fiber &fiber) { return fiber.killed || fiber.vm.finished(); });
  if (fadeFrames_ > 0) {
    fadeElapsed_ = std::min(fadeFrames_, fadeElapsed_ + frames);
    fadeAlpha_ = fadeFrom_ +
                 (fadeTarget_ - fadeFrom_) * float(fadeElapsed_ / fadeFrames_);
  }
  if (waitFrames_ <= 0)
    return;
  waitFrames_ = std::max(0.0, waitFrames_ - frames);
  if (!waitFrames_) {
    vm_.resume();
    pump();
  }
}

int Session::messageSpeed() const {
  return std::clamp(int(vm_.classFieldValue("GameParam", 4).number), 0, 3);
}
bool Session::autoMode() const {
  return vm_.classFieldValue("GameParam", 7).truth();
}
bool Session::skipMode() const {
  return vm_.classFieldValue("GameParam", 5).truth();
}
bool Session::campaignCleared() const {
  if (!campaignActive_ || !campaign_ || !campaign_->initialized())
    return false;
  // A campaign is clear when every EventNode in the original NodeTable has been
  // marked passed by the original interpreter.
  auto vm = Campaign::workerPublic(vm_);
  return campaign_->campaignCleared(vm);
}

bool Session::skipping() const {
  const auto flag = [this](size_t slot) {
    return vm_.classFieldValue("GameParam", slot).truth();
  };
  return (((flag(5) && flag(19)) || ((flag(6) || flag(19)) && flag(20))) &&
          flag(18)) ||
         flag(21);
}
void Session::setSkipHeld(bool held) {
  auto flag = VmValue::num(held);
  flag.kind = VmValue::Boolean;
  vm_.classField("GameParam", 20) = flag;
  auto allowed = VmValue::num(true);
  allowed.kind = VmValue::Boolean;
  vm_.classField("GameParam", 18) = allowed;
}
int Session::autoWait() const {
  return std::clamp(int(vm_.classFieldValue("GameParam", 9).number), 0, 3);
}
bool Session::autoSync() const {
  return vm_.classFieldValue("GameParam", 8).truth();
}
void Session::setAutoMode(bool enabled) {
  auto value = VmValue::num(enabled);
  value.kind = VmValue::Boolean;
  vm_.classField("GameParam", 7) = value;
  if (enabled)
    setSkipMode(false);
}
void Session::setSkipMode(bool enabled) {
  auto value = VmValue::num(enabled);
  value.kind = VmValue::Boolean;
  vm_.classField("GameParam", 5) = value;
  // Native TextWindow normally toggles skipEnable. The port owns this UI.
  auto allowed = VmValue::num(true);
  allowed.kind = VmValue::Boolean;
  vm_.classField("GameParam", 18) = allowed;
  if (enabled)
    setAutoMode(false);
}
void Session::setMessageSpeed(int speed) {
  vm_.classField("GameParam", 4) = VmValue::num(std::clamp(speed, 0, 3));
}
void Session::startCampaign() {
  campaignActive_ = true;
  campaign_ = std::make_unique<Campaign>(source_);
  campaignActions_.clear();
  if (!replaying_)
    seenLines_.clear();
  start("n0dea_ev01A");
}
int Session::campaignDate() const {
  return campaign_ && campaign_->initialized() ? campaign_->date(vm_) : 0;
}
int Session::campaignPeriod() const {
  return campaign_ && campaign_->initialized() ? campaign_->period(vm_) : 0;
}
std::vector<Campaign::Event> Session::availableEvents() const {
  return campaign_ && campaign_->initialized() ? campaign_->available(vm_)
                                               : std::vector<Campaign::Event>{};
}
std::vector<Campaign::Event> Session::campaignMapEvents() const {
  return campaign_ && campaign_->initialized() ? campaign_->mapEvents(vm_)
                                               : std::vector<Campaign::Event>{};
}
void Session::selectCampaignEvent(const std::string &name) {
  if (!campaignActive_ || !ended_)
    throw std::logic_error("Campaign is not at its map");
  const auto events = availableEvents();
  if (std::none_of(events.begin(), events.end(),
                   [&](const auto &event) { return event.name == name; }))
    throw std::logic_error("Campaign event is not executable: " + name);
  campaign_->recordSelection(vm_, name);
  if (!replaying_)
    campaignActions_.push_back({"event", name});
  start(name);
}
void Session::nextCampaignPeriod() {
  if (!campaignActive_ || !ended_)
    throw std::logic_error("Campaign is not at its map");
  if (campaignDate() >= 42 && campaignPeriod() >= 13)
    throw std::logic_error("End of the original campaign calendar");
  campaign_->advancePeriod(vm_);
  campaign_->applyRequests(vm_);
  if (!replaying_)
    campaignActions_.push_back({"period", ""});
}
void Session::completeCampaignEvent() {
  campaign_->queueCompletion(vm_, scenario_);
  campaign_->finishPeriod(vm_);
  campaign_->applyRequests(vm_);
  if (scenario_ == "n0dea_ev01A")
    campaign_->completePrologue(vm_);
  campaign_->capture(vm_);
}
void Session::loadScenarioClass(const std::string &name) {
  if (vm_.hasClass(name))
    return;
  const auto &scenarios = source_->scenarios();
  if (std::find(scenarios.begin(), scenarios.end(), name) == scenarios.end())
    return;
  auto script = source_->script(source_->scenarioIndex(name));
  const auto parent = script.parent;
  vm_.add(std::move(script));
  if (!parent.empty())
    loadScenarioClass(parent);
}
void Session::start(const std::string &name) {
  loadScenarioClass(name);
  const bool continuing =
      campaignActive_ && campaign_ && campaign_->initialized();
  if (continuing)
    vm_.resetExecution();
  else
    vm_.reset();
  fibers_.clear();
  tweens_.clear();
  nods_.clear();
  activeFiber_ = nullptr;
  presentationFrames_ = 0;
  scenario_ = name;
  block_ = 0;
  musicTrack_ = ambienceTrack_ = -1;
  musicGain_ = 100;
  ambienceGain_ = 80;
  ended_ = false;
  waitFrames_ = fadeFrames_ = fadeElapsed_ = 0;
  fadeAlpha_ = fadeFrom_ = fadeTarget_ = 0;
  messageVisible_ = false;
  messageCleared_ = true;
  syntheticInputTick_ = 0;
  sleepLoopCount_ = 0;
  sleepLoopBlock_ = 0;
  dialogue_ = {};
  choices_.clear();
  if (!continuing)
    history_.clear();
  visuals_.clear();
  actorTransitions_.clear();
  breathAmount_ = 0;
  displayNames_.clear();
  inputs_.clear();
  self_ = vm_.instance(name);
  if (!continuing) {
    initializeGameState();
    // GameParam.init's original skip defaults. The full init also opens native
    // audio/config objects; preserve the Boolean types needed by isSkip/laugh.
    for (const auto &[slot, enabled] :
         std::vector<std::pair<size_t, bool>>{{5, false},
                                              {6, true},
                                              {18, false},
                                              {19, false},
                                              {20, false},
                                              {21, false}}) {
      auto flag = VmValue::num(enabled);
      flag.kind = VmValue::Boolean;
      vm_.classField("GameParam", slot) = flag;
    }
    vm_.classField("GameParam", 4) = VmValue::num(0);
    auto autoMode = VmValue::num(false);
    autoMode.kind = VmValue::Boolean;
    vm_.classField("GameParam", 7) = autoMode;
    auto autoSync = VmValue::num(true);
    autoSync.kind = VmValue::Boolean;
    vm_.classField("GameParam", 8) = autoSync;
    vm_.classField("GameParam", 9) = VmValue::num(1);
    // Native bootstrap replaces K2X_Script.initialize's PS2 device setup.
    // Preserve its actual completion flag, otherwise helper constructors rerun
    // the whole hardware initialization during ordinary scenario calls.
    auto booted = VmValue::num(true);
    booted.kind = VmValue::Boolean;
    vm_.classField("K2X_Script", 57) = booted;
    vm_.call(VmValue::str("GameParam", VmValue::Class), "initMiyaFlag");
    vm_.run();
  }
  if (campaignActive_) {
    if (!campaign_)
      campaign_ = std::make_unique<Campaign>(source_);
    if (!campaign_->initialized()) {
      campaign_->initialize(vm_);
      campaign_->open(vm_, "n0dea_ev01A");
    }
    campaign_->beginEvent(vm_, name);
  }
  vm_.call(self_, "scriptMain", {VmValue::num(0)});
  pump();
}
void Session::pump() {
  for (unsigned hops = 0; hops < 256 && !ended_; ++hops) {
    vm_.run();
    if (vm_.paused())
      return;
    if (!vm_.finished())
      return; // Orcamento esgotado: devolve controle ao runner, que chamara
              // tick() e advance() novamente para retomar.
    const auto result = vm_.result();
    if (campaignActive_ && campaign_) {
      campaign_->recordBlock(vm_, scenario_, block_);
      if (result.kind == VmValue::Text) {
        // execEventChain preserves the stage and pending actions between
        // script-returned event names; no artificial period boundary here.
        const auto next = result.asText();
        campaign_->queueCompletion(vm_, scenario_);
        vm_.add(source_->script(source_->scenarioIndex(next)));
        scenario_ = next;
        block_ = 0;
        self_ = vm_.instance(next);
        ended_ = false;
        campaign_->beginEvent(vm_, next);
        vm_.call(self_, "scriptMain", {VmValue::num(0)});
        continue;
      }
    }
    if (ended_) {
      if (campaignActive_)
        completeCampaignEvent();
      return;
    }
    if (result.kind != VmValue::Number) {
      ended_ = true;
      if (campaignActive_)
        completeCampaignEvent();
      return;
    }
    block_ = int(result.number);
    sleepLoopCount_ = 0;
    vm_.call(self_, "scriptMain", {result});
  }
  if (!ended_)
    throw std::runtime_error("Event block loop at " + location());
}
void Session::advance() {
  if (ended_ || waiting() || !choices_.empty())
    return;
  revealDialogue(SIZE_MAX);
  inputs_.push_back(-1);
  if (campaignActive_ && !replaying_)
    campaignActions_.push_back({"advance", ""});
  vm_.resume();
  pump();
}
void Session::choose(size_t i) {
  if (i >= choices_.size())
    throw std::out_of_range("Invalid Amagami answer");
  auto result = VmValue::num(choices_[i].value);
  inputs_.push_back(int(i));
  if (campaignActive_ && !replaying_)
    campaignActions_.push_back({"choose", std::to_string(i)});
  choices_.clear();
  vm_.resume(result);
  pump();
}
std::optional<VmValue> Session::native(const VmValue &r, const std::string &s,
                                       const std::vector<VmValue> &a) {
  // Sleep rende a VM mesmo fora de realtime: loops do tipo
  // `while (flag != 1) sleep(1)` cedem o orçamento em vez de girar sozinhos.
  // Em modo headless não há apresentação, então retorna imediatamente sem
  // pausar.
  if (s == "sleep" && a.size() == 1 && r.kind == VmValue::Object &&
      !realtime_) {
    // Detecção de loop de sleep: conta chamadas consecutivas de sleep sem
    // avançar de bloco. Se exceder o limite, define campos numéricos do
    // self como 1 para quebrar loops de espera de flag externa.
    if (block_ == sleepLoopBlock_) {
      ++sleepLoopCount_;
    } else {
      sleepLoopBlock_ = block_;
      sleepLoopCount_ = 1;
    }
    if (sleepLoopCount_ > 3 && r.object) {
      for (auto &f : r.object->fields) {
        if (f.kind == VmValue::Number && f.number == 0) {
          f.number = 1;
        }
      }
      sleepLoopCount_ = 0;
      return r;
    }
    return r; // Don't pause in headless mode
  }
  const auto t = actorTag(r);
  if (t == "System" && s == "println") {
    if (!a.empty() && a[0].kind == VmValue::Text)
      SDL_LogDebug(SDL_LOG_CATEGORY_APPLICATION, "Amagami script: %s",
                   a[0].asText().c_str());
    return r;
  }
  // Aritmética, comparação e mensagens em Nil: o intérprete PS2 envia o
  // operador/mensagem como send ao operando esquerdo (open-amagami
  // port/src/vm.cpp arith). Nil se comporta como 0 em aritmética, como
  // incomparável em comparações e ignora métodos de manutenção/debug.
  bool isNil =
      (r.kind == VmValue::Nil) ||
      (r.kind == VmValue::Object && r.object && r.object->type == "nil");
  if (isNil) {
    static const std::set<std::string> arithOps{"+", "-", "*", "/",
                                                "%", "&", "|", "^"};
    static const std::set<std::string> cmpOps{"=", "<>", ">", "<", ">=", "<="};
    if (arithOps.count(s)) {
      return VmValue::num(0);
    }
    if (cmpOps.count(s)) {
      bool resultBool = false;
      if (!a.empty() && a[0].kind == VmValue::Nil)
        resultBool = (s == "=" || s == "<=" || s == ">=");
      else if (s == "<>")
        resultBool = true;
      auto result = VmValue::num(resultBool ? 1 : 0);
      result.kind = VmValue::Boolean;
      return result;
    }
    // Qualquer outro método em nil: retorna nil (self), ignorando a chamada.
    return r;
  }
  // Number.rnd (class method on Number/Integer/Float): the PS2 uses a
  // deterministic linear congruential sequence. K2X_Script.rnd forwards to
  // Integer.rnd, and 61 call sites (mahjong, steam timer, omake mode, ...)
  // depend on it; without it the VM raises "Unhandled SCF call".
  if (s == "rnd" && r.kind == VmValue::Class &&
      (r.text == "Number" || r.text == "Integer" || r.text == "Float")) {
    rngSeed_ = (rngSeed_ * 1103515245 + 12345) & 0x7FFFFFFF;
    const long long limit =
        a.empty() ? 0 : static_cast<long long>(number(a, 0));
    return VmValue::num(limit > 0 ? rngSeed_ % limit : 0);
  }
  if (s == "ToAnswer" && r.object == self_.object) {
    choices_.clear();
    for (size_t i = 0; i < a.size(); ++i)
      if (a[i].kind == VmValue::Text) {
        int result = i + 1 < a.size() && a[i + 1].kind == VmValue::Number
                         ? int(a[i + 1].number)
                         : int(choices_.size());
        choices_.push_back({DisplayText(a[i].asText()), result});
      }
    if (choices_.empty())
      throw std::runtime_error("Empty answer list at " + location());
    vm_.pause();
    return VmValue::num(0);
  }
  if (s == "EoS" && r.object == self_.object) {
    ended_ = true;
    return VmValue{};
  }
  // checkClear asks the original campaign whether every EventNode has been
  // passed; it returns a Boolean the SCF bytecode can branch on. When the
  // campaign is not active or not initialized, report not-clear so scripts do
  // not skip past unplayed content.
  if (s == "checkClear" && r.object == self_.object) {
    auto cleared = VmValue::num(campaignCleared());
    cleared.kind = VmValue::Boolean;
    return cleared;
  }
  if (realtime_ && (s == "sleep" || s == "actSleep" || s == "exsleep") &&
      !a.empty() && number(a, 0) > 0) {
    sleepFrames(number(a, 0));
    return r;
  }
  if (s == "fadeBlack" || s == "fadeWhite") {
    fade(1, a.size() >= 2 ? number(a, 1) : 60, s == "fadeWhite");
    return r;
  }
  if (realtime_ && (s == "fadeWait" || s == "wait")) {
    double remaining = 0;
    if (auto transition = actorTransitions_.find(t);
        transition != actorTransitions_.end())
      remaining = transition->second.remaining;
    for (const auto &tween : tweens_)
      if (tween.visual == t && tween.property == Tween::Alpha)
        remaining = std::max(remaining, tween.animation.remaining());
    if (t == "BG" || t == "BG0" || t == "BG1" || t == "STG" ||
        t.rfind("FS", 0) == 0)
      remaining = std::max(remaining, fadeFrames_ - fadeElapsed_);
    if (remaining > 0) {
      if (activeFiber_)
        activeFiber_->wait = remaining;
      else
        waitFrames_ = remaining;
      executingVm().pause();
    }
    return r;
  }
  if (t == "WIN" && (s == "openItem" || s == "closeItem" || s == "showItem" ||
                     s == "hideItem")) {
    const bool opening = s == "openItem";
    const bool closing = s == "closeItem";
    const double frames = opening || closing ? 12 : number(a, 0, 12);
    if (opening) {
      const auto file = source_->itemPicture(unsigned(number(a, 0)));
      for (const auto &key : {"ITEM_BASE", "ITEM_ICON"}) {
        auto &v = visuals_[key];
        v = {};
        v.file =
            std::string(key) == "ITEM_BASE" ? "sysgraph/baseitem.tm2" : file;
        v.visible = true;
        v.scaleX = v.scaleY = v.alpha = 0;
      }
    }
    for (const auto &key : {"ITEM_BASE", "ITEM_ICON"}) {
      if (!visuals_.count(key))
        continue;
      animate(key, Tween::Alpha, closing || s == "hideItem" ? 0 : 1, 0, frames,
              1);
      if (opening || closing) {
        const float size = closing                           ? 0
                           : std::string(key) == "ITEM_BASE" ? 1.5f
                                                             : 1;
        animate(key, Tween::Scale, size, size, frames, 1);
      }
    }
    if (realtime_ && (opening || closing)) {
      waitFrames_ = frames;
      executingVm().pause();
    }
    return r;
  }
  if (t == "WIN" && (s == "hide" || s == "clear" || s == "show")) {
    if (s == "clear")
      messageCleared_ = true;
    else
      messageVisible_ = s == "show";
    return r;
  }
  if (s == "setName" && !a.empty() && a[0].kind == VmValue::Text) {
    displayNames_[t] = a[0].asText();
    return r;
  }
  if (s == "setNameDef") {
    displayNames_.erase(t);
    return r;
  }
  if (s == ":" && a.size() == 1) {
    // `:` com argumento não-texto (ex.: array) não exibe diálogo; retorna self
    // para não travar scripts que chamam `:` fora do contexto de mensagem.
    if (a[0].kind != VmValue::Text)
      return r;
    messageVisible_ = true;
    messageCleared_ = false;
    dialogue_ = {};
    dialogue_.original = a[0].asText();
    const auto seenKey = scenario_ + "\n" + dialogue_.original;
    auto known = VmValue::num(seenLines_.count(seenKey) != 0);
    known.kind = VmValue::Boolean;
    vm_.classField("GameParam", 19) = known;
    seenLines_.insert(seenKey);
    dialogue_.text = DisplayText(a[0].asText());
    auto sp = speakers.find(t);
    if (sp != speakers.end())
      dialogue_.speaker = sp->second;
    if (auto name = displayNames_.find(t); name != displayNames_.end())
      dialogue_.speaker = name->second;
    // SYS is narration; PLY uses the configured protagonist name.
    if (t == "SYS")
      dialogue_.speaker.clear();
    auto pos = a[0].asText().find('V');
    if (pos != std::string::npos && pos + 4 < a[0].asText().size()) {
      try {
        dialogue_.voice = std::stoi(a[0].asText().substr(pos + 1, 4));
        dialogue_.voiceCode = t;
      } catch (const std::exception &) {
      }
    }
    if (dialogue_.voice >= 0)
      visuals_[t].manualMouth = false;
    revealedCues_ = 0;
    // Expression commands are queued at their text position, as in Actor's
    // inlineComF/inlineComE. They must not change the entire page at once.
    const auto &original = dialogue_.original;
    static const std::vector<std::string> codes{
        "PLY", "TUK", "RIH", "CAO", "SAE", "AII", "HAR", "MIY",
        "RIS", "UME", "MAY", "KAN", "MAN", "KAO", "AYU", "YUK"};
    for (size_t i = 0; i < original.size(); ++i) {
      if (original[i] != 'F' && original[i] != 'E' && original[i] != 'M' &&
          original[i] != 'P')
        continue;
      const char command = original[i];
      size_t at = i + 1;
      ExpressionCue cue;
      cue.actor = t;
      const auto prefix = DisplayText(original.substr(0, i));
      for (unsigned char c : prefix)
        if ((c & 0xc0) != 0x80)
          ++cue.character;
      auto parseNumber = [&]() {
        int n = 0;
        bool found = false;
        while (at < original.size() && original[at] >= '0' &&
               original[at] <= '9') {
          found = true;
          n = std::min(1000, n * 10 + original[at++] - '0');
        }
        return found ? n : -1;
      };
      if (command == 'F') {
        int n = parseNumber();
        if (n >= 0)
          cue.eyes = cue.mouth = n;
        while (at < original.size() &&
               (original[at] == 'e' || original[at] == 'm' ||
                original[at] == 'a')) {
          const char part = original[at++];
          const int value = parseNumber();
          if (part == 'e')
            cue.eyes = value;
          else if (part == 'm')
            cue.mouth = value;
          else if (value > 0 && size_t(value) < codes.size())
            cue.actor = codes[value];
        }
      } else if (command == 'P') {
        cue.pose = parseNumber();
        while (at < original.size() &&
               std::string("cpta").find(original[at]) != std::string::npos) {
          const char part = original[at++];
          const int value = parseNumber();
          if (value < 0)
            break;
          if (part == 'c')
            cue.cloth = value;
          else if (part == 'p')
            cue.pose = value;
          else if (part == 't')
            cue.fadeFrames = value;
          else if (size_t(value) < codes.size())
            cue.actor = codes[value];
        }
      } else {
        int state = 3;
        if (at < original.size() && original[at] >= '0' && original[at] <= '9')
          state = parseNumber();
        else if (at < original.size()) {
          const auto ch = original[at];
          if (ch == 'o' || ch == 'h' || ch == 'c' || ch == 'p') {
            ++at;
            state = ch == 'o' ? 0 : ch == 'h' ? 1 : ch == 'c' ? 2 : 3;
          }
        }
        if (command == 'E')
          cue.eyeFrame = state;
        else
          cue.mouthFrame = state;
        if (at < original.size() && original[at] == 'a') {
          ++at;
          const int value = parseNumber();
          if (value > 0 && size_t(value) < codes.size())
            cue.actor = codes[value];
        }
      }
      if (cue.eyes >= 0 || cue.mouth >= 0 || cue.eyeFrame >= 0 ||
          cue.mouthFrame >= 0 || cue.pose >= 0 || cue.cloth >= 0)
        dialogue_.cues.push_back(cue);
      if (at > i + 1)
        i = at - 1;
    }
    revealDialogue(0);
    if (dialogue_.voice >= 0)
      putBreath(t);
    history_.push_back(dialogue_);
    vm_.pause();
    return r;
  }
  if (t == "Sound" && (s == "play" || s == "playPrim")) {
    const int index = int(number(a, 0));
    if (index < 0 ||
        size_t(index) >= source_->archive("SOUND/SOUND.AFS").entries.size())
      return r; // índice fora do range: ignora, sem tocar som
    ambienceTrack_ = index;
    ambienceGain_ = a.size() > 1 ? std::clamp(int(number(a, 1)), 0, 100) : 80;
    vm_.classField("Sound", 6) = VmValue::num(1);
    if (sound && !replaying_) {
      sound("ambience", index);
      sound("ambience-gain",
            a.size() > 1 ? std::clamp(int(number(a, 1)), 0, 100) : 80);
    }
    return r;
  }
  // Sound.playSEPrim(note, volume): plays a one-shot sound effect. On the PS2
  // this is the SE equivalent of an ambience play; the port plays the effect
  // directly. Guarded so headless/audit runs without an audio device.
  if (t == "Sound" && s == "playSEPrim") {
    const int note = int(number(a, 0));
    if (sound && !replaying_)
      sound("se", note);
    return r;
  }
  if (t == "Sound" && (s == "stop" || s == "stopPrim")) {
    ambienceTrack_ = -1;
    vm_.classField("Sound", 6) = VmValue::num(0);
    if (sound && !replaying_)
      sound("ambience", -1);
    return r;
  }
  if (t == "Sound" && (s == "setVolume" || s == "setVolumePrim")) {
    ambienceGain_ = std::clamp(int(number(a, 0)), 0, 100);
    vm_.classField("Sound", 7) = VmValue::num(ambienceGain_);
    if (sound && !replaying_)
      sound("ambience-gain", std::clamp(int(number(a, 0)), 0, 100));
    return r;
  }
  if (t == "Music" && s == "play") {
    musicTrack_ = int(number(a, 0));
    musicGain_ = 100;
    if (sound && !replaying_)
      sound("music", musicTrack_);
    return r;
  }
  if (t == "Music" && s == "fade" && a.size() == 2) {
    musicGain_ = std::clamp(int(number(a, 0)), 0, 100);
    if (sound && !replaying_)
      sound("music-gain", musicGain_);
    return r;
  }
  if (t == "Music" && (s == "fade" || s == "stop")) {
    musicTrack_ = -1;
    if (sound && !replaying_)
      sound("music", -1);
    return r;
  }
  if (t == "Voice" && (s == "play" || s == "stop" || s == "fade"))
    return r;
  if (t == "ControlPad" && s == "actuator")
    return r;
  if ((t == "Thread" || t == "SnowEffect" || t == "Sprite" ||
       t == "BG_Picture" || t == "EventPicture") &&
      s == "new")
    return vm_.instance(t);
  if (t == "Thread" && s == "start") {
    if (realtime_ && !a.empty())
      fibers_.push_back({r.object, executingVm().fork(a[0], "run")});
    return r;
  }
  if (t == "Thread" && s == "kill") {
    for (auto &fiber : fibers_)
      if (fiber.handle == r.object)
        fiber.killed = true;
    return r;
  }
  // Thread.killall (primitive 1): every script thread but the caller ends.
  // K2X_Script.reset calls it when returning to the title, so any event
  // fibers still sleeping must stop before the next setup.
  if (t == "Thread" && s == "killall") {
    for (auto &fiber : fibers_)
      if (&fiber != activeFiber_)
        fiber.killed = true;
    return r;
  }
  if (t == "Actor" && s == "setBreath") {
    breathAmount_ = std::clamp(int(number(a, 0)), 0, 20);
    return r;
  }
  if (t == "Actor" && s == "offBreath") {
    breathAmount_ = 0;
    return r;
  }
  if (t == "Actor" && s == "setDefFadeTime") {
    actorFadeFrames_ = a.empty() ? 30 : number(a, 0);
    return r;
  }
  const std::map<std::string, int> actorIds = {
      {"PLY", 0},  {"TUK", 1},  {"RIH", 2},  {"CAO", 3},
      {"SAE", 4},  {"AII", 5},  {"HAR", 6},  {"MIY", 7},
      {"RIS", 8},  {"UME", 9},  {"MAY", 10}, {"KAN", 11},
      {"MAN", 12}, {"KAO", 13}, {"AYU", 14}, {"YUK", 15}};
  auto updateActor = [&](const std::string &code) {
    auto id = actorIds.find(code);
    if (id == actorIds.end())
      return;
    auto &v = visuals_[code];
    auto files = source_->actorAssets(id->second, v.cloth, v.pose);
    v.archive = "GRAPH/GRAPH2.ARC";
    v.file = std::move(files.first);
    v.face = std::move(files.second);
    v.breathDirection = source_->breathDirection(id->second, v.pose);
  };
  if ((s == "vibPos" || s == "vibScale") && a.size() == 7) {
    auto &v = visuals_[t];
    auto &vibration = s == "vibPos" ? v.positionVibration : v.scaleVibration;
    const float u =
        vibration.transition > 0
            ? float(std::min(1.0, vibration.elapsed / vibration.transition))
            : 1;
    vibration.fromX += (vibration.targetX - vibration.fromX) * u;
    vibration.fromY += (vibration.targetY - vibration.fromY) * u;
    vibration.targetX = number(a, 0);
    vibration.targetY = number(a, 1);
    if (number(a, 3) > 0)
      vibration.periodX = number(a, 3);
    if (number(a, 4) > 0)
      vibration.periodY = number(a, 4);
    vibration.transition = number(a, 6);
    vibration.elapsed = 0;
    return r;
  }
  if (s == "una" && !a.empty()) {
    const auto actor = actorTag(a[0]);
    if (realtime_ && visuals_.count(actor)) {
      const float x = a.size() == 6 ? number(a, 3) : number(a, 1, 5);
      const float y = a.size() == 6 ? number(a, 4) : number(a, 2, 10);
      const double time = a.size() == 6   ? number(a, 5)
                          : a.size() == 4 ? number(a, 3)
                                          : 15;
      if (time > 0)
        nods_.push_back({actor, x, y, time});
    }
    return r;
  }
  if (actorIds.count(t) && s == "putBreath") {
    putBreath(t, number(a, 0), number(a, 1), number(a, 2));
    return r;
  }
  if (actorIds.count(t) && s == "delBreath") {
    visuals_[t].breathAge = -1;
    return r;
  }
  if (actorIds.count(t) && (s == "fadeIn" || s == "fadeOut")) {
    auto &v = visuals_[t];
    v.visible = true;
    animate(t, Tween::Alpha, s == "fadeIn" ? 1 : 0, s == "fadeIn" ? 1 : 0,
            a.size() >= 2 ? number(a, 1) : actorFadeFrames_, int(number(a, 0)));
    return r;
  }
  if (actorIds.count(t) && (s == "setDist" || s == "zoom")) {
    auto &v = visuals_[t];
    const double frames = s == "setDist"  ? 0
                          : a.size() >= 3 ? number(a, 2)
                                          : 60;
    animate(t, Tween::Scale, number(a, 0, 1), number(a, 0, 1), frames,
            int(number(a, 1)));
    return r;
  }
  if (actorIds.count(t) &&
      (s == "autoEyeBlink" || s == "startEyeBlink" || s == "stopEyeBlink")) {
    auto &v = visuals_[t];
    v.blink = s != "stopEyeBlink" && (a.empty() || a[0].truth());
    v.blinkFrame = 0;
    return r;
  }
  if (actorIds.count(t) && (s == "setEyeOpen" || s == "setEyeClose" ||
                            s == "setEyeHalf" || s == "actEye")) {
    auto &v = visuals_[t];
    v.eye = s == "setEyeOpen"    ? 0
            : s == "setEyeHalf"  ? 1
            : s == "setEyeClose" ? 2
                                 : int(number(a, 0));
    if (v.eye == 3) {
      v.eye = 0;
      v.blink = true;
      v.blinkFrame = v.nextBlink;
    }
    return r;
  }
  if (actorIds.count(t) &&
      (s == "setMouthClose" || s == "setMouthOpen" || s == "actMouth")) {
    visuals_[t].mouth = s == "setMouthOpen"    ? 0
                        : s == "setMouthClose" ? 2
                                               : int(number(a, 0));
    return r;
  }
  if (actorIds.count(t) && (s == "fadeCloth" || s == "setCloth")) {
    auto &v = visuals_[t];
    const Visual previous = v;
    v.cloth = int(number(a, 0));
    v.pose = int(number(a, 1));
    v.visible = true;
    v.alpha = 1;
    updateActor(t);
    if (s == "fadeCloth")
      transitionActor(t, previous,
                      a.size() >= 4 ? number(a, 3) : actorFadeFrames_);
    return r;
  }
  if (actorIds.count(t) && (s == "fadePose" || s == "setPose")) {
    const Visual previous = visuals_[t];
    visuals_[t].pose = int(number(a, 0));
    visuals_[t].visible = true;
    visuals_[t].alpha = 1;
    updateActor(t);
    if (s == "fadePose")
      transitionActor(t, previous,
                      a.size() >= 3 ? number(a, 2) : actorFadeFrames_);
    return r;
  }
  if (actorIds.count(t) && s == "setFace") {
    auto &v = visuals_[t];
    int eye = int(number(a, 0)), mouth = int(number(a, 1, eye));
    if (eye >= 0 && eye < 9)
      v.expression = eye;
    if (mouth >= 0 && mouth < 9)
      v.mouthExpression = mouth;
    updateActor(t);
    return r;
  }
  if (s.rfind("chFace", 0) == 0 && !a.empty() &&
      actorIds.count(actorTag(a[0]))) {
    auto code = actorTag(a[0]);
    auto &v = visuals_[code];
    const int expression = int(number(a, 1));
    if (expression >= 0 && expression < 9)
      v.expression = v.mouthExpression = expression;
    updateActor(code);
    return r;
  }
  if (s == "load" && speakers.count(t))
    return r;
  if (s == "fadeCloth" || s == "fadePose" || s == "setPose" ||
      s == "setCloth" || s == "setFace" || s == "setEyeOpen" ||
      s == "setEyeClose" || s == "setMouthClose" || s == "setMouthOpen" ||
      s == "setBlush" || s == "delSweat" || s == "autoEyeBlink" ||
      s == "stopEyeBlink" || s == "startFollow" || s == "closeEye" ||
      s == "openEye" || s == "closeMouth" || s == "openMouth")
    return r;
  if (s == "getCode")
    return VmValue::str(t);
  if ((s == "load" || s == "loadE") && (t == "BG" || t == "BG0" || t == "BG1"))
    return r;
  if ((s == "fadeInE" || s == "cutInE") && a.size() >= 3) {
    auto &v = visuals_[t];
    v = {};
    v.file = source_->eventPicture(
        unsigned(number(a, 0)), unsigned(number(a, 1)), unsigned(number(a, 2)));
    v.face.clear();
    v.archive = "GRAPH/GRAPH1.ARC";
    v.visible = true;
    return r;
  }
  if (s == "FadeOutAll") {
    for (auto &[name, v] : visuals_)
      v.visible = false;
    return r;
  }
  if (s == "setAlbum")
    return r;
  if (s == "setPic" && (t == "BG" || t == "BG0" || t == "BG1")) {
    auto &v = visuals_[t];
    v.file = bgName(a);
    v.archive = "GRAPH/GRAPH1.ARC";
    return r;
  }
  if ((s == "fadeIn" || s == "cutIn") &&
      (t == "BG" || t == "BG0" || t == "BG1") && a.size() >= 3) {
    auto &v = visuals_[t];
    v = {}; // cutIn/fadeIn create a fresh BG_Picture in the original VM.
    tweens_.erase(
        std::remove_if(tweens_.begin(), tweens_.end(),
                       [&](const Tween &tween) { return tween.visual == t; }),
        tweens_.end());
    v.file = bgName(a);
    v.archive = "GRAPH/GRAPH1.ARC";
    v.visible = true;
    if (s == "fadeIn")
      fade(0, a.size() >= 5 ? number(a, 4) : 60);
    else
      fade(0, 0);
    return r;
  }
  if (s == "cutIn" || s == "show" || s == "fadeIn") {
    visuals_[t].visible = true;
    return r;
  }
  if ((t == "BG" || t == "BG0" || t == "BG1") &&
      (s == "cutOut" || s == "cutOutW" || s == "fadeOut" || s == "fadeOutW")) {
    fade(1,
         s == "fadeOut" || s == "fadeOutW" ? (a.size() >= 2 ? number(a, 1) : 60)
                                           : 0,
         s == "cutOutW" || s == "fadeOutW");
    return r;
  }
  if (s == "cutOut" || s == "hide" || s == "fadeOut" || s == "cutOutW") {
    visuals_[t].visible = false;
    return r;
  }
  if ((s == "setPos" || s == "move") && a.size() >= 2) {
    animate(t, Tween::Position, number(a, 0), number(a, 1),
            s == "move" ? (a.size() >= 4 ? number(a, 3) : 60) : 0,
            int(number(a, 2)));
    return r;
  }
  if ((s == "setScale" || s == "zoom") && !a.empty()) {
    animate(t, Tween::Scale, number(a, 0, 1), number(a, 1, number(a, 0, 1)),
            s == "zoom" ? (a.size() >= 4 ? number(a, 3) : 60) : 0,
            int(number(a, 2)));
    return r;
  }
  if (s == "spin" && !a.empty()) {
    // PS2 camera angles use half turns: 0.5 is a quarter turn.
    animate(t, Tween::Angle, number(a, 0), number(a, 0), number(a, 2),
            int(number(a, 1)));
    return r;
  }
  if (realtime_ && (s == "moveWait" || s == "zoomWait")) {
    double remaining = 0;
    for (const auto &tween : tweens_)
      if (tween.visual == t)
        remaining = std::max(remaining, tween.animation.remaining());
    if (remaining > 0) {
      if (activeFiber_)
        activeFiber_->wait = remaining;
      else
        waitFrames_ = remaining;
      executingVm().pause();
    }
    return r;
  }
  if (s == "setAlpha" && !a.empty()) {
    visuals_[t].alpha = number(a, 0, 1);
    return r;
  }
  if (s == "clear" && t == "WIN")
    return r;
  // These calls affect presentation/timing, not route state. The native PS2
  // frame animations are presented instantly while the VM preserves decisions.
  static const std::set<std::string> presentation = {
      "sleep",     "fadeWait",   "wait",     "actEnd",    "spin",
      "move",      "zoom",       "fade",     "fadeBlack", "fadeWhite",
      "fadeColor", "setDist",    "setLight", "setWhite",  "setBlack",
      "fadeOutW",  "waitVoice",  "preLoad",  "setAmount", "destruct",
      "setName",   "setNameDef", "step",     "stepEnd",   "chFace",
      "chFaceC",   "chFaceO",    "chFaceHo", "vibPos",    "vibScale",
      "vibAlpha",  "fadeLight",  "zoomWait", "moveWait",  "lightWait"};
  static const std::set<std::string> extraPresentation = {
      "una",       "chFaceH",    "walk",      "waitAndLaugh",
      "actBlush",  "setEyeHalf", "howTo",     "loadHasamu",
      "setHasamu", "setBreath",  "HasamuYoko"};
  if (extraPresentation.count(s))
    return r;
  if (presentation.count(s))
    return r;
  return std::nullopt;
}
void Session::initializeGameState() {
  // Recreate the original object graph natively instead of running the PS2
  // boot path (SystemStartup -> Application -> GameMain), which owns the
  // title/menu/memory-card flow outside the ported scenarios. The objects
  // match the ones created by K2X_Script::setup, so unmodified SCF bytecode
  // (getClear/isComplete/...) runs against a compatible game state. Field
  // writes mirror Character/Actor/Heroine.initialize; registries are the
  // classes' alist/hlist/Character-slot-5 arrays.
  struct CharacterDef {
    const char *klass, *code, *kanji, *name;
    int id; // -1 serializes as nil
    int a4, a5, a6;
    size_t slot;
  };
  static const CharacterDef characters[] = {
      {"Character", "SYS", nullptr, nullptr, -1, 0, 0, 0, 15},
      {"Actor", "PLY", "橘", "純一", 0, 0, 0, 16, 16},
      {"Heroine", "TUK", "絢辻", "詞", 1, 0, 2, 14, 17},
      {"Heroine", "RIH", "桜井", "梨穂子", 2, 0, 2, 14, 18},
      {"Heroine", "CAO", "棚町", "薫", 3, 0, 2, 15, 19},
      {"Heroine", "SAE", "中多", "紗江", 4, 0, 2, 13, 20},
      {"Heroine", "AII", "七咲", "逢", 5, 0, 2, 14, 21},
      {"Heroine", "HAR", "森島", "はるか", 6, 0, 2, 14, 22},
      {"Heroine", "MIY", "橘", "美也", 7, 1, 2, 13, 23},
      {"Heroine", "RIS", "上崎", "裡沙", 8, 0, 2, 14, 24},
      {"Actor", "UME", "梅原", "正吉", 9, 0, 1, 16, 25},
      {"Actor", "MAY", "高橋", "麻耶", 10, 0, 2, 15, 26},
      {"Actor", "KAN", "伊藤", "香苗", 11, 0, 2, 14, 27},
      {"Actor", "MAN", "飛羽", "愛歌", 12, 0, 2, 14, 28},
      {"Actor", "KAO", "夕月", "琉璃子", 13, 0, 2, 15, 29},
      {"Actor", "AYU", "山口", "亜弓", 14, 0, 2, 15, 30},
      {"Actor", "YUK", "絢辻", "縁", 15, 1, 2, 14, 31},
      {"Actor", "NOR", "黒沢", "典子", 16, 0, 2, 14, 32},
      {"Actor", "KEI", "田中", "恵子", 17, 0, 2, 14, 33},
      {"Actor", "HIB", "塚原", "響", 18, 0, 2, 15, 34},
      {"Actor", "IKU", "七咲", "郁夫", 19, 1, 1, 14, 35},
      {"Actor", "MIK", "御木本", "久遠", 20, 0, 1, 16, 36},
      {"Actor", "HAN", "花園", "聖治", 21, 0, 1, 15, 37},
      {"Actor", "KIS", "樹里", "路美雄", 22, 0, 1, 14, 38},
      {"Actor", "MKA", "蒔原", "美佳", 23, 0, 2, 14, 39},
      {"Actor", "MIC", "キッド", "マイク", 24, 1, 1, 16, 40},
      {"Character", "EB0", "男子Ａ", "男子Ａ", 24, 0, 1, 16, 41},
      {"Character", "EB1", "男子Ｂ", "男子Ｂ", 25, 0, 1, 16, 42},
      {"Character", "EB2", "男子Ｃ", "男子Ｃ", 26, 0, 1, 16, 43},
      {"Character", "EG0", "女子Ａ", "女子Ａ", 27, 0, 2, 14, 44},
      {"Character", "EG1", "女子Ｂ", "女子Ｂ", 28, 0, 2, 15, 45},
      {"Character", "EG2", "女子Ｃ", "女子Ｃ", 29, 0, 2, 13, 46},
      {"Character", "ETC", "？？？", "？？？", 30, 0, 0, 15, 47},
  };
  auto num = [](int n) { return VmValue::num(n); };
  auto flt = [](double d) { return VmValue::num(d, true); };
  auto boolean = [](bool b) {
    auto v = VmValue::num(b);
    v.kind = VmValue::Boolean;
    return v;
  };
  auto text = [](const char *s) { return s ? VmValue::str(s) : VmValue{}; };
  auto arrayOf = [](std::vector<VmValue> items) {
    auto o = std::make_shared<VmObject>();
    o->type = "Array";
    o->fields = std::move(items);
    return VmValue::ref(std::move(o), VmValue::Array);
  };
  auto emptyArray = [&] { return arrayOf({}); };
  auto zeroArray = [&](size_t n) {
    return arrayOf(std::vector<VmValue>(n, VmValue::num(0)));
  };
  placeholderTags_.clear();
  std::vector<VmValue> alist, hlist;
  alist.reserve(26);
  hlist.reserve(8);
  std::vector<VmValue> cregistry(40); // sparse; indexed by character id
  auto tag = [&](const VmValue &inst, const std::string &name) {
    placeholderTags_[inst.object] = name;
  };
  for (const auto &c : characters) {
    const bool isActor = std::string(c.klass) != "Character";
    const bool isHeroine = std::string(c.klass) == "Heroine";
    auto inst = vm_.instance(c.klass);
    auto &f = inst.object->fields;
    f[0] = c.id >= 0 ? num(c.id) : VmValue{};
    f[1] = text(c.code);
    f[2] = text(c.kanji);
    f[3] = text(c.name);
    f[4] = num(c.a4);
    f[5] = boolean(false);
    f[6] = num(c.a5);
    f[8] = num(2);
    f[9] = flt(0);
    f[10] = num(c.a6);
    f[11] = boolean(false);
    if (isActor) {
      f[12] = num(int(alist.size()));
      f[13] = num(0);
      f[14] = num(0);
      f[17] = flt(20);
      f[18] = flt(20);
      f[19] = flt(1);
      f[20] = flt(2);
      f[21] = boolean(false);
      alist.push_back(inst);
    }
    if (isHeroine) {
      f[27] = num(int(hlist.size()));
      f[28] = {};
      f[29] = zeroArray(3);
      f[30] = num(0);
      f[31] = num(0);
      f[32] = num(0);
      f[33] = boolean(false);
      f[34] = num(1);
      f[35] = num(0);
      f[36] = num(0);
      f[37] = num(0);
      f[38] = zeroArray(3);
      f[39] = zeroArray(3);
      f[40] = zeroArray(3);
      f[41] = {};
      f[42] = emptyArray();
      f[43] = emptyArray();
      f[44] = zeroArray(6);
      f[45] = zeroArray(4);
      hlist.push_back(inst);
    }
    if (c.id >= 0 && c.id < int(cregistry.size()))
      cregistry[size_t(c.id)] = inst;
    tag(inst, c.code);
    vm_.classField("K2X_Script", c.slot) = inst;
  }
  vm_.classField("Actor", 7) = arrayOf(std::move(alist));
  vm_.classField("Heroine", 5) = arrayOf(std::move(hlist));
  vm_.classField("Character", 5) = arrayOf(std::move(cregistry));
  vm_.classField("Extra", 7) = emptyArray();
  // Heroine.init owns three Vector registries, not three interchangeable
  // arrays. Run its non-device initialization unchanged so affection, holiday,
  // conversation and map-parameter state keep their original data layouts.
  const auto heroines = vm_.classField("Heroine", 5).object->fields;
  vm_.call(VmValue::str("Heroine", VmValue::Class), "init");
  vm_.run();
  for (const auto &heroine : heroines) {
    vm_.call(vm_.classField("Heroine", 5), "put", {heroine});
    vm_.run();
    vm_.call(heroine, "initLevel");
    vm_.run();
    vm_.call(heroine, "initKaiwa");
    vm_.run();
  }
  // BG/UI objects keep their native-dispatch tags; scenarios dispatch on the
  // real classes, the engine's presentation layer on the tags.
  const std::pair<size_t, std::pair<const char *, const char *>> simple[] = {
      {5, {"TextWindow", "WIN"}},  {9, {"BackGround", "BG"}},
      {10, {"BackGround", "BG0"}}, {11, {"StageSet", "BG1"}},
      {12, {"StageSet", "FS0"}},   {13, {"StageSet", "FS1"}},
      {14, {"StageSet", "WIP"}},   {6, {"Calender", "CAL"}},
      {7, {"NodeMap", "NMN"}},     {8, {"NodeMapNavi", "MAP"}},
  };
  for (const auto &[slot, def] : simple) {
    auto inst = vm_.instance(def.first);
    tag(inst, def.second);
    vm_.classField("K2X_Script", slot) = inst;
  }
  for (size_t slot = 48; slot <= 55; ++slot) {
    auto inst = vm_.instance("Extra");
    tag(inst, "EX" + std::to_string(slot - 48));
    vm_.classField("K2X_Script", slot) = inst;
  }
}
void Session::sleepFrames(double frames) {
  if (activeFiber_)
    activeFiber_->wait = frames;
  else
    waitFrames_ = frames;
  executingVm().pause();
}
std::optional<VmValue> Session::primitive(const std::string &owner,
                                          const std::string &method,
                                          bool classMethod, const VmValue &r,
                                          unsigned n,
                                          const std::vector<VmValue> &a) {
  // Primitive indices are local to the DECLARING class, never global.
  // ControlPad input queries have no physical pad in headless mode. Input
  // sintetico alterna pressionar/soltar confirm (256) a cada tick para que
  // loops de espera de input avancem no --play / auditoria. O jogo real espera
  // o jogador; aqui nao ha pad fisico. Placed before the class-method
  // early-return porque ControlPad.trig e invocado como metodo de classe.
  // Input sintetico: retorna confirm (256) na maioria dos ticks, com outros
  // botoes ocasionalmente para navegar menus. Isso permite que loops de
  // espera de input avancem no modo headless sem um pad fisico.
  if (owner == "ControlPad" && n == 1 && !realtime_) {
    // A cada 8 ticks: 256 (confirm) por 6 ticks, depois 16 (Up), 32 (Right),
    // 4, 512, 2048, 4096, 16384 uma vez cada.
    static const int masks[] = {256, 256, 256, 256,  256,  256,   16,
                                32,  4,   512, 2048, 4096, 16384, 0};
    return VmValue::num(masks[syntheticInputTick_ % 14]);
  }
  if (owner == "ControlPad" && n == 0 && !realtime_) {
    // held: mesmo que trig.
    static const int masks[] = {256, 256, 256, 256,  256,  256,   16,
                                32,  4,   512, 2048, 4096, 16384, 0};
    return VmValue::num(masks[syntheticInputTick_ % 14]);
  }
  if (owner == "ControlPad" && n == 2 && !realtime_) {
    // rapid: rotaciona 61440 e 0 para cobrir rapid & 61440.
    return VmValue::num((syntheticInputTick_ % 2 == 0) ? 61440 : 0);
  }
  if (owner == "ControlPad" && n == 5)
    return VmValue::num(0);
  // setLayer only records draw order on the PS2. The port orders pictures by
  // category, so returning self keeps the original wrappers chaining.
  if ((owner == "BG_Picture" && n == 6) || (owner == "Sprite" && n == 6) ||
      (owner == "ActorPicture" && n == 5) ||
      (owner == "ExtraPicture" && n == 5) ||
      (owner == "SnowEffect" && n == 2) || (owner == "FontChar2" && n == 1))
    return r;
  // Music.setup(track): initialize music track.
  if (owner == "Music" && n == 0)
    return r;
  // Carregadores e construtores que apenas retornam sucesso/self, sem
  // modelar a tabela de objetos de tela do PS2.
  if (owner == "K2X_Script" && n == 1) {
    // fileReadBusy: nunca estamos ocupados lendo arquivo em modo headless.
    auto result = VmValue::num(0);
    result.kind = VmValue::Boolean;
    return result;
  }
  if ((owner == "Sprite" && (n == 1 || n == 4 || n == 5)))
    return r;
  // Sprite primitives restantes (open-amagami port/src/gfx.cpp): attach,
  // destruct, update, animate, spin, vibrate, skip, pause -- todos retornam
  // self (no-ops no port, sem modelo de objetos de tela do PS2).
  if (owner == "Sprite" && (n == 2 || n == 3 || n >= 7 && n <= 18))
    return r;
  // ActorPicture primitives restantes: initialize, diversos setters/getters,
  // animate, vibrate, skip, pause -- todos retornam self.
  if (owner == "ActorPicture" && (n >= 1 && n <= 4) ||
      (owner == "ActorPicture" && n >= 6 && n <= 24))
    return r;
  // ExtraPicture primitives restantes: diversos setters/getters, animate,
  // vibrate, skip, pause -- todos retornam self.
  if (owner == "ExtraPicture" && n >= 1 && n <= 4 ||
      (owner == "ExtraPicture" && n >= 6 && n <= 15))
    return r;
  // FontChar primitives restantes: destruct, update, animate, vibrate -- todos
  // retornam self.
  if (owner == "FontChar" && n >= 1 && n <= 10)
    return r;
  // FontChar2 primitives restantes: diversos -- todos retornam self.
  if (owner == "FontChar2" && n >= 2 && n <= 19)
    return r;
  if (owner == "System" && n == 3)
    return r; // System.gc: sem coletor de lixo no port, retorna self.
  // Presentation-only natives reached by conversation, map and ending scenes.
  // The port's renderer does not model the PS2 screen-object tables, but the
  // original scripts must keep running: loaders answer success and the
  // constructors/setters return self so the wrappers keep chaining.
  const auto success = [] {
    auto value = VmValue::num(1);
    value.kind = VmValue::Boolean;
    return value;
  };
  if (classMethod) {
    if ((owner == "Sprite" || owner == "BG_Picture" ||
         owner == "EventPicture" || owner == "Balloon" ||
         owner == "LightBall" || owner == "ClockIcon" ||
         owner == "NodeMapNaviBase" || owner == "CutInWindow") &&
        n == 0)
      return success();
    return std::nullopt;
  }
  if ((owner == "Balloon" && n == 1) || (owner == "LightBall" && n == 1) ||
      (owner == "CutInWindow" && (n == 0 || n == 1)) ||
      (owner == "ClockIcon" && (n == 1 || n == 3)) ||
      (owner == "NodeMapNaviBase" && (n == 1 || n == 2)) ||
      (owner == "StandExt" && n == 0) ||
      (owner == "StructPicture" && (n == 0 || n == 1)) ||
      (owner == "SnowEffect" && (n == 0 || n == 3 || n == 6 || n == 7)) ||
      (owner == "ActorPicture" && n == 25))
    return r;
  // C++ owns the lifetime of the script objects; these original maintenance
  // operations need no PS2 heap/collector and must not pause story time.
  if (owner == "System" && (method == "gc" || method == "FullGC"))
    return r;
  if (owner == "Sound")
    return native(r, method, a);
  // MemoryCard primitives (open-amagami port/src/natives.cpp). Como nao ha
  // cartao de memoria fisico no port, respondem valores padrao que permitem
  // os scripts prosseguirem sem gravar/lidar dados reais.
  if (owner == "MemoryCard") {
    if (n == 0) {
      // isChanged: o cartao nunca muda.
      auto result = VmValue::num(0);
      result.kind = VmValue::Boolean;
      return result;
    }
    if (n == 1) {
      // status: 6 = usavel, sem save ainda.
      return VmValue::num(6);
    }
    if (n == 2) {
      // checkData: retorna self.
      return r;
    }
    if (n >= 3 && n <= 9) {
      // createFileSystemPrim, writeSystemData, readSystemData,
      // readShioriHeadPrim, writeShioriData, readShioriBody,
      // selectDeleteAllSaveData: sem cartao, retorna erro de argumento ou 0.
      return VmValue::num(0);
    }
    if (n == 10) {
      // displaySysMsg: nao ha mensagem, retorna self.
      return r;
    }
    if (n == 11) {
      // waitSysMsgEnd: sem mensagem ativa, retorna false.
      auto result = VmValue::num(0);
      result.kind = VmValue::Boolean;
      return result;
    }
  }
  // Music primitives (open-amagami port/src/audio.cpp). O port nao tem engine
  // de audio real, mas responde valores padrao para que scripts de musica
  // prosseguirem sem travar.
  if (owner == "Music") {
    if (n == 0) {
      // setup(track): prepara a faixa; sem engine de audio, retorna self.
      return r;
    }
    if (n == 3) {
      // pause(flag): pausa/retorna a musica; sem engine, retorna self.
      return r;
    }
    if (n == 4) {
      // setVolumePrim(0..100): define o volume; sem engine, retorna self.
      return r;
    }
    if (n == 5) {
      // isBusy: sem musica tocando, retorna false.
      auto result = VmValue::num(0);
      result.kind = VmValue::Boolean;
      return result;
    }
  }
  // Voice.length: sem engine de voz, retorna 0 frames.
  if (owner == "Voice" && n == 5)
    return VmValue::num(0);
  // Voice device primitives (0-4, 6-10): requerem um dispositivo de voz que
  // nao existe no port; retornam self para nao travar scripts.
  if (owner == "Voice" && (n == 0 || n == 1 || n == 2 || n == 3 || n == 4 ||
                           n == 6 || n == 7 || n == 8 || n == 9 || n == 10))
    return r;
  // K2X_Script primitives (open-amagami port/src/natives.cpp).
  if (owner == "K2X_Script") {
    if (n == 2) {
      // findClass: nao conseguimos resolver nomes dinamicamente, retorna nil.
      return VmValue{};
    }
    if (n == 3) {
      // isEnabled/isReady: sempre verdadeiro.
      auto result = VmValue::num(1);
      result.kind = VmValue::Boolean;
      return result;
    }
    if (n == 4) {
      // getTick: retorno 0 (sem relogio de jogo modelado).
      return VmValue::num(0);
    }
    if (n == 5) {
      // getClock: retorna [mes, dia, hora, minuto] do tempo local do PC.
      std::time_t now = std::time(nullptr);
      std::tm local{};
#ifdef _WIN32
      localtime_s(&local, &now);
#else
      localtime_r(&now, &local);
#endif
      auto arr = std::make_shared<VmObject>();
      arr->type = "Array";
      arr->fields.resize(4, VmValue::num(0));
      arr->fields[0] = VmValue::num(local.tm_mon + 1);
      arr->fields[1] = VmValue::num(local.tm_mday);
      arr->fields[2] = VmValue::num(local.tm_hour);
      arr->fields[3] = VmValue::num(local.tm_min);
      return VmValue::ref(std::move(arr), VmValue::Array);
    }
    if (n >= 6 && n <= 8) {
      // setScrScl, setScrOrgY, unlockTrophy: no-ops no port.
      return r;
    }
  }
  return std::nullopt;
}
void Session::save(const std::string &path) const {
  std::ostringstream out;
  out << (campaignActive_ ? "AMAGAMI_CAMPAIGN_REPLAY 1\n"
                          : "AMAGAMI_REPLAY 2\n")
      << std::quoted(scenario_) << '\n'
      << inputs_.size() << '\n';
  for (int i : inputs_)
    out << i << '\n';
  if (campaignActive_) {
    out << campaignActions_.size() << '\n';
    for (const auto &action : campaignActions_)
      out << std::quoted(action.kind) << ' ' << std::quoted(action.value)
          << '\n';
  }
  out << "STATE " << messageSpeed() << ' ' << autoMode() << ' ' << skipMode()
      << '\n';
  out << "READ " << seenLines_.size() << '\n';
  for (const auto &line : seenLines_)
    out << std::quoted(line) << '\n';
  Kotonoha::writeTextFileAtomically(path, out.str());
}
void Session::load(const std::string &path) {
  Kotonoha::Filesystem::InputFile in(path, std::ios::binary);
  std::string magic, name;
  unsigned version = 0;
  size_t count = 0;
  if (!(in >> magic >> version >> std::quoted(name) >> count) ||
      (magic != "AMAGAMI_REPLAY" && magic != "AMAGAMI_CAMPAIGN_REPLAY") ||
      count > 1000000)
    throw std::runtime_error("Invalid Amagami save");
  std::vector<int> inputs(count);
  for (auto &i : inputs)
    if (!(in >> i) || i < -1 || i > 32)
      throw std::runtime_error("Invalid save input");

  bool isCampaign = (magic == "AMAGAMI_CAMPAIGN_REPLAY");
  size_t actionCount = 0;
  std::vector<CampaignAction> actions;
  if (isCampaign) {
    if (!(in >> actionCount) || actionCount > 100000)
      throw std::runtime_error("Invalid Amagami campaign save actions");
    actions.resize(actionCount);
    for (auto &action : actions) {
      if (!(in >> std::quoted(action.kind) >> std::quoted(action.value)))
        throw std::runtime_error("Invalid Amagami campaign save action entry");
    }
  }

  int speed = 0, automatic = 0, skip = 0;
  std::set<std::string> seen;
  std::string marker;
  size_t number = 0;
  if (!(in >> marker >> speed >> automatic >> skip) || marker != "STATE" ||
      speed < 0 || speed > 3 || automatic < 0 || automatic > 1 || skip < 0 ||
      skip > 1 || !(in >> marker >> number) || marker != "READ" ||
      number > 100000)
    throw std::runtime_error("Invalid Amagami save settings");
  size_t bytes = 0;
  for (size_t i = 0; i < number; ++i) {
    std::string line;
    if (!(in >> std::quoted(line)) || line.empty() || line.size() > 65536 ||
        (bytes += line.size()) > 32 * 1024 * 1024)
      throw std::runtime_error("Invalid Amagami read registry");
    seen.insert(std::move(line));
  }

  Session candidate(source_);
  candidate.replaying_ = true;
  candidate.seenLines_ = std::move(seen);
  if (isCampaign) {
    candidate.startCampaign();
    for (const auto &action : actions) {
      if (action.kind == "event") {
        candidate.selectCampaignEvent(action.value);
        while (!candidate.ended_) {
          if (!candidate.choices_.empty())
            candidate.choose(0);
          else if (candidate.waiting())
            candidate.tick(1);
          else
            candidate.advance();
        }
        candidate.completeCampaignEvent();
      } else if (action.kind == "period") {
        candidate.nextCampaignPeriod();
      } else {
        throw std::runtime_error("Unknown campaign replay action");
      }
    }
  }
  candidate.start(name);
  for (int i : inputs) {
    if (candidate.ended_ || (i == -1 && !candidate.choices_.empty()) ||
        (i >= 0 && size_t(i) >= candidate.choices_.size()))
      throw std::runtime_error("Save replay diverged");
    if (i < 0)
      candidate.advance();
    else
      candidate.choose(size_t(i));
  }
  candidate.setMessageSpeed(speed);
  candidate.setAutoMode(automatic != 0);
  candidate.setSkipMode(skip != 0);
  fibers_.clear();
  tweens_.clear();
  nods_.clear();
  activeFiber_ = nullptr;
  vm_ = std::move(candidate.vm_);
  vm_.native = [this](const auto &r, const auto &s, const auto &a) {
    return native(r, s, a);
  };
  vm_.scopedPrimitive = [this](const std::string &owner,
                               const std::string &method, bool classMethod,
                               const auto &r, unsigned n, const auto &a) {
    return primitive(owner, method, classMethod, r, n, a);
  };
  placeholderTags_ = std::move(candidate.placeholderTags_);
  self_ = std::move(candidate.self_);
  campaignActive_ = candidate.campaignActive_;
  campaign_ = std::move(candidate.campaign_);
  campaignActions_ = std::move(candidate.campaignActions_);
  block_ = candidate.block_;
  musicTrack_ = candidate.musicTrack_;
  musicGain_ = candidate.musicGain_;
  ambienceTrack_ = candidate.ambienceTrack_;
  ambienceGain_ = candidate.ambienceGain_;
  seenLines_ = std::move(candidate.seenLines_);
  ended_ = candidate.ended_;
  scenario_ = std::move(candidate.scenario_);
  dialogue_ = std::move(candidate.dialogue_);
  revealedCues_ = candidate.revealedCues_;
  history_ = std::move(candidate.history_);
  choices_ = std::move(candidate.choices_);
  visuals_ = std::move(candidate.visuals_);
  actorTransitions_.clear();
  breathAmount_ = candidate.breathAmount_;
  actorFadeFrames_ = candidate.actorFadeFrames_;
  displayNames_ = std::move(candidate.displayNames_);
  inputs_ = std::move(candidate.inputs_);
  messageVisible_ = candidate.messageVisible_;
  messageCleared_ = candidate.messageCleared_;
  waitFrames_ = fadeFrames_ = fadeElapsed_ = 0;
  fadeAlpha_ = candidate.fadeAlpha_;
  fadeWhite_ = candidate.fadeWhite_;
  replaying_ = false;
}
} // namespace Amagami
