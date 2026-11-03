#include <Amagami/Session.hpp>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <iomanip>
#include <set>
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
          "", "TUK", "RIH", "CAO", "SAE", "HAR", "AII", "MIY", "RIS", "MAY"};
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
  tweens_.push_back({name, property, a, b, x, y, 0, frames, curve});
}
void Session::updateTweens(double frames) {
  for (auto &t : tweens_) {
    t.elapsed = std::min(t.frames, t.elapsed + frames);
    double u = t.elapsed / t.frames;
    if (t.curve == 1)
      u = std::sin(u * 1.5707963267948966);
    else if (t.curve == 2)
      u = 1 - std::cos(u * 1.5707963267948966);
    auto &v = visuals_[t.visual];
    float &a = t.property == Tween::Position ? v.x
               : t.property == Tween::Scale  ? v.scaleX
               : t.property == Tween::Alpha  ? v.alpha
                                             : v.angle;
    float &b = t.property == Tween::Position ? v.y
               : t.property == Tween::Scale  ? v.scaleY
               : t.property == Tween::Alpha  ? v.alpha
                                             : v.angle;
    a = t.fromX + (t.toX - t.fromX) * float(u);
    b = t.fromY + (t.toY - t.fromY) * float(u);
  }
  tweens_.erase(
      std::remove_if(tweens_.begin(), tweens_.end(),
                     [](const Tween &t) { return t.elapsed >= t.frames; }),
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
void Session::start(const std::string &name) {
  auto s = source_->script(source_->scenarioIndex(name));
  vm_.add(std::move(s));
  vm_.reset();
  fibers_.clear();
  tweens_.clear();
  nods_.clear();
  activeFiber_ = nullptr;
  presentationFrames_ = 0;
  scenario_ = name;
  block_ = 0;
  musicTrack_ = -1;
  musicGain_ = 100;
  ended_ = false;
  waitFrames_ = fadeFrames_ = fadeElapsed_ = 0;
  fadeAlpha_ = fadeFrom_ = fadeTarget_ = 0;
  messageVisible_ = false;
  messageCleared_ = true;
  dialogue_ = {};
  choices_.clear();
  history_.clear();
  visuals_.clear();
  actorTransitions_.clear();
  breathAmount_ = 0;
  displayNames_.clear();
  inputs_.clear();
  self_ = vm_.instance(name);
  for (const auto &entry : source_->classes())
    if (entry.name == "K2X_Script") {
      const auto script = ParseScf(entry.bytes);
      for (const auto &v : script.classVariables)
        vm_.classField(script.name, v.slot) = vm_.instance(v.name);
    }
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
  vm_.call(self_, "scriptMain", {VmValue::num(0)});
  pump();
}
void Session::pump() {
  for (unsigned hops = 0; hops < 256 && !ended_; ++hops) {
    vm_.run();
    if (vm_.paused())
      return;
    if (!vm_.finished())
      throw std::runtime_error("SCF did not yield");
    const auto result = vm_.result();
    if (result.kind != VmValue::Number) {
      ended_ = true;
      return;
    }
    block_ = int(result.number);
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
  vm_.resume();
  pump();
}
void Session::choose(size_t i) {
  if (i >= choices_.size())
    throw std::out_of_range("Invalid Amagami answer");
  auto result = VmValue::num(choices_[i].value);
  inputs_.push_back(int(i));
  choices_.clear();
  vm_.resume(result);
  pump();
}
std::optional<VmValue> Session::native(const VmValue &r, const std::string &s,
                                       const std::vector<VmValue> &a) {
  const auto t = type(r);
  if (t == "System" && s == "println") {
    if (!a.empty() && a[0].kind == VmValue::Text)
      SDL_LogDebug(SDL_LOG_CATEGORY_APPLICATION, "Amagami script: %s",
                   a[0].text.c_str());
    return r;
  }
  if (s == "ToAnswer" && r.object == self_.object) {
    choices_.clear();
    for (size_t i = 0; i < a.size(); ++i)
      if (a[i].kind == VmValue::Text) {
        int result = i + 1 < a.size() && a[i + 1].kind == VmValue::Number
                         ? int(a[i + 1].number)
                         : int(choices_.size());
        choices_.push_back({DisplayText(a[i].text), result});
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
  if (s == "checkClear" && r.object == self_.object)
    return r;
  if (s == "actTime" && !a.empty())
    return VmValue::num(a[0].number, a[0].floating);
  if (s == "actStart" || s == "actInit")
    return r;
  if (s == "actEnd") {
    if (!activeFiber_) {
      for (auto &fiber : fibers_)
        fiber.killed = true;
      updateTweens(1e12);
    }
    return r;
  }
  if (realtime_ && (s == "sleep" || s == "actSleep" || s == "exsleep") &&
      !a.empty() && number(a, 0) > 0) {
    if (activeFiber_)
      activeFiber_->wait = number(a, 0);
    else
      waitFrames_ = number(a, 0);
    executingVm().pause();
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
        remaining = std::max(remaining, tween.frames - tween.elapsed);
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
    displayNames_[t] = a[0].text;
    return r;
  }
  if (s == "setNameDef") {
    displayNames_.erase(t);
    return r;
  }
  if (s == ":" && a.size() == 1 && a[0].kind == VmValue::Text) {
    messageVisible_ = true;
    messageCleared_ = false;
    dialogue_ = {};
    dialogue_.original = a[0].text;
    dialogue_.text = DisplayText(a[0].text);
    auto sp = speakers.find(t);
    if (sp != speakers.end())
      dialogue_.speaker = sp->second;
    if (auto name = displayNames_.find(t); name != displayNames_.end())
      dialogue_.speaker = name->second;
    // SYS is narration; PLY uses the configured protagonist name.
    if (t == "SYS")
      dialogue_.speaker.clear();
    auto pos = a[0].text.find('V');
    if (pos != std::string::npos && pos + 4 < a[0].text.size()) {
      try {
        dialogue_.voice = std::stoi(a[0].text.substr(pos + 1, 4));
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
        "", "TUK", "RIH", "CAO", "SAE", "HAR", "AII", "MIY", "RIS", "MAY"};
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
      {"TUK", 1}, {"RIH", 2}, {"CAO", 3}, {"SAE", 4}, {"HAR", 5},
      {"AII", 6}, {"MIY", 7}, {"RIS", 8}, {"MAY", 9}};
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
    const auto actor = type(a[0]);
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
  if (s.rfind("chFace", 0) == 0 && !a.empty() && actorIds.count(type(a[0]))) {
    auto code = type(a[0]);
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
        remaining = std::max(remaining, tween.frames - tween.elapsed);
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
      "sleep",     "fadeWait",  "wait",       "actEnd",   "actStart",
      "spin",      "move",      "zoom",       "fade",     "fadeBlack",
      "fadeWhite", "fadeColor", "setDist",    "setLight", "setWhite",
      "setBlack",  "fadeOutW",  "waitVoice",  "preLoad",  "setAmount",
      "destruct",  "setName",   "setNameDef", "step",     "stepEnd",
      "chFace",    "chFaceC",   "chFaceO",    "chFaceHo", "vibPos",
      "vibScale",  "vibAlpha",  "fadeLight",  "zoomWait", "moveWait",
      "lightWait"};
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
void Session::save(const std::string &path) const {
  std::ofstream out(path, std::ios::binary);
  out << "AMAGAMI_REPLAY 1\n"
      << std::quoted(scenario_) << '\n'
      << inputs_.size() << '\n';
  for (int i : inputs_)
    out << i << '\n';
  if (!out)
    throw std::runtime_error("Cannot save Amagami session");
}
void Session::load(const std::string &path) {
  std::ifstream in(path, std::ios::binary);
  std::string magic, name;
  unsigned version = 0;
  size_t count = 0;
  if (!(in >> magic >> version >> std::quoted(name) >> count) ||
      magic != "AMAGAMI_REPLAY" || version != 1 || count > 1000000)
    throw std::runtime_error("Invalid Amagami save");
  std::vector<int> inputs(count);
  for (auto &i : inputs)
    if (!(in >> i) || i < -1 || i > 32)
      throw std::runtime_error("Invalid save input");
  // Replay into a candidate first so malformed or divergent saves leave the
  // running session intact. Map nodes keep VM method pointers valid on move.
  Session candidate(source_);
  candidate.replaying_ = true;
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
  fibers_.clear();
  tweens_.clear();
  nods_.clear();
  activeFiber_ = nullptr;
  vm_ = std::move(candidate.vm_);
  vm_.native = [this](const auto &r, const auto &s, const auto &a) {
    return native(r, s, a);
  };
  self_ = std::move(candidate.self_);
  block_ = candidate.block_;
  musicTrack_ = candidate.musicTrack_;
  musicGain_ = candidate.musicGain_;
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
