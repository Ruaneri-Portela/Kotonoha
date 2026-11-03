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
    if (s[i] == 'V' || s[i] == 'W') {
      size_t j = i + 1;
      while (j < s.size() && s[j] >= '0' && s[j] <= '9')
        ++j;
      if (j > i + 1) {
        i = j;
        continue;
      }
    }
    if (s[i] == 'F' || s[i] == 'E' || s[i] == 'L' || s[i] == 'T') {
      size_t j = i + 1;
      while (j < s.size() &&
             ((s[j] >= 'A' && s[j] <= 'Z') || (s[j] >= 'a' && s[j] <= 'z') ||
              (s[j] >= '0' && s[j] <= '9')))
        ++j;
      if (j > i + 1) {
        i = j;
        continue;
      }
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
  scenario_ = name;
  block_ = 0;
  musicTrack_ = -1;
  musicGain_ = 100;
  ended_ = false;
  waitFrames_ = fadeFrames_ = fadeElapsed_ = 0;
  fadeAlpha_ = fadeFrom_ = fadeTarget_ = 0;
  messageVisible_ = false;
  dialogue_ = {};
  choices_.clear();
  history_.clear();
  visuals_.clear();
  displayNames_.clear();
  inputs_.clear();
  self_ = vm_.instance(name);
  for (const auto &entry : source_->classes())
    if (entry.name == "K2X_Script") {
      const auto script = ParseScf(entry.bytes);
      for (const auto &v : script.classVariables)
        vm_.classField(script.name, v.slot) = vm_.instance(v.name);
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
  if (realtime_ && s == "sleep" && !a.empty() && number(a, 0) > 0) {
    waitFrames_ = number(a, 0);
    vm_.pause();
    return r;
  }
  if (s == "fadeBlack" || s == "fadeWhite") {
    fade(1, a.size() >= 2 ? number(a, 1) : 60, s == "fadeWhite");
    return r;
  }
  if (realtime_ && (s == "fadeWait" || s == "wait") &&
      fadeFrames_ > fadeElapsed_) {
    waitFrames_ = fadeFrames_ - fadeElapsed_;
    vm_.pause();
    return r;
  }
  if (t == "WIN" && (s == "hide" || s == "clear" || s == "show")) {
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
  if (t == "Thread" && (s == "start" || s == "kill"))
    return r;
  const std::map<std::string, int> actorIds = {
      {"TUK", 1}, {"RIH", 2}, {"CAO", 3}, {"SAE", 4}, {"HAR", 5},
      {"AII", 6}, {"MIY", 7}, {"RIS", 8}, {"MAY", 9}};
  auto updateActor = [&](const std::string &code) {
    auto id = actorIds.find(code);
    if (id == actorIds.end())
      return;
    auto &v = visuals_[code];
    char b[64], f[64];
    std::snprintf(b, sizeof(b), "stand/b%x_%c%cx.tm2", id->second,
                  'A' + v.cloth, 'a' + v.pose);
    std::snprintf(f, sizeof(f), "stand/f%x_%cx.tm2", id->second, 'A' + v.pose);
    v.archive = "GRAPH/GRAPH2.ARC";
    v.file = b;
    v.face = f;
  };
  if (actorIds.count(t) && (s == "fadeCloth" || s == "setCloth")) {
    auto &v = visuals_[t];
    v.cloth = int(number(a, 0));
    v.pose = int(number(a, 1));
    v.visible = true;
    updateActor(t);
    return r;
  }
  if (actorIds.count(t) && (s == "fadePose" || s == "setPose")) {
    visuals_[t].pose = int(number(a, 0));
    updateActor(t);
    return r;
  }
  if (actorIds.count(t) && s == "setFace") {
    visuals_[t].expression = int(number(a, 0));
    updateActor(t);
    return r;
  }
  if (s.rfind("chFace", 0) == 0 && !a.empty() && actorIds.count(type(a[0]))) {
    auto code = type(a[0]);
    visuals_[code].expression = int(number(a, 1));
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
    char file[64];
    std::snprintf(file, sizeof(file), "stand/e%x_%02d%c.tm2", int(number(a, 0)),
                  int(number(a, 1)), 'a' + int(number(a, 2)));
    auto &v = visuals_[t];
    v.file = file;
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
  if (s == "setPos" && a.size() >= 2) {
    auto &v = visuals_[t];
    v.x = number(a, 0);
    v.y = number(a, 1);
    return r;
  }
  if (s == "setScale" && !a.empty()) {
    auto &v = visuals_[t];
    v.scaleX = number(a, 0, 1);
    v.scaleY = number(a, 1, v.scaleX);
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
  history_ = std::move(candidate.history_);
  choices_ = std::move(candidate.choices_);
  visuals_ = std::move(candidate.visuals_);
  displayNames_ = std::move(candidate.displayNames_);
  inputs_ = std::move(candidate.inputs_);
  messageVisible_ = candidate.messageVisible_;
  waitFrames_ = fadeFrames_ = fadeElapsed_ = 0;
  fadeAlpha_ = candidate.fadeAlpha_;
  fadeWhite_ = candidate.fadeWhite_;
  replaying_ = false;
}
} // namespace Amagami
