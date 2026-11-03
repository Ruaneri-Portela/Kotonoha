#include <Amagami/Source.hpp>
#include <cstdio>
#include <stdexcept>

namespace Amagami {
namespace {
uint32_t u32(const Bytes &b, size_t p) {
  if (p + 4 > b.size())
    throw std::runtime_error("Truncated archive header");
  return uint32_t(b[p]) | uint32_t(b[p + 1]) << 8 | uint32_t(b[p + 2]) << 16 |
         uint32_t(b[p + 3]) << 24;
}
} // namespace
Source::Source(const std::string &path)
    : disc_(std::make_shared<Kotonoha::DiscImage>(path)) {
  if (!disc_->find("SLPS_259.18"))
    throw std::runtime_error("Expected Amagami Japan PS2 disc (SLPS_259.18)");
  // Original pose -> body/face prefix table, SLPS_259.18 VA 0x1b7a70.
  const auto executable = disc_->read("SLPS_259.18");
  auto prefix = [&](uint32_t address) {
    if (address < 0xff000 || address - 0xff000 + 4 >= executable.size())
      throw std::runtime_error("Invalid actor asset table pointer");
    const size_t p = address - 0xff000;
    if (executable[p + 4])
      throw std::runtime_error("Invalid actor asset prefix");
    return std::string(executable.begin() + p, executable.begin() + p + 4);
  };
  for (size_t i = 0; i < 16 * 26; ++i) {
    const size_t p = 0x1b7a70 - 0xff000 + i * 8;
    actorAssets_.emplace_back(prefix(u32(executable, p)),
                              prefix(u32(executable, p + 4)));
  }
  for (const auto &name :
       {"SCENARIO.ARC", "GRAPH/GRAPH0.ARC", "GRAPH/GRAPH1.ARC",
        "GRAPH/GRAPH2.ARC", "SOUND/MUSIC.ARC", "SOUND/MUSIC.AFS",
        "SOUND/SOUND.AFS", "SOUND/VOICE.AFS"}) {
    const auto *entry = disc_->find(name);
    if (!entry)
      throw std::runtime_error(std::string("Missing disc file: ") + name);
    const bool afs = std::string(name).find(".AFS") != std::string::npos;
    auto header = disc_->read(*entry, 0, 8);
    const uint64_t n = afs ? u32(header, 4) : u32(header, 0);
    const uint64_t bytes = afs ? 8 + n * 8 : 8 + n * 20;
    if (bytes > entry->size || bytes > 32 * 1024 * 1024)
      throw std::runtime_error("Invalid archive index length");
    header = disc_->read(*entry, 0, bytes);
    archives_.emplace(name,
                      Archive{*entry, afs ? ParseAfs(header, entry->size)
                                          : ParseArc(header, entry->size)});
  }
  const auto count = archive("SCENARIO.ARC").entries.size();
  for (size_t i = 0; i < count; ++i) {
    auto s = script(i);
    if (!scenarioByName_.emplace(s.name, i).second)
      throw std::runtime_error("Duplicate scenario: " + s.name);
    scenarios_.push_back(std::move(s.name));
  }
  classes_ = ParseClassImage(Unpack(disc_->read("AMAGAMI.IMG")));
  for (const auto &entry : classes_) {
    auto s = ParseScf(entry.bytes);
    if (s.name != entry.name)
      throw std::runtime_error("Class image name mismatch");
    if (s.name == "ActorPicture")
      for (const auto &value : s.constants) {
        if (value.type != Value::Array || value.elements.size() != 16)
          continue;
        bool table = true;
        for (const auto &row : value.elements)
          if (row.type != Value::Array || row.elements.size() < 27)
            table = false;
        if (!table)
          continue;
        for (const auto &row : value.elements) {
          std::vector<SDL_FPoint> offsets;
          for (const auto &point : row.elements) {
            if (point.type != Value::Array || point.elements.size() != 2)
              throw std::runtime_error("Invalid actor origin table");
            offsets.push_back({float(int32_t(point.elements[0].bits)),
                               float(int32_t(point.elements[1].bits))});
          }
          actorOrigins_.push_back(std::move(offsets));
        }
        break;
      }
    if (s.name == "CharParam")
      for (const auto &value : s.constants) {
        if (value.type != Value::Array || value.elements.size() != 16)
          continue;
        bool table = true;
        for (const auto &row : value.elements)
          if (row.type != Value::Array || row.elements.size() != 26)
            table = false;
        if (!table)
          continue;
        for (const auto &row : value.elements) {
          std::vector<int> directions;
          for (const auto &cell : row.elements)
            directions.push_back(int32_t(cell.bits));
          breathDirections_.push_back(std::move(directions));
        }
        break;
      }
  }
  const auto &music = archive("SOUND/MUSIC.AFS");
  const auto countMusic = music.entries.size();
  auto info = disc_->read(music.file, 8 + countMusic * 8, 8);
  uint64_t namesOffset = u32(info, 0), namesLength = u32(info, 4);
  if (namesOffset > music.file.size ||
      namesLength > music.file.size - namesOffset ||
      namesLength < countMusic * 48)
    throw std::runtime_error("Invalid music names");
  auto namesMusic = disc_->read(music.file, namesOffset, countMusic * 48);
  for (size_t i = 0; i < countMusic; ++i) {
    const auto *name = namesMusic.data() + i * 48;
    if (name[0] == 'M' && name[1] >= '0' && name[1] <= '9' && name[2] >= '0' &&
        name[2] <= '9')
      music_.emplace((name[1] - '0') * 10 + name[2] - '0', i);
  }
  const auto &voice = archive("SOUND/VOICE.AFS");
  for (size_t i = 0; i < voice.entries.size(); ++i) {
    const auto &bank = voice.entries[i];
    auto h = disc_->read(voice.file, bank.offset, 8);
    uint64_t n = u32(h, 4);
    if (n > 100000 || 8 + n * 8 + 8 > bank.size)
      throw std::runtime_error("Invalid voice bank");
    h = disc_->read(voice.file, bank.offset, 8 + n * 8 + 8);
    auto entries = ParseAfs(h, bank.size);
    uint64_t names = u32(h, 8 + n * 8), length = u32(h, 12 + n * 8);
    if (names > bank.size || length > bank.size - names || n * 48 > length)
      throw std::runtime_error("Invalid AFS names");
    auto table = disc_->read(voice.file, bank.offset + names, n * 48);
    for (size_t k = 0; k < n; ++k) {
      size_t end = k * 48;
      while (end < k * 48 + 32 && table[end])
        ++end;
      std::string name(table.begin() + k * 48, table.begin() + end);
      if (name.size() >= 4 && name.substr(name.size() - 4) == ".ahx")
        voices_.emplace(
            name.substr(0, name.size() - 4),
            VoiceEntry{bank.offset + entries[k].offset, entries[k].size});
    }
  }
}
const Source::Archive &Source::archive(const std::string &name) const {
  auto it = archives_.find(name);
  if (it == archives_.end())
    throw std::out_of_range("Unknown Amagami archive: " + name);
  return it->second;
}
Bytes Source::read(const std::string &name, size_t index) const {
  const auto &a = archive(name);
  const auto &entry = a.entries.at(index);
  return disc_->read(a.file, entry.offset, entry.size);
}
Script Source::script(size_t index) const {
  return ParseScf(read("SCENARIO.ARC", index));
}
Script Source::systemClass(size_t index) const {
  return ParseScf(classes_.at(index).bytes);
}
size_t Source::scenarioIndex(const std::string &name) const {
  const auto it = scenarioByName_.find(name);
  if (it == scenarioByName_.end())
    throw std::out_of_range("Unknown Amagami scenario: " + name);
  return it->second;
}
} // namespace Amagami

namespace Amagami {
int Source::find(const std::string &archiveName,
                 const std::string &name) const {
  // K2R_CArcFile::Hash @ 0x186560; signed bytes (ASCII resource paths).
  uint32_t hash = 0;
  for (unsigned char c : name)
    hash = hash * 0x3fad + int8_t(c);
  const auto &a = archive(archiveName);
  for (size_t i = 0; i < a.entries.size(); ++i)
    if (a.entries[i].hash == hash)
      return int(i);
  return -1;
}
Bytes Source::voice(const std::string &code, unsigned number) const {
  char id[16];
  std::snprintf(id, sizeof(id), "%s%04u", code.c_str(), number);
  auto it = voices_.find(id);
  if (it == voices_.end())
    return {};
  return disc_->read(archive("SOUND/VOICE.AFS").file, it->second.offset,
                     it->second.size);
}
} // namespace Amagami

namespace Amagami {
std::pair<std::string, std::string>
Source::actorAssets(unsigned character, unsigned cloth, unsigned pose) const {
  if (character >= 16 || cloth >= 26 || pose >= 26)
    throw std::out_of_range("Invalid actor costume or pose");
  const auto &prefix = actorAssets_.at(character * 26 + pose);
  return {"stand/" + prefix.first + char('a' + cloth) + "x.tm2",
          "stand/" + prefix.second + "x.tm2"};
}
SDL_FPoint Source::actorOrigin(unsigned character, unsigned pose) const {
  if (character >= actorOrigins_.size() ||
      pose + 1 >= actorOrigins_[character].size())
    throw std::out_of_range("Invalid actor origin");
  const auto &row = actorOrigins_[character];
  return {row[0].x + row[pose + 1].x, row[0].y + row[pose + 1].y};
}
int Source::breathDirection(unsigned character, unsigned pose) const {
  return character < breathDirections_.size() &&
                 pose < breathDirections_[character].size()
             ? breathDirections_[character][pose]
             : 0;
}
int Source::musicIndex(unsigned number) const {
  auto i = music_.find(number);
  return i == music_.end() ? -1 : int(i->second);
}
} // namespace Amagami
