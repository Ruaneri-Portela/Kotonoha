#include <ZeroNoTsukaima/Game.hpp>

#include <algorithm>
#include <cctype>

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
  else if (n == "p") { if (!text_.empty()) waiting_ = true; }
  else if (n == "er") text_.clear();
  else if (n == "msgon") msgOn_ = true;
  else if (n == "msgoff") { msgOn_ = false; text_.clear(); speaker_.clear(); }
  else if (n == "dispname") {
    speaker_ = attr("name");
    const std::string v = attr("voice");
    voice_ = v.empty() ? 0 : std::atoi(v.c_str());
  } else if (n == "eval") Assign(attr("exp"));
  else if (n == "set") {
    flags_[attr("name")] = Eval(attr("value"));
  } else if (n == "goto") {
    if (const std::string *s = tag.Get("storage")) { LoadByName(*s); if (tag.Get("target")) Jump(attr("target")); }
    else Jump(attr("target"));
  } else if (n == "next" || n == "coffee") {
    if (!LoadByName(attr("storage"))) finished_ = true;
  } else if (n == "seladd") {
    choiceText_.push_back(attr("text"));
    choiceTarget_.push_back(attr("target"));
  } else if (n == "select") {
    if (!choiceText_.empty()) choosing_ = true;
  } else if (n == "stage") {
    stage_ = attr("stage");
    if (stage_.empty())
      for (const auto &[k, v] : tag.attrs)
        if (k != "tagname") stage_ += (stage_.empty() ? "" : " ") + k;
    event_.clear();
    ++version_;
  } else if (n == "ev") {
    event_ = attr("file");
    ++version_;
  } else if (n == "bgm") {
    bgm_.clear();
    for (const auto &[k, v] : tag.attrs)
      if (k != "tagname" && k != "stop") bgm_ = k;
    ++version_;
  } else if (n == "allchar" || n == "hidecharacters") {
    if (tag.Get("reset")) characters_.clear();
    else if (tag.Get("hide") || n == "hidecharacters")
      for (CharacterState &c : characters_) c.visible = false;
    ++version_;
  } else if (ExecuteCharacter(tag)) {
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
  bool show = false;
  for (const auto &[k, v] : tag.attrs) {
    if (pose != info->second.poses.end()) {
      if (pose->second.dresses.count(k)) c.dress = k;
      if (pose->second.faces.count(k)) c.face = k;
    }
    auto p = table_->positions.find(k);
    if (p == table_->positions.end()) continue;
    if (p->second.hasX) c.xpos = p->second.relative ? c.xpos + p->second.xpos : p->second.xpos;
    if (p->second.disp == 1 || p->second.disp == 2) show = true;
    else if (p->second.disp != 0) c.visible = false;
  }
  if (show) {
    // A character that (re)appears is drawn in front of the others.
    c.visible = true;
    CharacterState moved = c;
    characters_.erase(it);
    characters_.push_back(moved);
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

void Game::Pump() {
  for (int guard = 0; guard < 200000 && !waiting_ && !choosing_ && !finished_; ++guard) {
    if (pc_ >= tags_.size()) {
      if (sceneIndex_ == 0 || !LoadScene(sceneIndex_ - 1, "")) finished_ = true;
      continue;
    }
    Execute(tags_[pc_++]);
  }
}

bool Game::Click() {
  if (!waiting_) return false;
  waiting_ = false;
  return true;
}

bool Game::Choose(size_t index) {
  if (!choosing_ || index >= choiceTarget_.size()) return false;
  const std::string target = choiceTarget_[index];
  choiceText_.clear();
  choiceTarget_.clear();
  choosing_ = false;
  Jump(target);
  return true;
}

} // namespace ZeroNoTsukaima
