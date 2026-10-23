#include <ZeroNoTsukaima/Lz.hpp>
#include <ZeroNoTsukaima/Scene.hpp>
#include <ZeroNoTsukaima/Source.hpp>

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <sstream>
#include <tuple>

namespace ZeroNoTsukaima {

bool SceneSource::ReadEntry(const Disc::Entry &bin, const std::vector<ArchiveEntry> &index, size_t i,
                            std::vector<uint8_t> *out) const {
  if (i >= index.size()) return false;
  std::vector<uint8_t> blob;
  if (!disc_.ReadRange(bin, index[i].offset, index[i].size, &blob)) return false;
  std::vector<uint8_t> unpacked;
  if (Unpack(blob.data(), blob.size(), &unpacked)) *out = std::move(unpacked);
  else *out = std::move(blob);
  return true;
}

bool SceneSource::Open(const std::string &discPath, std::string *error) {
  if (!disc_.Open(discPath, error)) return false;
  const Disc::Entry *sysBin = disc_.Find("SYSTEM.BIN");
  const Disc::Entry *sysHd = disc_.Find("SYSTEM.HD");
  sceneBin_ = disc_.Find("SCENE_ID.BIN");
  const Disc::Entry *sceneHd = disc_.Find("SCENE_ID.HD");
  if (!sysBin || !sysHd || !sceneBin_ || !sceneHd) {
    if (error) *error = "SYSTEM / SCENE_ID archives not found on the disc";
    return false;
  }
  std::vector<uint8_t> hd;
  disc_.Read(*sceneHd, &hd);
  sceneIndex_ = ParseArchiveIndex(hd);
  pictureBin_ = disc_.Find("SCENEDAT.BIN");
  if (const Disc::Entry *h = disc_.Find("SCENEDAT.HD")) {
    std::vector<uint8_t> raw;
    if (pictureBin_ && disc_.Read(*h, &raw)) pictureIndex_ = ParseArchiveIndex(raw);
  }
  voiceBin_ = disc_.Find("VOICE_ID.BIN");
  soundBin_ = disc_.Find("SOUND_ID.BIN");
  for (auto [bin, name, index] : {std::tuple{voiceBin_, "VOICE_ID.HD", &voiceIndex_},
                                  std::tuple{soundBin_, "SOUND_ID.HD", &soundIndex_}}) {
    const Disc::Entry *h = disc_.Find(name);
    std::vector<uint8_t> raw;
    if (bin && h && disc_.Read(*h, &raw)) *index = ParseArchiveIndex(raw);
  }
  hd.clear();
  disc_.Read(*sysHd, &hd);
  const auto sysIndex = ParseArchiveIndex(hd);
  std::vector<uint8_t> nut;
  if (!ReadEntry(*sysBin, sysIndex, 32, &nut) || !ParseSceneNames(nut, &names_)) {
    if (error) *error = "cannot read the scene name table (SYSTEM #32)";
    return false;
  }
  std::vector<uint8_t> env, main;
  if (ReadEntry(*sysBin, sysIndex, 31, &env) && ReadEntry(*sysBin, sysIndex, 35, &main)) {
    std::vector<uint32_t> sizes;
    for (const ArchiveEntry &e : pictureIndex_) sizes.push_back(e.size);
    catalog_.Build(env, main, sizes);
  }
  normalBin_ = disc_.Find("NORMAL.BIN");
  if (const Disc::Entry *h = disc_.Find("NORMAL.HD")) {
    std::vector<uint8_t> raw;
    if (normalBin_ && disc_.Read(*h, &raw)) normalIndex_ = ParseArchiveIndex(raw);
  }
  std::vector<uint8_t> list;
  if (normalBin_) {
    for (auto [entry, table] : {std::pair{9, &scenedatNames_}, std::pair{10, &soundNames_}, std::pair{11, &normalNames_}})
      if (ReadEntry(*normalBin_, normalIndex_, size_t(entry), &list)) *table = ParseNameList(list);
    if (ReadEntry(*normalBin_, normalIndex_, 247, &list))
      for (size_t o = 0; o + 14 <= list.size(); o += 14) {
        std::string name(reinterpret_cast<const char *>(list.data() + o), 14);
        name.resize(name.find('\0') == std::string::npos ? name.size() : name.find('\0'));
        if (name.empty()) break;
        movies_.push_back(name);
      }
  }
  // NORMAL #8 is SCENE_ID.LST ("[SCENE_ID]" then "SCENE\\NAME.nut" per entry): the real
  // entry order. The SCENES.NUT literal pool read above is only hash ordered.
  if (normalBin_ && ReadEntry(*normalBin_, normalIndex_, 8, &list)) {
    std::vector<std::string> names;
    std::string text(list.begin(), list.end()), line;
    std::istringstream in(text);
    while (std::getline(in, line)) {
      while (!line.empty() && (line.back() == '\r' || line.back() == '\0')) line.pop_back();
      if (line.empty() || line[0] == '[') continue;
      const size_t slash = line.find_last_of("\\/");
      std::string name = slash == std::string::npos ? line : line.substr(slash + 1);
      const size_t dot = name.rfind('.');
      if (dot != std::string::npos) name = name.substr(0, dot);
      names.push_back(name + ".KS");
    }
    if (names.size() == sceneIndex_.size()) names_ = std::move(names);
    else std::fprintf(stderr, "[zero] SCENE_ID.LST has %zu names for %zu entries; keeping SCENES.NUT\n", names.size(), sceneIndex_.size());
  }
  if (ReadEntry(*sysBin, sysIndex, 36, &list))
    if (auto table = EvalNutTable(list))
      for (const auto &[k, v] : table->fields)
        if (v && v->kind == NutValue::Integer) systemVoices_[k] = v->value;
  return true;
}

std::map<std::string, int> ParseNameList(const std::vector<uint8_t> &text) {
  std::map<std::string, int> out;
  int index = 0;
  size_t start = 0;
  while (start < text.size()) {
    size_t end = start;
    while (end < text.size() && text[end] != '\n') ++end;
    std::string line(text.begin() + long(start), text.begin() + long(end));
    start = end + 1;
    while (!line.empty() && (line.back() == '\r' || line.back() == ' ' || line.back() == '\t')) line.pop_back();
    if (line.empty() || line.compare(0, 2, "//") == 0 || line[0] == '[') continue;
    std::string key = line.substr(line.find_last_of("\\/") == std::string::npos ? 0 : line.find_last_of("\\/") + 1);
    key = key.substr(0, key.find('.'));
    for (char &c : key) c = char(std::toupper(static_cast<unsigned char>(c)));
    out.emplace(key, index++);
  }
  return out;
}

int SceneSource::Lookup(const std::map<std::string, int> &table, const std::string &name) {
  std::string key = name;
  for (char &c : key) c = char(std::toupper(static_cast<unsigned char>(c)));
  const auto it = table.find(key);
  return it == table.end() ? -1 : it->second;
}

bool SceneSource::LoadNormalPicture(int index, Image *out) const {
  if (!normalBin_ || index < 0) return false;
  std::vector<uint8_t> data;
  if (!ReadEntry(*normalBin_, normalIndex_, size_t(index), &data)) return false;
  return DecodeTim2(data.data(), data.size(), out);
}

bool SceneSource::LoadMovie(int number, std::vector<uint8_t> *out) const {
  if (number < 0 || size_t(number) >= movies_.size()) return false;
  const Disc::Entry *e = disc_.Find("MOVIE/" + movies_[size_t(number)]);
  return e != nullptr && disc_.Read(*e, out);
}

bool SceneSource::LoadPicture(int index, Image *out) const {
  if (!pictureBin_ || index < 0) return false;
  std::vector<uint8_t> data;
  if (!ReadEntry(*pictureBin_, pictureIndex_, size_t(index), &data)) return false;
  return DecodeTim2(data.data(), data.size(), out);
}

bool SceneSource::LoadScene(size_t index, std::vector<uint8_t> *out) const {
  return sceneBin_ != nullptr && ReadEntry(*sceneBin_, sceneIndex_, index, out);
}

bool SceneSource::LoadVoice(size_t index, std::vector<uint8_t> *out) const {
  if (!voiceBin_ || index >= voiceIndex_.size()) return false;
  return disc_.ReadRange(*voiceBin_, voiceIndex_[index].offset, voiceIndex_[index].size, out);
}

bool SceneSource::LoadSound(size_t index, std::vector<uint8_t> *out) const {
  if (!soundBin_ || index >= soundIndex_.size()) return false;
  return disc_.ReadRange(*soundBin_, soundIndex_[index].offset, soundIndex_[index].size, out);
}

} // namespace ZeroNoTsukaima
