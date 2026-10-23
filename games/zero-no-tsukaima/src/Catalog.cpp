#include <ZeroNoTsukaima/Catalog.hpp>
#include <ZeroNoTsukaima/Scene.hpp>

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <set>
#include <sstream>

namespace ZeroNoTsukaima {

namespace {

std::vector<std::string> Tokens(const std::vector<uint8_t> &data) {
  std::vector<std::string> out;
  std::string cur;
  for (uint8_t c : data) {
    if (c < 0x20) {
      if (cur.size() >= 2) out.push_back(ShiftJisToUtf8(cur));
      cur.clear();
    } else {
      cur += char(c);
    }
  }
  return out;
}

std::string Lower(std::string s) {
  for (char &c : s) c = char(std::tolower((unsigned char)c));
  return s;
}

// "cg05a" -> (false, 5, 'a'); "ecg48" -> (true, 48, 0)
bool ParseCg(const std::string &name, bool *ecg, int *num, char *letter) {
  size_t i = 0;
  *ecg = false;
  if (name.compare(0, 3, "ecg") == 0) {
    *ecg = true;
    i = 3;
  } else if (name.compare(0, 2, "cg") == 0) {
    i = 2;
  } else {
    return false;
  }
  if (i >= name.size() || !std::isdigit((unsigned char)name[i])) return false;
  size_t j = i;
  while (j < name.size() && std::isdigit((unsigned char)name[j])) ++j;
  *num = std::atoi(name.substr(i, j - i).c_str());
  *letter = j < name.size() ? name[j] : 0;
  return j >= name.size() - 1;
}

int TimeOffset(const std::string &word) {
  if (word == "昼") return 0;
  if (word == "夕" || word == "夕室内") return 1;
  if (word == "夜" || word == "夜室内" || word == "夜闇") return 2;
  return -1;
}

} // namespace

bool ImageCatalog::Build(const std::vector<uint8_t> &systemEnv, const std::vector<uint8_t> &systemMain,
                         const std::vector<uint32_t> &pictureSizes) {
  stages_.clear();
  specialStages_.clear();
  events_.clear();

  // Stage table: "stages" followed by (display name, file base) pairs; a base ending in TIME has
  // three pictures (day, evening, night). Pictures are stored sorted by base name, BG before BGN.
  const auto env = Tokens(systemEnv);
  auto begin = std::find(env.begin(), env.end(), "stages");
  if (begin == env.end()) return false;
  std::map<std::string, std::pair<std::string, bool>> byBase; // base -> (stage name, timed)
  for (auto it = begin + 1; it + 1 < env.end() && *it != "charDispTrans"; it += 2) {
    std::string base = *(it + 1);
    const bool timed = base.size() > 4 && base.compare(base.size() - 4, 4, "TIME") == 0;
    if (timed) base.resize(base.size() - 4);
    if (base.compare(0, 2, "BG") != 0) {
      specialStages_[*it] = base; // black, white, MOERU, ECG09D ...
      continue;
    }
    byBase[base] = {*it, timed};
  }
  int next = 0;
  for (const auto &[base, info] : byBase) {
    stages_[info.first] = {next, info.second};
    next += info.second ? 3 : 1;
  }

  // Event CGs follow the backgrounds, ordered by number; the gallery list omits a few variants.
  const auto main = Tokens(systemMain);
  auto list = std::find(main.begin(), main.end(), "cglist");
  if (list == main.end()) {
    BuildCharacters(systemEnv, pictureSizes, next);
    return next > 0;
  }
  std::vector<std::string> names;
  for (auto it = list + 1; it != main.end(); ++it) {
    const std::string l = Lower(*it);
    bool ecg;
    int num;
    char letter;
    if (l.compare(0, 3, "th_") == 0) continue;
    if (!ParseCg(l, &ecg, &num, &letter)) break;
    names.push_back(l);
  }
  for (const char *extra : {"cg03b", "cg05c", "cg69b", "cg76b", "cg85c", "ecg39d"}) names.push_back(extra);
  auto key = [](const std::string &n) {
    bool ecg;
    int num;
    char letter;
    ParseCg(n, &ecg, &num, &letter);
    return std::make_tuple(ecg, num, letter);
  };
  std::sort(names.begin(), names.end(), [&](const auto &a, const auto &b) { return key(a) < key(b); });
  names.erase(std::unique(names.begin(), names.end()), names.end());
  for (const std::string &n : names) events_[n] = next++;
  BuildCharacters(systemEnv, pictureSizes, next);
  return true;
}

void ImageCatalog::BuildCharacters(const std::vector<uint8_t> &systemEnv, const std::vector<uint32_t> &sizes,
                                   int first) {
  characters_ = {};
  spriteGroups_.clear();
  const auto env = EvalNutTable(systemEnv);
  if (!env) return;
  auto text = [](const NutValue *v) { return v ? v->Text() : std::string(); };
  auto names = [&](const NutValue *t, std::map<std::string, std::string> *out) {
    if (t && t->kind == NutValue::Table)
      for (const auto &[k, v] : t->fields) (*out)[k] = text(v.get());
  };

  nameImages_.clear();
  names(env->Get("nameImageMap"), &nameImages_);

  std::set<std::string> bases;
  if (const NutValue *chars = env->Get("characters"); chars && chars->kind == NutValue::Table) {
    for (const auto &[name, c] : chars->fields) {
      if (!c || c->kind != NutValue::Table) continue;
      CharacterInfo info;
      info.defaultPose = text(c->Get("defaultPose"));
      if (const NutValue *poses = c->Get("poses"); poses && poses->kind == NutValue::Table) {
        for (const auto &[poseName, p] : poses->fields) {
          if (!p || p->kind != NutValue::Table) continue;
          CharacterPose pose;
          const std::string image = text(p->Get("image"));
          pose.prefix = image.substr(0, image.find('_'));
          names(p->Get("dresses"), &pose.dresses);
          names(p->Get("faces"), &pose.faces);
          pose.defaultDress = text(p->Get("defaultDress"));
          pose.defaultFace = text(p->Get("defaultFace"));
          for (const auto &[d, code] : pose.dresses) bases.insert(pose.prefix + "_" + code);
          info.poses[poseName] = std::move(pose);
        }
      }
      characters_.characters[name] = std::move(info);
    }
  }
  if (const NutValue *pos = env->Get("positions"); pos && pos->kind == NutValue::Table) {
    for (const auto &[word, p] : pos->fields) {
      if (!p || p->kind != NutValue::Table) continue;
      CharacterPosition cp;
      if (const NutValue *x = p->Get("xpos")) {
        cp.hasX = true;
        const std::string s = x->Text();
        cp.relative = x->kind == NutValue::String && !s.empty() && (s[0] == '+' || s[0] == '-');
        cp.xpos = std::atoi(s.c_str() + (s[0] == '+' ? 1 : 0));
      }
      if (const NutValue *d = p->Get("disp")) cp.disp = d->value;
      characters_.positions[word] = cp;
    }
  }

  // Sprites follow the event CGs: per base name ("TCG01_1D") an empty entry, then one picture per
  // face code 1..n (n is 7, or 6 / 5 for characters without the last faces). The mob characters
  // TCG15..TCG18 are defined in ENVINIT but have no pictures on the PS2 disc.
  int i = first;
  const int count = int(sizes.size());
  for (const std::string &base : bases) {
    const int number = std::atoi(base.c_str() + 3);
    if (number >= 15 && number <= 18) continue;
    // Next group: an empty entry followed by at least one picture.
    while (i < count && !(sizes[size_t(i)] == 0 && i + 1 < count && sizes[size_t(i) + 1] != 0)) ++i;
    if (i >= count) break;
    int n = 0;
    while (i + 1 + n < count && sizes[size_t(i + 1 + n)] != 0) ++n;
    spriteGroups_[base] = {i, n};
    i += 1 + n;
  }
}

int ImageCatalog::ResolveSprite(const std::string &image) const {
  // "TCG01_1D3": base "TCG01_1D", face 3.
  size_t end = image.size();
  while (end > 0 && std::isdigit((unsigned char)image[end - 1])) --end;
  if (end == image.size()) return -1;
  auto it = spriteGroups_.find(image.substr(0, end));
  if (it == spriteGroups_.end()) return -1;
  const int face = std::atoi(image.c_str() + end);
  const auto [start, n] = it->second;
  return start + (face >= 1 && face <= n ? face : 1);
}

int ImageCatalog::ResolveStage(const std::string &stage) const {
  std::istringstream words(stage);
  std::string w;
  int offset = 0;
  const Stage *found = nullptr;
  while (words >> w) {
    const int t = TimeOffset(w);
    if (t >= 0) offset = t;
    else if (auto it = stages_.find(w); it != stages_.end()) found = &it->second;
  }
  if (!found) return -1;
  return found->first + (found->timed ? offset : 0);
}

std::set<std::string> ImageCatalog::StageNames() const {
  std::set<std::string> out;
  for (const auto &[name, stage] : stages_) out.insert(name);
  for (const auto &[name, image] : specialStages_) out.insert(name);
  return out;
}

const std::string *ImageCatalog::NameImage(const std::string &speaker) const {
  auto it = nameImages_.find(speaker);
  return it == nameImages_.end() ? nullptr : &it->second;
}

std::string ImageCatalog::SpecialStage(const std::string &stage) const {
  std::istringstream words(stage);
  std::string w;
  while (words >> w)
    if (auto it = specialStages_.find(w); it != specialStages_.end()) return it->second;
  return {};
}

int ImageCatalog::ResolveEvent(const std::string &name) const {
  const std::string l = Lower(name);
  if (auto it = events_.find(l); it != events_.end()) return it->second;
  if (auto it = events_.find(l + "a"); it != events_.end()) return it->second;
  return -1;
}

} // namespace ZeroNoTsukaima
