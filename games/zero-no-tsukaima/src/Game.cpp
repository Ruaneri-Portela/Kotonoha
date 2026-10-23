#include <ZeroNoTsukaima/Game.hpp>

#include <algorithm>
#include <cctype>
#include <cstdlib>

namespace ZeroNoTsukaima {

namespace {

std::string Upper(std::string s) {
  for (char &c : s) c = char(std::toupper(static_cast<unsigned char>(c)));
  return s;
}

// Recursive-descent evaluator for the small expressions used in "eval" and
// "if" attributes: integers, flag names, ! && || and arithmetic/comparison.
struct Expr {
  const std::string &s;
  const std::map<std::string, int64_t> &flags;
  size_t p = 0;

  void Ws() { while (p < s.size() && std::isspace(static_cast<unsigned char>(s[p]))) ++p; }
  bool Eat(const char *t) {
    Ws();
    const size_t n = std::char_traits<char>::length(t);
    if (s.compare(p, n, t) == 0) { p += n; return true; }
    return false;
  }
  int64_t Primary() {
    Ws();
    if (p >= s.size()) return 0;
    if (Eat("(")) { int64_t v = Or(); Eat(")"); return v; }
    if (Eat("!")) return !Primary();
    if (s[p] == '-') { ++p; return -Primary(); }
    if (std::isdigit(static_cast<unsigned char>(s[p]))) {
      size_t q = p;
      int64_t v = 0;
      if (s.compare(p, 2, "0x") == 0) { q += 2; while (q < s.size() && std::isxdigit(static_cast<unsigned char>(s[q]))) v = v * 16 + std::stoi(std::string(1, s[q++]), nullptr, 16); }
      else while (q < s.size() && std::isdigit(static_cast<unsigned char>(s[q]))) v = v * 10 + (s[q++] - '0');
      p = q;
      return v;
    }
    size_t q = p;
    while (q < s.size() && (std::isalnum(static_cast<unsigned char>(s[q])) || s[q] == '_' || s[q] == '.' || static_cast<unsigned char>(s[q]) >= 0x80)) ++q;
    if (q == p) { ++p; return 0; }
    const std::string id = s.substr(p, q - p);
    p = q;
    if (id == "true") return 1;
    if (id == "false") return 0;
    auto it = flags.find(id);
    return it == flags.end() ? 0 : it->second;
  }
  int64_t Mul() {
    int64_t v = Primary();
    for (;;) {
      if (Eat("*")) v *= Primary();
      else if (Eat("/")) { int64_t d = Primary(); v = d ? v / d : 0; }
      else if (Eat("%")) { int64_t d = Primary(); v = d ? v % d : 0; }
      else return v;
    }
  }
  int64_t Add() {
    int64_t v = Mul();
    for (;;) {
      if (Eat("+")) v += Mul();
      else if (Eat("-")) v -= Mul();
      else return v;
    }
  }
  int64_t Cmp() {
    int64_t v = Add();
    for (;;) {
      if (Eat("==")) v = v == Add();
      else if (Eat("!=")) v = v != Add();
      else if (Eat("<=")) v = v <= Add();
      else if (Eat(">=")) v = v >= Add();
      else if (Eat("<")) v = v < Add();
      else if (Eat(">")) v = v > Add();
      else return v;
    }
  }
  int64_t And() {
    int64_t v = Cmp();
    while (Eat("&&")) { int64_t r = Cmp(); v = v && r; }
    return v;
  }
  int64_t Or() {
    int64_t v = And();
    while (Eat("||")) { int64_t r = And(); v = v || r; }
    return v;
  }
};


// ENVINIT "actions" (LayerVibrateActionModule / LayerDecelMoveModule). The
// jump / zoom modules (びょんびょん, おじぎ, どっきり) are accepted and ignored.
struct ActionDef {
  const char *name;
  int vibration, interval; // shake amplitude, update period (ms)
  bool move;
  int x, y, time;          // decelerated move target and duration
};
constexpr ActionDef kActions[] = {
    {"揺れ０", 7, 50, false, 0, 0, 0},       {"揺れ１", 14, 80, false, 0, 0, 0},
    {"揺れ２", 21, 110, false, 0, 0, 0},     {"揺れ０継続", 7, 16, false, 0, 0, 0},
    {"揺れ１継続", 14, 16, false, 0, 0, 0},  {"揺れ２継続", 21, 16, false, 0, 0, 0},
    {"揺らす小", 12, 30, false, 0, 0, 0},    {"揺らす中", 30, 200, false, 0, 0, 0},
    {"揺らす大", 70, 300, false, 0, 0, 0},   {"ガクガク", 3, 40, false, 0, 0, 0},
    {"上段", 0, 0, true, 0, 0, 2000},        {"下段", 0, 0, true, 0, -268, 2000},
    {"下段２", 0, 0, true, 0, -412, 2000},   {"下段３", 0, 0, true, 0, -408, 2000},
    {"下段４", 0, 0, true, 0, -462, 2000},   {"リシュ登場", 0, 0, true, 0, -284, 2000},
    {"背景左", 0, 0, true, 0, 0, 1000},      {"背景真ん中", 0, 0, true, -400, 0, 1000},
    {"背景右", 0, 0, true, -800, 0, 1000},   {"アクション停止", 0, 0, false, 0, 0, 0},
    {"びょんびょん", 0, 0, false, 0, 0, 0},  {"おじぎ", 0, 0, false, 0, 0, 0},
    {"どっきり", 0, 0, false, 0, 0, 0},
};

const ActionDef *FindAction(const std::string &key) {
  for (const ActionDef &a : kActions)
    if (key == a.name) return &a;
  return nullptr;
}

// ENVINIT "times".
bool IsTime(const std::string &w) {
  return w == "昼" || w == "夕" || w == "夜" || w == "夜闇" || w == "夕室内" || w == "夜室内" || w == "特殊背景" ||
         w == "特殊背景２";
}

// Tag attributes that are parameters rather than names.
bool IsParam(const std::string &k) {
  static const char *const kParams[] = {"tagname", "time", "trans", "fade", "sync", "nosync", "xpos", "ypos",
                                        "hide", "show", "reset", "file", "error", "level", "opacity", "stage",
                                        "stime", "buf", "wait", "stop", "loop", "name", "if", "disp", "left", "top"};
  for (const char *p : kParams)
    if (k == p) return true;
  return false;
}

int IntAttr(const SceneTag &tag, const char *key, int fallback) {
  const std::string *v = tag.Get(key);
  return v && !v->empty() ? std::atoi(v->c_str()) : fallback;
}

// Deterministic shake offset in [-amplitude/2, amplitude/2] for one update period.
float ShakeOffset(int amplitude, int64_t period, uint32_t salt) {
  uint32_t h = uint32_t(period) * 2654435761u ^ salt * 40503u;
  h ^= h >> 15;
  h *= 2246822519u;
  h ^= h >> 13;
  return float(int(h % uint32_t(amplitude + 1)) - amplitude / 2);
}

} // namespace

Game::Game(std::vector<std::string> sceneNames, SceneLoader loader)
    : names_(std::move(sceneNames)), loader_(std::move(loader)) {}

int64_t Game::Flag(const std::string &name) const {
  auto it = flags_.find(name);
  return it == flags_.end() ? 0 : it->second;
}

int64_t Game::Eval(const std::string &expression) const {
  Expr e{expression, flags_};
  return e.Or();
}

void Game::Assign(const std::string &expression) {
  // Find the assignment operator: a lone '=' (not ==, !=, <=, >=).
  size_t eq = std::string::npos;
  for (size_t i = 0; i < expression.size(); ++i) {
    if (expression[i] != '=') continue;
    const char prev = i ? expression[i - 1] : 0;
    const char next = i + 1 < expression.size() ? expression[i + 1] : 0;
    if (next == '=' || prev == '!' || prev == '<' || prev == '>' || prev == '=') { if (next == '=') ++i; continue; }
    eq = i;
    break;
  }
  if (eq == std::string::npos) {
    // "name++" / "name--"
    for (const char *op : {"++", "--"}) {
      const size_t at = expression.find(op);
      if (at != std::string::npos) {
        std::string name = expression.substr(0, at);
        flags_[name] += op[0] == '+' ? 1 : -1;
      }
    }
    return;
  }
  char compound = 0;
  size_t nameEnd = eq;
  if (eq > 0 && (expression[eq - 1] == '+' || expression[eq - 1] == '-' || expression[eq - 1] == '*')) {
    compound = expression[eq - 1];
    --nameEnd;
  }
  std::string name = expression.substr(0, nameEnd);
  name.erase(std::remove_if(name.begin(), name.end(), [](unsigned char c) { return std::isspace(c); }), name.end());
  std::string rhs = expression.substr(eq + 1);
  rhs.erase(std::remove_if(rhs.begin(), rhs.end(), [](unsigned char c) { return std::isspace(c); }), rhs.end());
  if (!compound && strings_.count(rhs)) {
    // String copy (f.coffeetitle = f.title).
    strings_[name] = strings_[rhs];
    return;
  }
  const int64_t value = Eval(expression.substr(eq + 1));
  int64_t &slot = flags_[name];
  if (compound == '+') slot += value;
  else if (compound == '-') slot -= value;
  else if (compound == '*') slot *= value;
  else slot = value;
}

bool Game::LoadScene(size_t index, const std::string &label) {
  std::vector<uint8_t> data;
  tags_.clear();
  labels_.clear();
  if (index >= names_.size() || !loader_(index, &data) || !ParseSceneTags(data, &tags_)) return false;
  sceneIndex_ = index;
  scriptName_ = names_[index];
  for (size_t i = 0; i < tags_.size(); ++i)
    if (tags_[i].name == "label")
      if (const std::string *l = tags_[i].Get("label")) labels_.emplace(*l, i);
  pc_ = 0;
  if (!label.empty()) Jump(label);
  return true;
}

bool Game::LoadByName(const std::string &name) {
  std::string key = Upper(name);
  if (key.size() < 3 || key.compare(key.size() - 3, 3, ".KS") != 0) key += ".KS";
  for (size_t i = 0; i < names_.size(); ++i)
    if (Upper(names_[i]) == key) return LoadScene(i, "");
  return false;
}

bool Game::Jump(const std::string &label) {
  auto it = labels_.find(label);
  if (it == labels_.end()) it = labels_.find(label.size() && label[0] == '*' ? label : "*" + label);
  if (it == labels_.end()) return false;
  pc_ = it->second;
  return true;
}

bool Game::Start(const std::string &scriptName) {
  finished_ = false;
  return LoadByName(scriptName);
}

float Game::Anim::At(int64_t t) const {
  if (duration <= 0 || t >= start + duration) return to;
  if (t <= start) return from;
  float k = float(t - start) / float(duration);
  if (decel) k = 1.0f - (1.0f - k) * (1.0f - k);
  return from + (to - from) * k;
}

void Game::Anim::To(int64_t now, float target, int ms, bool decelerate) {
  from = At(now);
  to = target;
  start = now;
  duration = ms;
  decel = decelerate;
}

std::string Game::Stage() const { return stage_.visible ? stage_.image : std::string(); }

Game::Block Game::Blocked() const {
  if (finished_) return Block::End;
  if (choosing_) return Block::Choice;
  if (waiting_) return Block::Click;
  if (waitUntil_ >= 0) return Block::Time;
  return external_;
}

void Game::WaitFor(int ms) {
  if (ms <= 0) return;
  if (skipping_) clock_ += ms;
  else waitUntil_ = clock_ + ms;
}

int64_t Game::ActionsEnd() const {
  int64_t end = 0;
  auto add = [&](const Props &p) { end = std::max({end, p.alpha.End(), p.x.End(), p.y.End()}); };
  add(stage_.p);
  add(event_.p);
  for (const CharacterState &c : characters_) add(c.p);
  for (const LayerState &l : layers_) add(l.p);
  return end;
}

void Game::StopActions() {
  auto stop = [](Props &p) {
    p.shake = 0;
    p.alpha.Set(p.alpha.to);
    p.x.Set(p.x.to);
    p.y.Set(p.y.to);
  };
  stop(stage_.p);
  stop(event_.p);
  for (CharacterState &c : characters_) stop(c.p);
  for (LayerState &l : layers_) stop(l.p);
}

bool Game::ApplyAction(const std::string &key, Props &p, bool isEvent) {
  if (key == "reset") {
    p.shake = 0;
    return true;
  }
  const ActionDef *a = FindAction(key);
  if (!a) return false;
  if (a->vibration > 0) {
    p.shake = a->vibration;
    p.shakeInterval = a->interval;
  } else if (a->move) {
    p.x.To(clock_, float(a->x), a->time, true);
    p.y.To(clock_, float(a->y), a->time, true);
  } else if (std::string(a->name) == "アクション停止") {
    p.shake = 0;
  }
  (void)isEvent;
  return true;
}

void Game::ApplyActions(const SceneTag &tag, Props &p, bool isEvent) {
  for (const auto &[k, v] : tag.attrs) ApplyAction(k, p, isEvent);
}

// Environment auto transition (stageTrans / eventTrans / timeTrans): the scene
// before the tag fades into the new one; the story waits unless nosync.
void Game::AutoTransition(const SceneTag &tag, int ms, bool hideMessage) {
  if (hideMessage) {
    msgOn_ = false;
    text_.clear();
    speaker_.clear();
  }
  if (transMode_ || skipping_ || ms <= 0) return;
  if (tag.Get("nosync")) return;
  WaitFor(ms);
}

bool Game::ExecuteStage(const SceneTag &tag, const std::string &bareName) {
  const std::vector<Element> before = SceneAt(clock_);
  std::string name = bareName, time;
  for (const auto &[k, v] : tag.attrs) {
    if (k == "stage") name = v;
    else if (k == "stime") time = v;
    else if (IsTime(k)) time = k;
    else if (ApplyAction(k, stage_.p, false) || IsParam(k)) continue;
    else if (stageNames_.empty() || stageNames_.count(k)) name = k;
  }
  if (const std::string *x = tag.Get("xpos")) stage_.p.x.Set(float(std::atoi(x->c_str())));
  if (const std::string *y = tag.Get("ypos")) stage_.p.y.Set(float(std::atoi(y->c_str())));
  if (tag.Get("hide")) {
    stage_.visible = false;
    ++version_;
    return true;
  }
  if (name.empty() && time.empty()) return true;
  if (!name.empty()) {
    stageName_ = name;
    stage_.p = Props{};
    // stageTrans "stagecharoff": the characters and the event leave with the old stage.
    for (CharacterState &c : characters_) c.visible = false;
    event_.visible = false;
  }
  if (!time.empty()) stageTime_ = time;
  stage_.image = stageName_ + (stageTime_.empty() ? "" : " " + stageTime_);
  stage_.visible = true;
  ++version_;
  if (!transMode_) {
    const int ms = IntAttr(tag, "time", name.empty() ? 300 : 1000);
    transition_ = {before, clock_, ms, transition_.serial + 1};
    AutoTransition(tag, ms, !name.empty());
  }
  return true;
}

void Game::ExecuteEvent(const SceneTag &tag) {
  const std::vector<Element> before = SceneAt(clock_);
  std::string name;
  if (const std::string *f = tag.Get("file")) name = *f;
  for (const auto &[k, v] : tag.attrs) {
    if (ApplyAction(k, event_.p, true) || IsParam(k)) continue;
    name = k;
  }
  if (tag.Get("hide")) {
    event_.visible = false;
    ++version_;
    return;
  }
  if (!name.empty()) {
    event_.image = name;
    event_.visible = true;
    const Props keep = event_.p;
    event_.p = Props{};
    event_.p.shake = keep.shake;
    event_.p.shakeInterval = keep.shakeInterval;
  }
  if (const std::string *x = tag.Get("xpos")) event_.p.x.Set(float(std::atoi(x->c_str())));
  if (const std::string *y = tag.Get("ypos")) event_.p.y.Set(float(std::atoi(y->c_str())));
  ++version_;
  if (!name.empty() && !transMode_) {
    const int ms = IntAttr(tag, "time", 300);
    transition_ = {before, clock_, ms, transition_.serial + 1};
    AutoTransition(tag, ms, true);
  }
}

bool Game::ExecuteLayer(const SceneTag &tag) {
  auto it = std::find_if(layers_.begin(), layers_.end(), [&](const LayerState &l) { return l.name == tag.name; });
  if (it == layers_.end()) return false;
  LayerState &l = *it;
  if (const std::string *o = tag.Get("opacity")) {
    l.visible = true;
    const float target = float(std::clamp(std::atoi(o->c_str()), 0, 255)) / 255.0f;
    const int ms = skipping_ ? 0 : IntAttr(tag, "time", 0);
    if (ms > 0) l.p.alpha.To(clock_, target, ms);
    else l.p.alpha.Set(target);
  }
  if (tag.Get("show")) l.visible = true;
  if (tag.Get("hide")) l.visible = false;
  if (const std::string *x = tag.Get("xpos")) l.p.x.Set(float(std::atoi(x->c_str())));
  if (const std::string *y = tag.Get("ypos")) l.p.y.Set(float(std::atoi(y->c_str())));
  ApplyActions(tag, l.p);
  ++version_;
  return true;
}

void Game::Execute(const SceneTag &tag) {
  const std::string &n = tag.name;
  if (const std::string *cond = tag.Get("if"))
    if (!Eval(*cond)) return;
  auto attr = [&](const char *k) -> std::string {
    const std::string *v = tag.Get(k);
    return v ? *v : std::string();
  };

  if (n == "ch") text_ += attr("text");
  else if (n == "r") text_ += "\n";
  else if (n == "p") {
    if (!text_.empty()) {
      waiting_ = true;
      pagePc_ = pc_;
    }
  }
  else if (n == "er") text_.clear();
  else if (n == "msgon") msgOn_ = true;
  else if (n == "msgoff") { msgOn_ = false; text_.clear(); speaker_.clear(); }
  else if (n == "dispname") {
    speaker_ = attr("disp").empty() ? attr("name") : attr("disp");
    const std::string v = attr("voice");
    voice_ = v.empty() ? 0 : std::atoi(v.c_str());
  } else if (n == "eval") Assign(attr("exp"));
  else if (n == "set") {
    const std::string value = attr("value");
    const std::string up = Upper(value);
    if (up.size() > 3 && up.compare(up.size() - 3, 3, ".KS") == 0) strings_[attr("name")] = value;
    else flags_[attr("name")] = Eval(value);
  } else if (n == "goto") {
    if (const std::string *s = tag.Get("storage")) { LoadByName(*s); if (tag.Get("target")) Jump(attr("target")); }
    else Jump(attr("target"));
  } else if (n == "title") {
    strings_["f.title"] = attr("name");
  } else if (n == "selopt") {
    if (tag.Get("left")) selectArea_.x = IntAttr(tag, "left", 0);
    if (tag.Get("top")) selectArea_.y = IntAttr(tag, "top", 0);
    if (tag.Get("width")) selectArea_.w = IntAttr(tag, "width", 640);
    if (tag.Get("height")) selectArea_.h = IntAttr(tag, "height", 448);
  } else if (n == "好感度") {
    // LikeMeter: icon + up/down mark (SE04 / SE05), 1500 ms. No variable changes.
    std::string who = Upper(attr("name"));
    if (!skipping_ && !who.empty()) {
      const bool up = !tag.Get("action") || attr("action") == "up";
      meter_ = {Meter::Like, who, up ? 1 : -1, 0, 0, clock_, clock_ + 1500};
      cues_.push_back({Cue::Se, up ? "SYS_SE04" : "SYS_SE05", 0});
      WaitFor(1500);
    }
  } else if (n == "ツンデレパラメータ") {
    // TndlMeter.work: f.prm_tundere_eval += param, clamped to 0..255.
    const int param = IntAttr(tag, "param", 0);
    const int from = int(flags_["f.prm_tundere_eval"]);
    const int to = std::clamp(from + param, 0, 255);
    flags_["f.prm_tundere_eval"] = to;
    if (!skipping_) {
      const int ms = param != 0 ? 1500 : 1000;
      meter_ = {Meter::Tundere, {}, param, from, to, clock_, clock_ + ms};
      if (param != 0) cues_.push_back({Cue::Se, param > 0 ? "SYS_SE04" : "SYS_SE05", 0});
      WaitFor(ms);
    }
  } else if (n == "tndlslot") {
    roulette_ = choosing_ = true;
    rouletteTsun_ = tag.Get("tun") && attr("tun") != "false" && attr("tun") != "0";
    choiceText_.clear();
  } else if (n == "next" || n == "coffee") {
    if (n == "coffee") {
      // ScenePlayer.coffee: f.title = f.coffeetitle, then the coffee-break menu over the next story.
      auto ct = strings_.find("f.coffeetitle");
      if (ct != strings_.end() && !ct->second.empty()) strings_["f.title"] = ct->second;
      strings_.erase("f.coffeetitle");
      external_ = Block::Coffee;
    }
    // "@expr" storages name a string variable (tag_next: nextStorage = eval(...)).
    std::string storage = attr("storage");
    if (!storage.empty() && storage[0] == '@') {
      auto it = strings_.find(storage.substr(1));
      storage = it == strings_.end() ? std::string() : it->second;
    }
    if (storage.empty() || !LoadByName(storage)) finished_ = true;
  } else if (n == "moveinit") {
    moves_.clear();
    moveCount_ = IntAttr(tag, "count", 0);
    moveBack_ = tag.Get("back") ? attr("back") : "BG01A";
    moveBgm_ = tag.Get("bgm") ? attr("bgm") : "BGM02";
    moveDone_ = attr("storage");
  } else if (n == "moveadd") {
    if (moves_.size() < 5) moves_.push_back({attr("text"), attr("pose"), attr("storage")});
  } else if (n == "move") {
    // doMove: the map while visits remain, then the moveinit storage.
    if (moveCount_ > 0 && !moves_.empty()) {
      moving_ = choosing_ = true;
      moveShown_ = moves_;
      choiceText_.clear();
      for (const auto &m : moves_) choiceText_.push_back(m.text);
      if (!moveBgm_.empty()) bgm_ = moveBgm_;
      msgOn_ = false;
    } else if (!moveDone_.empty()) {
      const std::string done = moveDone_;
      moveDone_.clear();
      if (!LoadByName(done)) finished_ = true;
    }
  } else if (n == "seladd") {
    choiceText_.push_back(attr("text"));
    choiceTarget_.push_back(attr("target"));
  } else if (n == "select") {
    if (!choiceText_.empty()) choosing_ = true;
  } else if (n == "wait") {
    int ms = IntAttr(tag, "time", 0);
    for (const auto &[k, v] : tag.attrs)
      if (!ms && !k.empty() && std::isdigit(static_cast<unsigned char>(k[0]))) ms = std::atoi(k.c_str());
    WaitFor(ms);
  } else if (n == "wact") {
    const int64_t end = ActionsEnd();
    if (end > clock_) WaitFor(int(end - clock_));
  } else if (n == "stopaction") {
    StopActions();
    ++version_;
  } else if (n == "beginskip") {
    inSkip_ = true;
  } else if (n == "endskip") {
    inSkip_ = skipping_ = false;
  } else if (n == "begintrans") {
    transMode_ = true;
    transFrom_ = SceneAt(clock_);
  } else if (n == "endtrans") {
    transMode_ = false;
    const int ms = IntAttr(tag, "time", 300);
    if (!skipping_) {
      transition_ = {std::move(transFrom_), clock_, ms, transition_.serial + 1};
      WaitFor(ms);
    }
    transFrom_.clear();
    ++version_;
  } else if (n == "stage") {
    ExecuteStage(tag, "");
  } else if (n == "ev") {
    ExecuteEvent(tag);
  } else if (n == "hidebase") {
    stage_.visible = event_.visible = false;
    ++version_;
  } else if (n == "bgm") {
    bgm_.clear();
    for (const auto &[k, v] : tag.attrs)
      if (!IsParam(k)) bgm_ = k;
    ++version_;
  } else if (n == "se") {
    if (tag.Get("wait")) {
      if (!skipping_) external_ = Block::Se;
    } else if (tag.Get("stop")) {
      cues_.push_back({Cue::Se, "", 0});
    } else {
      for (const auto &[k, v] : tag.attrs)
        if (!IsParam(k)) cues_.push_back({Cue::Se, k, 0});
    }
  } else if (n == "entryvoice") {
    entryVoice_ = IntAttr(tag, "voice", 0);
  } else if (n == "playvoice") {
    if (entryVoice_ > 0) cues_.push_back({Cue::Voice, "", entryVoice_});
    entryVoice_ = 0;
  } else if (n == "waitvoice") {
    if (!skipping_) external_ = Block::Voice;
  } else if (n == "movie") {
    movie_ = IntAttr(tag, "no", 0);
    cues_.push_back({Cue::Movie, "", movie_});
    if (!skipping_) external_ = Block::Movie;
  } else if (n == "newlay") {
    LayerState l;
    l.name = attr("name");
    l.file = attr("file");
    l.level = IntAttr(tag, "level", 0);
    l.visible = !tag.Get("hide");
    l.p.alpha.Set(float(std::clamp(IntAttr(tag, "opacity", 255), 0, 255)) / 255.0f);
    l.p.x.Set(float(IntAttr(tag, "xpos", 0)));
    l.p.y.Set(float(IntAttr(tag, "ypos", 0)));
    layers_.erase(std::remove_if(layers_.begin(), layers_.end(), [&](const LayerState &o) { return o.name == l.name; }),
                  layers_.end());
    layers_.push_back(l);
    ++version_;
  } else if (n == "dellay") {
    for (const auto &attr : tag.attrs)
      layers_.erase(std::remove_if(layers_.begin(), layers_.end(),
                                   [&](const LayerState &o) { return o.name == attr.first; }),
                    layers_.end());
    ++version_;
  } else if (n == "allchar" || n == "hidecharacters") {
    const bool fade = n == "allchar" && !transMode_ && !skipping_;
    for (CharacterState &c : characters_) {
      ApplyActions(tag, c.p);
      if ((tag.Get("hide") || n == "hidecharacters") && c.visible) {
        c.visible = false;
        if (fade) {
          // showslide hide action: slide 50 px right while fading out.
          c.p.alpha.To(clock_, 0, 200);
          c.p.x.To(clock_, 50, 200);
        } else {
          c.p.alpha.Set(0);
        }
      }
    }
    ++version_;
  } else if (ExecuteCharacter(tag) || ExecuteLayer(tag)) {
    ++version_;
  } else if (IsTime(n)) {
    SceneTag t = tag;
    t.attrs["stime"] = n;
    ExecuteStage(t, "");
  } else if (stageNames_.count(n)) {
    ExecuteStage(tag, n);
  } else if (n.compare(0, 3, "bgm") == 0) {
    bgm_ = n;
    ++version_;
  }
}

bool Game::ExecuteCharacter(const SceneTag &tag) {
  if (!table_) return false;
  auto info = table_->characters.find(tag.name);
  if (info == table_->characters.end()) return false;
  auto it = std::find_if(characters_.begin(), characters_.end(),
                         [&](const CharacterState &c) { return c.name == tag.name; });
  if (it == characters_.end()) {
    CharacterState c;
    c.name = tag.name;
    c.pose = info->second.defaultPose;
    characters_.push_back(c);
    it = characters_.end() - 1;
  }
  CharacterState &c = *it;
  // The pose decides which dress / face names are valid, so it is applied first.
  for (const auto &[k, v] : tag.attrs)
    if (info->second.poses.count(k)) c.pose = k;
  auto pose = info->second.poses.find(c.pose);
  bool show = false, hide = false;
  for (const auto &[k, v] : tag.attrs) {
    if (ApplyAction(k, c.p, false)) continue;
    if (pose != info->second.poses.end()) {
      if (pose->second.dresses.count(k)) c.dress = k;
      if (pose->second.faces.count(k)) c.face = k;
    }
    auto p = table_->positions.find(k);
    if (p == table_->positions.end()) continue;
    if (p->second.hasX) c.xpos = p->second.relative ? c.xpos + p->second.xpos : p->second.xpos;
    if (p->second.disp == 1 || p->second.disp == 2) show = true;
    else if (p->second.disp != 0) hide = true;
  }
  if (tag.Get("hide")) hide = true;
  const bool animate = !transMode_ && !skipping_;
  if (hide && c.visible) {
    c.visible = false;
    if (animate) {
      c.p.alpha.To(clock_, 0, 200);
      c.p.x.To(clock_, 50, 200);
    } else {
      c.p.alpha.Set(0);
    }
  }
  if (show) {
    const bool appearing = !c.visible;
    c.visible = true;
    if (appearing) {
      // charDispTrans "showslide": slide in 50 px from the left while fading in.
      if (animate) {
        c.p.alpha.Set(0);
        c.p.alpha.To(clock_, 1, 200);
        c.p.x.Set(-50);
        c.p.x.To(clock_, 0, 200);
        if (!tag.Get("nosync")) WaitFor(200);
      } else {
        c.p.alpha.Set(1);
        c.p.x.Set(0);
      }
      // A character that (re)appears is drawn in front of the others.
      CharacterState moved = c;
      characters_.erase(it);
      characters_.push_back(moved);
    }
  }
  return true;
}

std::vector<Game::Sprite> Game::Sprites() const {
  std::vector<Sprite> out;
  if (!table_) return out;
  for (const CharacterState &c : characters_) {
    if (!c.visible) continue;
    auto info = table_->characters.find(c.name);
    if (info == table_->characters.end()) continue;
    auto pose = info->second.poses.find(c.pose);
    if (pose == info->second.poses.end()) continue;
    const CharacterPose &p = pose->second;
    auto d = p.dresses.find(c.dress);
    auto f = p.faces.find(c.face);
    out.push_back({c.name, p.prefix + "_" + (d != p.dresses.end() ? d->second : p.defaultDress) +
                               (f != p.faces.end() ? f->second : p.defaultFace),
                   c.xpos});
  }
  return out;
}

// Original layer order (GamePlayer indices): stage 100, characters by level
// (level+1)*200, event 1100, free layers by level (6 = 1400, 7 = 1600).
std::vector<Game::Element> Game::SceneAt(int64_t t) const {
  std::vector<Element> out;
  uint32_t salt = 1;
  auto place = [&](Element e, const Props &p) {
    e.x += p.x.At(t);
    e.y += p.y.At(t);
    e.alpha = p.alpha.At(t);
    if (p.shake > 0) {
      const int64_t period = t / std::max(16, p.shakeInterval);
      e.x += ShakeOffset(p.shake, period, salt * 2);
      e.y += ShakeOffset(p.shake, period, salt * 2 + 1);
    }
    ++salt;
    if (e.alpha > 0.0f) out.push_back(std::move(e));
  };
  auto layersAt = [&](int minLevel, int maxLevel) {
    for (const LayerState &l : layers_)
      if (l.visible && l.level >= minLevel && l.level <= maxLevel) place({Element::Layer, l.file}, l.p);
  };
  if (stage_.visible) place({Element::Stage, stage_.image}, stage_.p);
  layersAt(-100, 4);
  if (table_) {
    for (const CharacterState &c : characters_) {
      if (!c.visible && c.p.alpha.At(t) <= 0.0f) continue;
      auto info = table_->characters.find(c.name);
      if (info == table_->characters.end()) continue;
      auto pose = info->second.poses.find(c.pose);
      if (pose == info->second.poses.end()) continue;
      const CharacterPose &cp = pose->second;
      auto d = cp.dresses.find(c.dress);
      auto f = cp.faces.find(c.face);
      Element e{Element::Character,
                cp.prefix + "_" + (d != cp.dresses.end() ? d->second : cp.defaultDress) +
                    (f != cp.faces.end() ? f->second : cp.defaultFace),
                float(c.xpos), 0, 1};
      // charLightColor / charBrightness approximated as a multiply: 夕 0x40B67F71 (warm, light),
      // 夜 / 夜闇 0xFF42457B (blue, darker).
      if (stage_.visible && stageTime_ == "夕") {
        e.tint[0] = 0.92f, e.tint[1] = 0.86f, e.tint[2] = 0.84f;
      } else if (stage_.visible && (stageTime_ == "夜" || stageTime_ == "夜闇")) {
        e.tint[0] = 0.64f, e.tint[1] = 0.66f, e.tint[2] = 0.82f;
      }
      place(e, c.p);
    }
  }
  if (event_.visible) place({Element::Event, event_.image}, event_.p);
  layersAt(5, 100);
  return out;
}

const Game::Transition *Game::ActiveTransition(float *progress) const {
  if (transition_.serial == 0 || transition_.duration <= 0) return nullptr;
  const int64_t elapsed = clock_ - transition_.start;
  if (elapsed >= transition_.duration) return nullptr;
  if (progress) *progress = std::clamp(float(elapsed) / float(transition_.duration), 0.0f, 1.0f);
  return &transition_;
}

void Game::Pump() {
  for (int guard = 0; guard < 200000 && Blocked() == Block::None; ++guard) {
    if (pc_ >= tags_.size()) {
      // End of the scenario with no next scene (SCENES.NUT has no "next" entries): the story stops.
      finished_ = true;
      break;
    }
    Execute(tags_[pc_++]);
  }
}

void Game::Advance(int64_t ms) {
  if (ms <= 0) return;
  clock_ += ms;
  if (waitUntil_ >= 0 && clock_ >= waitUntil_) {
    waitUntil_ = -1;
    Pump();
  }
}

void Game::FinishWait(bool skipSection) {
  const Block b = Blocked();
  if (b != Block::Time && b != Block::Voice && b != Block::Se && b != Block::Movie && b != Block::Coffee) return;
  if (waitUntil_ >= 0) clock_ = std::max(clock_, waitUntil_);
  waitUntil_ = -1;
  // NEXT on the coffee break: MusicStop(0, 0).
  if (b == Block::Coffee) bgm_.clear();
  external_ = Block::None;
  if (skipSection && inSkip_) skipping_ = true;
  Pump();
}

bool Game::Click() {
  if (!waiting_) return false;
  waiting_ = false;
  return true;
}

bool Game::Choose(size_t index) {
  if (choosing_ && roulette_) {
    flags_["f.prm_tundere_boot"] = int64_t(index);
    roulette_ = choosing_ = false;
    bgm_.clear(); // RouletteState.done: MusicStop(0, 500)
    Pump();
    return true;
  }
  if (choosing_ && moving_) {
    if (index >= moveShown_.size()) return false;
    // MoveState.select: drop the destination, count down and go there; the map music stops.
    const MoveDestination pick = moveShown_[index];
    for (auto it = moves_.begin(); it != moves_.end(); ++it)
      if (it->storage == pick.storage && it->text == pick.text) {
        moves_.erase(it);
        break;
      }
    --moveCount_;
    choosing_ = moving_ = false;
    choiceText_.clear();
    moveShown_.clear();
    if (!moveBgm_.empty()) bgm_.clear();
    if (!LoadByName(pick.storage)) finished_ = true;
    return true;
  }
  if (!choosing_ || index >= choiceTarget_.size()) return false;
  const std::string target = choiceTarget_[index];
  choiceText_.clear();
  choiceTarget_.clear();
  choosing_ = false;
  Jump(target);
  return true;
}

} // namespace ZeroNoTsukaima
