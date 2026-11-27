#include <Amagami/Source.hpp>
#include <Kotonoha/core/Config.hpp>
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <stdexcept>

namespace Amagami {
namespace {
uint32_t u32(const Bytes &b, size_t p) {
  if (p + 4 > b.size())
    throw std::runtime_error("Truncated archive header");
  return uint32_t(b[p]) | uint32_t(b[p + 1]) << 8 | uint32_t(b[p + 2]) << 16 |
         uint32_t(b[p + 3]) << 24;
}
float f32(const Bytes &b, size_t p) {
  const uint32_t bits = u32(b, p);
  float v = 0;
  std::memcpy(&v, &bits, 4);
  return v;
}
} // namespace
Source::Source(const std::string &path)
    : disc_(std::make_shared<Kotonoha::DiscImage>(path)) {
  if (!disc_->find("SLPS_259.18"))
    throw std::runtime_error("Expected Amagami Japan PS2 disc (SLPS_259.18)");
  // Original pose -> body/face prefix table, SLPS_259.18 VA 0x1b7a70.
  const auto executable = disc_->read("SLPS_259.18");
  auto cstr = [&](uint32_t address) {
    if (address == 0)
      return std::string{};
    if (address < 0xff000 || address - 0xff000 >= executable.size())
      throw std::runtime_error("Invalid executable string pointer");
    const size_t start = address - 0xff000;
    size_t end = start;
    while (end < executable.size() && executable[end] && end - start < 256)
      ++end;
    if (end >= executable.size() || executable[end] != 0)
      throw std::runtime_error("Unterminated executable string");
    return std::string(executable.begin() + start, executable.begin() + end);
  };
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
  // Weekday suffix table for Sprite argKind == 2, SLPS_259.18 VA 0x00289868.
  for (size_t i = 0; i < 7; ++i)
    weekdays_[i] = cstr(u32(executable, 0x00289868 - 0xff000 + i * 4));
  // Native Sprite texture descriptor table (252 entries x 36 bytes),
  // SLPS_259.18 VA 0x001b5700 .. 0x001b7a70 (including 0x001b74d0 bgsam/kaiwa).
  spriteTemplates_.reserve(252);
  for (size_t i = 0; i < 252; ++i) {
    const size_t p = 0x001b5700 - 0xff000 + i * 36;
    SpriteTemplate tpl;
    tpl.format = cstr(u32(executable, p));
    tpl.argKind = u32(executable, p + 4);
    tpl.rect = {f32(executable, p + 8), f32(executable, p + 12),
                f32(executable, p + 16), f32(executable, p + 20)};
    tpl.flags = u32(executable, p + 24);
    tpl.archiveHint = int32_t(u32(executable, p + 28));
    tpl.extra = int32_t(u32(executable, p + 32));
    spriteTemplates_.push_back(std::move(tpl));
  }
  // Classic BGM table (VA 0x0028e710, 41 entries) and Arrange BGM table
  // (VA 0x0028e7b4, 41 entries), plus Ambient SOUND.AFS table (VA 0x0028e6d0).
  classicMusic_.reserve(41);
  arrangeMusic_.reserve(41);
  for (size_t i = 0; i < 41; ++i) {
    classicMusic_.push_back(
        int32_t(u32(executable, 0x0028e710 - 0xff000 + i * 4)));
    arrangeMusic_.push_back(
        int32_t(u32(executable, 0x0028e7b4 - 0xff000 + i * 4)));
  }
  for (size_t i = 0; i < 8; ++i)
    ambienceTable_.push_back(
        int32_t(u32(executable, 0x0028e6d0 - 0xff000 + i * 4)));
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
    Archive parsed{*entry, afs ? ParseAfs(header, entry->size)
                               : ParseArc(header, entry->size)};
    if (!afs)
      for (size_t i = 0; i < parsed.entries.size(); ++i)
        parsed.byHash.emplace(parsed.entries[i].hash, i);
    archives_.emplace(name, std::move(parsed));
  }
  // Native builds retain the scenario archive to avoid repeated disk/SMB
  // seeks while indexing the 2,308 scenes. In WASM the disc can be remote
  // (HTTP Range): eagerly caching hundreds of MiB duplicates the browser's
  // bounded Fetch cache and can exhaust WebAssembly memory before the title.
  // Read one scene at a time instead; the shared HTTP provider caches pages.
  const auto &scenario = archive("SCENARIO.ARC");
  if (!Kotonoha::isBrowserPlatform() && !Kotonoha::isMobilePlatform()) {
    if (scenario.file.size > 256 * 1024 * 1024)
      throw std::runtime_error("Scenario archive exceeds memory limit");
    scenarioArchive_ = disc_->read(scenario.file, 0, scenario.file.size);
  }
  const auto count = scenario.entries.size();
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
  // SOUND/MUSIC.ARC contains systemBank.bd (entry 0) and systemBank.hd (entry
  // 1, Sony IECS header with Vagi sample table at +0x50 for 49 waves 0..48).
  const int bdIdx = find("SOUND/MUSIC.ARC", "systemBank.bd");
  const int hdIdx = find("SOUND/MUSIC.ARC", "systemBank.hd");
  if (bdIdx >= 0 && hdIdx >= 0) {
    const auto bd = read("SOUND/MUSIC.ARC", size_t(bdIdx));
    const auto hd = read("SOUND/MUSIC.ARC", size_t(hdIdx));
    constexpr size_t kVagiOffset = 0x50;
    if (hd.size() >= kVagiOffset + 16 &&
        std::memcmp(hd.data() + kVagiOffset, "IECSigaV", 8) == 0) {
      const int32_t maxIndex = int32_t(u32(hd, kVagiOffset + 12));
      if (maxIndex >= 0 && maxIndex < 1024 &&
          kVagiOffset + 16 + size_t(maxIndex + 1) * 4 <= hd.size()) {
        const size_t count = size_t(maxIndex + 1);
        std::vector<uint32_t> recOffsets(count);
        for (size_t i = 0; i < count; ++i)
          recOffsets[i] = u32(hd, kVagiOffset + 16 + i * 4);
        soundEffects_.resize(count);
        for (size_t i = 0; i < count; ++i) {
          const size_t recPos = kVagiOffset + recOffsets[i];
          if (recPos + 8 > hd.size())
            throw std::runtime_error("Truncated systemBank.hd Vagi entry");
          const uint32_t bdStart = u32(hd, recPos);
          const uint16_t rate =
              uint16_t(hd[recPos + 4]) | (uint16_t(hd[recPos + 5]) << 8);
          const uint32_t bdEnd =
              (i + 1 < count &&
               kVagiOffset + recOffsets[i + 1] + 4 <= hd.size())
                  ? u32(hd, kVagiOffset + recOffsets[i + 1])
                  : uint32_t(bd.size());
          if (bdStart > bd.size() || bdEnd > bd.size() || bdEnd < bdStart)
            throw std::runtime_error("Invalid systemBank.bd wave bounds");
          soundEffects_[i].adpcm.assign(bd.begin() + bdStart,
                                        bd.begin() + bdEnd);
          soundEffects_[i].sampleRate = rate > 0 ? int(rate) : 39000;
          soundEffects_[i].baseNote = hd[recPos + 6];
          soundEffects_[i].pan = hd[recPos + 7];
        }
      }
    }
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
    uint64_t names = u32(h, static_cast<size_t>(8 + n * 8)),
                                                     length = u32(h, static_cast<size_t>(12 + n * 8));
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
  if (name == "SCENARIO.ARC" && !scenarioArchive_.empty()) {
    if (entry.offset > scenarioArchive_.size() ||
        entry.size > scenarioArchive_.size() - entry.offset)
      throw std::runtime_error("Scenario outside cached archive");
    return Bytes(scenarioArchive_.begin() + entry.offset,
                 scenarioArchive_.begin() + entry.offset + entry.size);
  }
  // WASM streams only the requested SCF entry through ISO sectors and HTTP
  // Range. Never allocate the entire SCENARIO.ARC in WebAssembly linear memory.
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
  const auto found = a.byHash.find(hash);
  return found == a.byHash.end() ? -1 : int(found->second);
}
Bytes Source::ambience(unsigned index) const {
  const auto &a = archive("SOUND/SOUND.AFS");
  if (index >= a.entries.size())
    throw std::out_of_range("Ambient sound index outside SOUND.AFS");
  return read("SOUND/SOUND.AFS", index);
}
std::vector<std::string> Source::voiceAssetIds() const {
  std::vector<std::string> result;
  result.reserve(voices_.size());
  for (const auto &[key, entry] : voices_) {
    // Source::voice() resolves "<speaker-code><four-digit-number>".
    // Only expose keys that have that canonical shape.
    if (key.size() <= 4)
      continue;
    const auto number = key.substr(key.size() - 4);
    if (!std::all_of(number.begin(), number.end(),
                     [](unsigned char ch) { return ch >= '0' && ch <= '9'; }))
      continue;
    result.push_back("voice/" + key.substr(0, key.size() - 4) + "/" +
                     std::to_string(std::stoul(number)));
  }
  return result;
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
std::optional<Source::GraphicLocation>
Source::graphic(const std::string &name) const {
  if (auto cached = graphicLocations_.find(name);
      cached != graphicLocations_.end())
    return cached->second;
  for (const auto *archive :
       {"GRAPH/GRAPH0.ARC", "GRAPH/GRAPH1.ARC", "GRAPH/GRAPH2.ARC"}) {
    const int index = find(archive, name);
    if (index >= 0) {
      GraphicLocation location{archive, size_t(index)};
      graphicLocations_.emplace(name, location);
      return location;
    }
  }
  graphicLocations_.emplace(name, std::nullopt);
  return std::nullopt;
}
std::string Source::itemPicture(unsigned number) const {
  if (number > 999)
    throw std::out_of_range("Invalid item picture selector");
  // Native Sprite texture type 9; format string at SLPS_259.18 VA 0x28ce60.
  char name[32];
  std::snprintf(name, sizeof(name), "item/%3u.tm2", number);
  if (!graphic(name))
    throw std::runtime_error(std::string("Item picture not found: ") + name);
  return name;
}
std::string Source::eventPicture(unsigned character, unsigned number,
                                 unsigned variant) const {
  if (character >= 16 || number > 99 || variant >= 26)
    throw std::out_of_range("Invalid event picture selector");
  // EventPicture.initialize, SLPS_259.18 format string at VA 0x28e4d8.
  char name[64];
  std::snprintf(name, sizeof(name), "epi/e%1x_%02u%c0.tm2", character, number,
                'a' + variant);
  if (!graphic(name))
    throw std::runtime_error(std::string("Event picture not found: ") + name);
  return name;
}
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
int Source::musicIndex(unsigned number, bool classic) const {
  // In SLPS_259.18, Music.setup passes track (1..40) when Music.classic is
  // true, and track + 40 (41..80) when Music.classic is false, indexing
  // VA 0x0028e710 (Classic, 41 entries 0..40) followed immediately by
  // VA 0x0028e7b4 (Arrange, 41 entries 41..81).
  if (number >= 41 && number <= 80 && number - 41 < arrangeMusic_.size())
    return arrangeMusic_[number - 41];
  if (classic) {
    if (number < classicMusic_.size())
      return classicMusic_[number];
    return -1;
  }
  if (number == 0 && !classicMusic_.empty())
    return classicMusic_[0];
  if (number >= 1 && number <= 40 && number - 1 < arrangeMusic_.size())
    return arrangeMusic_[number - 1];
  auto i = music_.find(number);
  return i == music_.end() ? -1 : int(i->second);
}
int Source::ambienceIndex(unsigned id) const {
  if (id < ambienceTable_.size())
    return ambienceTable_[id];
  return -1;
}
Source::SoundEffect Source::soundEffect(unsigned index) const {
  // Support direct Vagi wave indices 0..48 as well as MIDI note IDs 12..60
  // (where Smpl[i] has base note 12 + i). If index is 49..60, map to index
  // - 12.
  const size_t resolved =
      index < soundEffects_.size()
          ? size_t(index)
          : (index >= 12 && index - 12 < soundEffects_.size()
                 ? size_t(index - 12)
                 : soundEffects_.size());
  if (resolved >= soundEffects_.size())
    throw std::out_of_range("Sound effect index outside systemBank");
  return soundEffects_[resolved];
}
std::optional<Source::SpriteDescriptor>
Source::spriteDescriptor(unsigned texId, unsigned variant) const {
  if (texId >= spriteTemplates_.size())
    return std::nullopt;
  const auto &tpl = spriteTemplates_[texId];
  SpriteDescriptor desc;
  desc.rect = tpl.rect;
  desc.flags = tpl.flags;
  desc.archiveHint = tpl.archiveHint;
  desc.extra = tpl.extra;
  if (!tpl.format.empty()) {
    char buf[128];
    const bool hasExt = tpl.format.size() >= 4 &&
                        tpl.format.substr(tpl.format.size() - 4) == ".tm2";
    const std::string fmtWithExt = hasExt ? tpl.format : (tpl.format + ".tm2");
    if (tpl.argKind == 1) {
      std::snprintf(buf, sizeof(buf), fmtWithExt.c_str(), variant);
      desc.asset = buf;
    } else if (tpl.argKind == 2) {
      std::snprintf(buf, sizeof(buf), fmtWithExt.c_str(),
                    weekdays_[variant % 7].c_str());
      desc.asset = buf;
    } else {
      desc.asset = fmtWithExt;
    }
  }
  return desc;
}
} // namespace Amagami
