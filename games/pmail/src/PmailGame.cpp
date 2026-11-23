#include "Pmail/Port.hpp"
#include "PmailGalleryData.hpp"
#include "PmailPack.hpp"
#include <Pmail/StateIO.hpp>

#include <Kotonoha/localization/Encoding.hpp>
#include <SDL3/SDL.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cstring>
#include <iostream>
#include <string_view>

namespace Kotonoha::PmailPort {

namespace {
std::string lower(std::string s) {
  std::transform(s.begin(), s.end(), s.begin(),
                 [](unsigned char c) { return char(std::tolower(c)); });
  return s;
}

// pmail.exe:0x44f394. SYS 215 sets a 1 in the global flag array
// 0x7933c0[0xc00 + index]; these are the 32 retail extra/gallery entries.
constexpr std::array<std::string_view, 32> kExtraNames{
    "etc00",  "hiro00", "hiro01", "hiro02", "hiro03", "hiro04", "hiro05",
    "hiro06", "hiro07", "hiro08", "mika00", "mika01", "mika02", "mika03",
    "mika04", "mika05", "mika06", "saku00", "saku01", "yuzu00", "yuzu01",
    "tohk00", "tohk01", "tohk02", "tohk03", "nana00", "rinn00", "rinn01",
    "rinn02", "rinn03", "rinn04", "yuzu02"};

bool isExtraName(const std::string &name) {
  const std::string_view view(name);
  return std::any_of(kExtraNames.begin(), kExtraNames.end(),
                     [&](std::string_view entry) { return entry == view; });
}

bool isMagicAt(const std::vector<uint8_t> &d, size_t i, const char *m,
               size_t n) {
  return i + n <= d.size() && std::memcmp(d.data() + i, m, n) == 0;
}
} // namespace

PmailGame::PmailGame(std::vector<uint8_t> scriptPack, std::string packPath)
    : pack_(std::move(scriptPack)), packPath_(std::move(packPath)), vm_(*this) {
  std::lock_guard lock(mutex_);
  if (!packPath_.empty()) {
    packReader_ = std::make_unique<PmailPack>();
    if (!packReader_->openBytes(pack_, packPath_)) {
      std::cerr
          << "[PmailPort] PmailPack index unavailable, falling back to scan: "
          << packPath_ << std::endl;
      packReader_.reset();
    }
  }
  indexPack();
}

PmailGame::~PmailGame() = default;

void PmailGame::setPlayerNames(std::string no, std::string ho) {
  std::lock_guard lock(mutex_);
  auto valid = [](const std::string &s) {
    return !s.empty() && s.size() <= 64 &&
           s.find_first_of("\r\n\t") == std::string::npos;
  };
  if (!valid(no) || !valid(ho))
    throw std::invalid_argument(
        "Pure Mail names must be 1-64 bytes without controls");
  playerNameNO_ = std::move(no);
  playerNameHO_ = std::move(ho);
}

std::string PmailGame::expandPlayerNames(const std::string &source) const {
  std::lock_guard lock(mutex_);
  std::string result;
  result.reserve(source.size());
  for (size_t i = 0; i < source.size();) {
    if (source.compare(i, 3, "$NO") == 0) {
      result += playerNameNO_;
      i += 3;
    } else if (source.compare(i, 3, "$HO") == 0) {
      result += playerNameHO_;
      i += 3;
    } else {
      result += source[i++];
    }
  }
  return result;
}

void PmailGame::clearComputerOverlay() {
  computerText_.clear();
  chatHistory_.clear();
  computerClock_.clear();
  chatPanel_ = {};
  mailPanel_ = {};
  computerIcons_ = {};
  computerOverlayVisible_ = false;
}

void PmailGame::indexPack() {
  std::lock_guard lock(mutex_);
  entries_.clear();
  struct Hit {
    size_t off;
    size_t mlen;
  };
  std::vector<Hit> hits;
  for (size_t i = 0; i + 4 <= pack_.size(); ++i) {
    if (isMagicAt(pack_, i, "kPM7\0", 5))
      hits.push_back({i, 5});
    else if (isMagicAt(pack_, i, "pm7\0", 4))
      hits.push_back({i, 4});
    else if (isMagicAt(pack_, i, "PM7\0", 4))
      hits.push_back({i, 4});
  }
  std::sort(hits.begin(), hits.end(),
            [](const Hit &a, const Hit &b) { return a.off < b.off; });
  // Deduplicate overlapping magics (e.g. kPM7 contains PM7).
  std::vector<Hit> uniq;
  for (const auto &h : hits) {
    if (!uniq.empty() && h.off < uniq.back().off + uniq.back().mlen)
      continue;
    uniq.push_back(h);
  }
  for (size_t i = 0; i < uniq.size(); ++i) {
    size_t off = uniq[i].off;
    size_t next = (i + 1 < uniq.size()) ? uniq[i + 1].off : pack_.size();
    size_t p = off + uniq[i].mlen;
    while (p < next && p < pack_.size() && pack_[p] == 0)
      ++p;
    size_t e = p;
    while (e < next && e < pack_.size() && pack_[e] != 0 && pack_[e] >= 0x20 &&
           pack_[e] < 0x7f)
      ++e;
    std::string name;
    if (e > p)
      name.assign(reinterpret_cast<const char *>(pack_.data() + p), e - p);
    if (name.empty())
      continue;
    Entry ent;
    ent.name = name;
    ent.offset = off;
    ent.size = (next > off) ? (next - off) : 0;
    entries_.push_back(std::move(ent));
  }
  std::cout << "[PmailPort] Indexed " << entries_.size()
            << " script objects from script.dat" << std::endl;
}

::Pmail::ObjectPtr PmailGame::loadByName(const std::string &rawName) {
  std::lock_guard lock(mutex_);
  std::string name = lower(rawName);
  if (name.find('.') == std::string::npos)
    name += ".obj";
  auto it = byName_.find(name);
  if (it != byName_.end())
    return it->second;

  // Prefer decoded PACK reader (correct container extraction).
  if (packReader_) {
    std::vector<uint8_t> bytes = packReader_->read(name);
    if (!bytes.empty()) {
      auto obj = std::make_shared<::Pmail::ScriptObject>();
      const size_t decodedSize = bytes.size();
      if (!obj->parse(rawName, std::move(bytes))) {
        log("Invalid PMAIL script object: " + rawName);
        return nullptr;
      }
      byName_[name] = obj;
      if (obj->id)
        byId_.emplace(obj->id & 0xfff, obj);
      std::cout << "[PmailPort] Loaded from pack index: " << rawName
                << " (codeWords=" << obj->codeWords << ", id=0x" << std::hex
                << obj->id << std::dec << ", bytes=" << decodedSize << ")"
                << std::endl;
      return obj;
    } else {
      std::cout << "[PmailPort] Pack miss for: " << name << std::endl;
    }
  } else {
    std::cout << "[PmailPort] No packReader for: " << name << std::endl;
  }

  // Fallback: signature-scan indexed chunks from raw pack buffer.
  for (const auto &e : entries_) {
    if (lower(e.name) != name)
      continue;
    if (e.offset + e.size > pack_.size() || e.size == 0)
      continue;
    auto obj = std::make_shared<::Pmail::ScriptObject>();
    std::vector<uint8_t> bytes(pack_.begin() + e.offset,
                               pack_.begin() + e.offset + e.size);
    // Never enter malformed bytecode found by the raw signature fallback.
    // A bad object must not replace a valid route or be cached by name.
    if (!obj->parse(e.name, std::move(bytes)))
      continue;
    byName_[name] = obj;
    if (obj->id)
      byId_.emplace(obj->id & 0xfff, obj);
    return obj;
  }
  return nullptr;
}

::Pmail::ObjectPtr PmailGame::loadById(uint32_t id12) {
  std::lock_guard lock(mutex_);
  auto it = byId_.find(id12 & 0xfff);
  if (it != byId_.end())
    return it->second;
  // The signature scan sees only a small fraction of compressed objects.
  // Resolve IDs through the decoded pack index before falling back to it.
  if (packReader_) {
    for (const auto &entry : packReader_->entries()) {
      if ((entry.id & 0xfff) == (id12 & 0xfff))
        return loadByName(entry.name);
    }
  }
  // Fallback for packs whose index cannot be decoded.
  for (const auto &e : entries_)
    (void)loadByName(e.name);
  it = byId_.find(id12 & 0xfff);
  return it != byId_.end() ? it->second : nullptr;
}

bool PmailGame::loadScript(const std::string &name, bool asMain) {
  std::lock_guard lock(mutex_);
  ::Pmail::ObjectPtr obj = loadByName(name);
  if (!obj)
    return false;
  if (asMain) {
    // pmail.obj is the engine library that owns the user-function table; it
    // must stay registered while story scripts come and go.
    if (main_ && main_ != lib_)
      vm_.unregisterObject(main_);
    main_ = obj;
  }
  scriptName_ = obj->name;
  vm_.jumpTo(obj, 0);
  return true;
}

std::map<int, std::vector<std::string>> PmailGame::translationInventory() {
  std::lock_guard lock(mutex_);
  if (translationCatalogReady_)
    return translationCatalog_;
  std::map<int, std::vector<std::string>> result;
  std::map<int, std::set<std::string>> seen;
  // Read-only indexing: DO NOT call loadByName() here. It registers objects
  // in the VM's name/ID maps and could change control flow merely because
  // the user opened F7. Decode temporary, unregistered script objects.
  for (const auto &entry : entries_) {
    if (entry.name.empty())
      continue;
    std::vector<uint8_t> bytes;
    try {
      if (packReader_)
        bytes = packReader_->read(lower(entry.name));
      if (bytes.empty() && entry.size > 0 && entry.offset <= pack_.size() &&
          entry.size <= pack_.size() - entry.offset)
        bytes.assign(pack_.begin() + entry.offset,
                     pack_.begin() + entry.offset + entry.size);
      if (bytes.empty())
        continue;
      ::Pmail::ScriptObject script;
      if (!script.parse(entry.name, std::move(bytes)))
        continue;
      const int route = static_cast<int>(script.id & 0xfff);
      auto &keys = result[route];
      auto &unique = seen[route];
      for (const auto &line : script.strings)
        if (!line.empty() && unique.insert(line).second)
          keys.push_back(line);
    } catch (const std::exception &) {
      // A malformed optional archive entry must not erase valid translation
      // routes or affect the native VM.
      continue;
    }
  }
  translationCatalog_ = result;
  translationCatalogReady_ = true;
  return result;
}

bool PmailGame::debugJumpScript(const std::string &name) {
  if (name.empty() || name == "pmail.obj")
    return false; // The native library is never a playable story root.
  if (!loadScript(name, true))
    return false;
  std::lock_guard lock(mutex_);
  waiting_ = false;
  choosing_ = false;
  message_.clear();
  speakerName_.clear();
  messageQueue_.clear();
  return true;
}

bool PmailGame::init() {
  std::lock_guard lock(mutex_);
  // pmail.exe:0x422357-0x4223df loads pmail.obj (the user-function library
  // with slots 3, 112, 113, ...) and start.obj, then runs start.obj at pc 0.
  // pmail.exe:0x422357–0x4223df requires pmail.obj and start.obj.
  // Guessing another entry object silently changes the original story.
  const auto library = loadByName("pmail.obj");
  const auto start = loadByName("start.obj");
  if (!library || !start) {
    std::cerr << "[PmailPort] Boot requires pmail.obj and start.obj"
              << " in data/pack/script.dat" << std::endl;
    return false;
  }
  lib_ = library;
  main_ = start;
  scriptName_ = start->name;
  vm_.registerObject(lib_);
  vm_.registerObject(main_);
  std::cout << "[PmailPort] Registered pmail.obj; starting start.obj"
            << std::endl;
  vm_.start(start, 0);
  waiting_ = false;
  title_ = false;
  message_.clear();
  messageQueue_.clear();
  lastRequestedMovie_.clear();
  movieWait_ = false;
  saveMenuWait_ = false;
  saveMenuLoad_ = false;
  extrasMenu_ = false;
  extrasIndex_ = 0;
  extrasViewerMode_ = ExtrasViewerMode::None;
  extrasViewerCategory_ = 0;
  extrasViewerPage_ = 0;
  extrasViewerSlot_ = 0;
  extrasViewerFullView_ = false;
  extrasViewerExtraIndex_ = -1;
  extrasViewerVar_ = 0;
  musicTrackIds_.clear();
  extrasViewerTrack_ = 0;
  extrasViewerTrackFirst_ = 0;
  extrasViewerRepeat_ = false;
  clearComputerOverlay();
  return true;
}

std::string PmailGame::serialize() const {
  std::lock_guard lock(mutex_);
  if (returningToTitle_)
    throw std::runtime_error("Cannot save while returning to title");
  ::Pmail::StateIO::Writer out;
  out.string("PMAIL-GAME-5");
  out.u64(::Pmail::StateIO::fingerprint(pack_.data(), pack_.size()));
  out.string(vm_.serialize());
  auto object = [&](const ::Pmail::ObjectPtr &obj) {
    out.string(obj ? obj->name : std::string());
  };
  object(sub_);
  object(lib_);
  object(main_);
  out.string(scriptName_);
  out.string(message_);
  out.string(speakerName_);
  out.string(background_);
  out.u32(uint32_t(messageQueue_.size()));
  for (const auto &line : messageQueue_)
    out.string(line);
  out.u32(uint32_t(callStack_.size()));
  for (const auto &frame : callStack_) {
    out.string(frame.name);
    out.u32(frame.pc);
  }
  out.u32(uint32_t(sprites_.size()));
  for (const auto &sprite : sprites_) {
    out.string(sprite.name);
    out.u32(uint32_t(sprite.x));
    out.u32(uint32_t(sprite.y));
    out.u32(uint32_t(sprite.id));
    for (int tint : sprite.tint)
      out.u32(uint32_t(tint));
  }
  out.boolean(filled_);
  out.u32(fillColor_);
  out.boolean(waiting_);
  out.boolean(title_);
  out.boolean(choosing_);
  out.u32(uint32_t(audioWait_));
  const auto now = SDL_GetTicks();
  out.u32(audioWait_ == AudioWait::Time && waitUntil_ > now
              ? uint32_t(waitUntil_ - now)
              : 0);
  out.u32(titleChoiceVar_);
  out.u32(uint32_t(titleVariant_));
  out.u32(uint32_t(titleCount_));
  out.u32(uint32_t(titleIndex_));
  out.u32(uint32_t(titleHover_));
  out.u32(choiceVar_);
  out.u32(uint32_t(choiceText_.size()));
  for (const auto &text : choiceText_)
    out.string(text);
  out.u32(uint32_t(choiceValue_.size()));
  for (auto value : choiceValue_)
    out.u32(uint32_t(value));
  out.u32(uint32_t(spriteSlots_.size()));
  for (const auto &[slot, name] : spriteSlots_) {
    out.u32(uint32_t(slot));
    out.string(name);
  }
  out.u32(uint32_t(animations_.size()));
  for (const auto &animation : animations_) {
    out.u32(animation.preset);
    out.u32(uint32_t(animation.x));
    out.u32(uint32_t(animation.y));
    out.u32(animation.frame);
    out.u32(animation.count);
    out.u32(animation.elapsedMs);
    out.boolean(animation.playing);
  }
  out.u32(uint32_t(voiceActor_));
  out.u32(uint32_t(computerText_.size()));
  for (const auto &slot : computerText_) {
    out.string(slot.text);
    out.u32(uint32_t(slot.x));
    out.u32(uint32_t(slot.y));
    out.u32(uint32_t(slot.width));
    out.u32(uint32_t(slot.height));
    out.u32(uint32_t(slot.size));
    out.u32(uint32_t(slot.color));
  }
  out.string(computerClock_);
  out.u32(uint32_t(chatHistory_.size()));
  for (const auto &line : chatHistory_) {
    out.u32(uint32_t(line.mode));
    out.string(line.text);
  }
  out.string(playerNameNO_);
  out.string(playerNameHO_);
  out.boolean(computerOverlayVisible_);
  // Version 5 adds the original two distinct computer UI families. Save
  // bounded structured state, not pre-rendered pixels, so assets can reload.
  const auto writePanel = [&](const ComputerPanel &panel) {
    out.boolean(panel.active);
    out.u32(uint32_t(panel.entries.size()));
    for (const auto &line : panel.entries) {
      out.u32(uint32_t(line.mode));
      out.string(line.text);
    }
    for (const int32_t value : panel.params)
      out.u32(uint32_t(value));
    out.u32(uint32_t(panel.selection));
  };
  writePanel(chatPanel_);
  writePanel(mailPanel_);
  for (const auto &icon : computerIcons_) {
    out.boolean(icon.active);
    out.u32(uint32_t(icon.x));
    out.u32(uint32_t(icon.y));
  }
  return out.finish();
}

void PmailGame::deserialize(const std::string &data) {
  std::lock_guard lock(mutex_);
  ::Pmail::StateIO::Reader in(data);
  const auto version = in.string(32);
  if (version != "PMAIL-GAME-1" && version != "PMAIL-GAME-2" &&
      version != "PMAIL-GAME-3" && version != "PMAIL-GAME-4" &&
      version != "PMAIL-GAME-5")
    throw std::runtime_error("Unsupported PMAIL save version");
  if (in.u64() != ::Pmail::StateIO::fingerprint(pack_.data(), pack_.size()))
    throw std::runtime_error("PMAIL save belongs to a different script pack");
  const auto vmData = in.string(16u << 20);
  auto object = [&]() -> ::Pmail::ObjectPtr {
    const auto name = in.string(256);
    if (name.empty())
      return {};
    const auto obj = loadByName(name);
    if (!obj)
      throw std::runtime_error("Missing saved PMAIL object: " + name);
    return obj;
  };
  auto sub = object(), lib = object(), main = object();
  auto scriptName = in.string(256), message = in.string(),
       speaker = in.string(), background = in.string(256);
  if (!main || !lib)
    throw std::runtime_error("Missing PMAIL main/library object");
  std::deque<std::string> queue;
  for (auto n = in.count(4096); n; --n)
    queue.push_back(in.string());
  std::vector<CallFrame> calls;
  for (auto n = in.count(4096); n; --n) {
    CallFrame frame{in.string(256), in.u32()};
    auto obj = loadByName(frame.name);
    if (!obj || frame.pc > obj->codeWords)
      throw std::runtime_error("Invalid PMAIL script return frame");
    calls.push_back(std::move(frame));
  }
  std::vector<Sprite> sprites;
  for (auto n = in.count(4096); n; --n) {
    Sprite sprite;
    sprite.name = in.string(256);
    sprite.x = int32_t(in.u32());
    sprite.y = int32_t(in.u32());
    sprite.id = int32_t(in.u32());
    for (auto &tint : sprite.tint)
      tint = int32_t(in.u32());
    sprites.push_back(std::move(sprite));
  }
  const bool filled = in.boolean();
  const auto fillColor = in.u32();
  const bool waiting = in.boolean(), title = in.boolean(),
             choosing = in.boolean();
  const auto audioWait = AudioWait(in.count(uint32_t(AudioWait::Time)));
  const auto waitRemaining = in.count(60000);
  const auto titleVar = in.count(65535), variant = in.count(6),
             titleCount = in.count(3);
  const auto titleIndex = in.count(2);
  const int titleHover = int32_t(in.u32());
  const auto choiceVar = in.count(65535);
  if (!titleCount || titleIndex >= titleCount || titleHover < -1 ||
      titleHover >= int(titleCount))
    throw std::runtime_error("Invalid PMAIL title state");
  std::vector<std::string> choices;
  for (auto n = in.count(256); n; --n)
    choices.push_back(in.string());
  std::vector<int> values;
  for (auto n = in.count(256); n; --n)
    values.push_back(int32_t(in.u32()));
  if (choosing && (choices.empty() || values.size() < choices.size()))
    throw std::runtime_error("Invalid PMAIL choice state");
  std::map<int, std::string> slots;
  for (auto n = in.count(4096); n; --n) {
    const auto slot = int32_t(in.u32());
    auto name = in.string(256);
    if (!slots.emplace(slot, std::move(name)).second)
      throw std::runtime_error("Duplicate PMAIL sprite slot");
  }
  int voiceActor = -1;
  std::vector<AnimationState> animations;
  if (version == "PMAIL-GAME-2" || version == "PMAIL-GAME-3" ||
      version == "PMAIL-GAME-4" || version == "PMAIL-GAME-5") {
    const auto count = in.count(4096);
    for (uint32_t i = 0; i < count; ++i) {
      AnimationState animation;
      animation.preset = in.count(uint32_t(AnimationPresets.size() - 1));
      animation.x = int32_t(in.u32());
      animation.y = int32_t(in.u32());
      animation.frame = in.count(1023);
      animation.count = in.count(1024);
      animation.elapsedMs =
          in.count(AnimationPresets[animation.preset].frameMs - 1);
      animation.playing = in.boolean();
      if (!animation.count || animation.frame >= animation.count ||
          (animation.playing && i + 1 != count))
        throw std::runtime_error("Invalid PMAIL animation state");
      if (animationFrameCount &&
          animationFrameCount(AnimationPresets[animation.preset].name) !=
              animation.count)
        throw std::runtime_error(
            "Saved PMAIL animation differs from installed assets");
      animations.push_back(animation);
    }
    voiceActor = int32_t(in.u32());
  }
  std::vector<ComputerText> computerText;
  std::vector<ChatLine> chatHistory;
  std::string computerClock;
  if (version == "PMAIL-GAME-3" || version == "PMAIL-GAME-4" ||
      version == "PMAIL-GAME-5") {
    for (auto n = in.count(ComputerTextLimit); n; --n) {
      ComputerText slot;
      slot.text = in.string(4096);
      slot.x = int32_t(in.u32());
      slot.y = int32_t(in.u32());
      slot.width = int32_t(in.u32());
      slot.height = int32_t(in.u32());
      slot.size = int32_t(in.u32());
      slot.color = int32_t(in.u32());
      if (slot.x < -2048 || slot.x > 2048 || slot.y < -2048 || slot.y > 2048 ||
          slot.width < 0 || slot.width > 4096 || slot.height < 0 ||
          slot.height > 4096 || slot.size < 1 || slot.size > 256 ||
          slot.color < 0 || slot.color > 255)
        throw std::runtime_error("Invalid PMAIL computer text slot");
      computerText.push_back(std::move(slot));
    }
    computerClock = in.string(32);
    for (auto n = in.count(ChatHistoryLimit); n; --n) {
      ChatLine line{int32_t(in.u32()), in.string(4096)};
      chatHistory.push_back(std::move(line));
    }
  }
  std::string savedNO = playerNameNO_, savedHO = playerNameHO_;
  bool overlayVisible =
      !computerText.empty() || !chatHistory.empty() || !computerClock.empty();
  if (version == "PMAIL-GAME-4" || version == "PMAIL-GAME-5") {
    savedNO = in.string(64);
    savedHO = in.string(64);
    overlayVisible = in.boolean();
    if (savedNO.empty() || savedHO.empty() ||
        savedNO.find_first_of("\r\n\t") != std::string::npos ||
        savedHO.find_first_of("\r\n\t") != std::string::npos)
      throw std::runtime_error("Invalid PMAIL player customization");
  }
  ComputerPanel chatPanel, mailPanel;
  std::array<ComputerIcon, ComputerIconLimit> computerIcons{};
  if (version == "PMAIL-GAME-5") {
    const auto readPanel = [&]() -> ComputerPanel {
      ComputerPanel panel;
      panel.active = in.boolean();
      for (auto n = in.count(uint32_t(ComputerPanelLimit)); n; --n)
        panel.entries.push_back({int32_t(in.u32()), in.string(4096)});
      for (auto &value : panel.params)
        value = int32_t(in.u32());
      panel.selection = int32_t(in.u32());
      return panel;
    };
    chatPanel = readPanel();
    mailPanel = readPanel();
    for (auto &icon : computerIcons) {
      icon.active = in.boolean();
      icon.x = int32_t(in.u32());
      icon.y = int32_t(in.u32());
      if (icon.x < -2048 || icon.x > 2048 || icon.y < -2048 || icon.y > 2048)
        throw std::runtime_error("Invalid PMAIL computer icon coordinate");
    }
  }
  in.end();
  ::Pmail::Vm staged(*this);
  staged.deserialize(vmData);
  // All validation completed. Swaps/moves below leave no partially loaded
  // state.
  vm_.swapState(staged);
  sub_ = std::move(sub);
  lib_ = std::move(lib);
  main_ = std::move(main);
  scriptName_ = std::move(scriptName);
  message_ = std::move(message);
  speakerName_ = std::move(speaker);
  background_ = std::move(background);
  messageQueue_ = std::move(queue);
  callStack_ = std::move(calls);
  animations_ = std::move(animations);
  voiceActor_ = voiceActor;
  sprites_ = std::move(sprites);
  filled_ = filled;
  fillColor_ = fillColor;
  waiting_ = waiting;
  title_ = title;
  choosing_ = choosing;
  audioWait_ = audioWait;
  waitUntil_ = SDL_GetTicks() + waitRemaining;
  titleChoiceVar_ = titleVar;
  titleVariant_ = int(variant);
  titleCount_ = int(titleCount);
  titleIndex_ = int(titleIndex);
  titleHover_ = titleHover;
  choiceVar_ = choiceVar;
  choiceText_ = std::move(choices);
  choiceValue_ = std::move(values);
  spriteSlots_ = std::move(slots);
  computerText_ = std::move(computerText);
  computerClock_ = std::move(computerClock);
  chatHistory_ = std::move(chatHistory);
  chatPanel_ = std::move(chatPanel);
  mailPanel_ = std::move(mailPanel);
  computerIcons_ = std::move(computerIcons);
  playerNameNO_ = std::move(savedNO);
  playerNameHO_ = std::move(savedHO);
  computerOverlayVisible_ = overlayVisible;
  returningToTitle_ = false;
  lastRequestedMovie_.clear();
  movieWait_ = false;
  saveMenuWait_ = false;
  saveMenuLoad_ = false;
  extrasMenu_ = false;
  extrasIndex_ = 0;
  extrasViewerMode_ = ExtrasViewerMode::None;
  extrasViewerCategory_ = 0;
  extrasViewerPage_ = 0;
  extrasViewerSlot_ = 0;
  extrasViewerFullView_ = false;
  extrasViewerExtraIndex_ = -1;
  extrasViewerVar_ = 0;
  musicTrackIds_.clear();
  extrasViewerTrack_ = 0;
  extrasViewerTrackFirst_ = 0;
  extrasViewerRepeat_ = false;
  // Runtime diagnostics belong to the live path, never to another save slot.
  unhandledNativeIds_.clear();
  unhandledNativeSites_.clear();
  ++sceneVersion_;
}

void PmailGame::returnToTitle() {
  std::lock_guard lock(mutex_);
  // Preserve persistent game flags, but discard suspended story call frames.
  vm_.reset(false);
  callStack_.clear();
  unhandledNativeIds_.clear();
  unhandledNativeSites_.clear();
  sprites_.clear();
  spriteSlots_.clear();
  endingVignettes_.clear();
  staffRoll_ = {};
  animations_.clear();
  background_.clear();
  message_.clear();
  speakerName_.clear();
  messageQueue_.clear();
  clearComputerOverlay();
  choiceText_.clear();
  choiceValue_.clear();
  choosing_ = false;
  audioWait_ = AudioWait::None;
  waitUntil_ = 0;
  lastRequestedMovie_.clear();
  movieWait_ = false;
  saveMenuWait_ = false;
  saveMenuLoad_ = false;
  extrasMenu_ = false;
  extrasIndex_ = 0;
  extrasViewerMode_ = ExtrasViewerMode::None;
  extrasViewerCategory_ = 0;
  extrasViewerPage_ = 0;
  extrasViewerSlot_ = 0;
  extrasViewerFullView_ = false;
  extrasViewerExtraIndex_ = -1;
  extrasViewerVar_ = 0;
  musicTrackIds_.clear();
  extrasViewerTrack_ = 0;
  extrasViewerTrackFirst_ = 0;
  extrasViewerRepeat_ = false;
  filled_ = false;
  voiceActor_ = -1;
  returningToTitle_ = true;
  if (!init()) {
    returningToTitle_ = false;
    throw std::runtime_error("Cannot load PMAIL title script");
  }
  // Only execute the title script's setup, stopping at its native menu call.
  // Its startup presentation and audio are suppressed by native() below.
  vm_.run(1000000);
  returningToTitle_ = false;
  if (!title_)
    throw std::runtime_error("PMAIL title script did not reach its menu");
  ++sceneVersion_;
}

std::vector<std::string> PmailGame::unlockedExtras() const {
  std::lock_guard lock(mutex_);
  std::vector<std::string> out;
  out.reserve(unlockedExtras_.size());
  for (const auto &name : kExtraNames)
    if (unlockedExtras_.count(std::string(name)))
      out.emplace_back(name);
  return out;
}

void PmailGame::loadUnlockedExtras(const std::vector<std::string> &names) {
  std::lock_guard lock(mutex_);
  for (const auto &name : names)
    if (isExtraName(name))
      unlockedExtras_.insert(name);
  ++extrasVersion_;
}

void PmailGame::resumeFromMovie() {
  std::lock_guard lock(mutex_);
  if (!movieWait_)
    return;
  movieWait_ = false;
  vm_.unblock();
  ++sceneVersion_;
}

void PmailGame::resumeFromSaveMenu() {
  std::lock_guard lock(mutex_);
  if (!saveMenuWait_)
    return;
  saveMenuWait_ = false;
  saveMenuLoad_ = false;
  vm_.unblock();
  ++sceneVersion_;
}

void PmailGame::flushLines() {
  std::lock_guard lock(mutex_);
  auto &lines = vm_.lines();
  if (lines.empty())
    return;
  for (const auto &l : lines) {
    std::string s = vm_.textAt(l.object, l.index);
    if (s.empty())
      continue;
    std::string utf = s;
    try {
      utf = ::Kotonoha::ShiftJisToUtf8(s);
    } catch (...) {
    }
    messageQueue_.push_back(expandPlayerNames(utf));
  }
  lines.clear();
  if (!messageQueue_.empty() && message_.empty())
    showNext();
}

void PmailGame::showNext() {
  std::lock_guard lock(mutex_);
  if (messageQueue_.empty()) {
    waiting_ = false;
    return;
  }
  message_ = std::move(messageQueue_.front());
  messageQueue_.pop_front();
  waiting_ = true;
}

bool PmailGame::popCall() {
  std::lock_guard lock(mutex_);
  if (callStack_.empty())
    return false;
  CallFrame f = callStack_.back();
  callStack_.pop_back();
  ::Pmail::ObjectPtr obj = loadByName(f.name);
  if (!obj)
    return false;
  if (main_ && main_ != lib_)
    vm_.unregisterObject(main_);
  main_ = obj;
  vm_.registerObject(main_);
  scriptName_ = obj->name;
  vm_.jumpTo(obj, f.pc);
  return true;
}

bool PmailGame::skipStaffRoll() {
  std::lock_guard lock(mutex_);
  if (!staffRoll_.active)
    return false;
  char card[32];
  std::snprintf(card, sizeof(card), "END%02d_03", staffRoll_.category);
  background_ = card;
  staffRoll_.active = false;
  if (stopMusic)
    stopMusic();
  vm_.unblock();
  ++sceneVersion_;
  return true;
}

void PmailGame::advanceAnimation(uint32_t elapsedMs) {
  std::lock_guard lock(mutex_);
  if (staffRoll_.active) {
    staffRoll_.elapsedMs += std::min<uint32_t>(elapsedMs, 250);
    if (staffRoll_.elapsedMs >= staffRoll_.durationMs) {
      skipStaffRoll();
    } else {
      const int phase = std::clamp(
          int((uint64_t(staffRoll_.elapsedMs) * 4) / staffRoll_.durationMs), 0,
          3);
      char card[32];
      std::snprintf(card, sizeof(card), "END%02d_%02d", staffRoll_.category,
                    phase);
      if (background_ != card) {
        background_ = card;
        ++sceneVersion_;
      }
    }
    return;
  }
  if (!animating())
    return;
  auto &animation = animations_.back();
  const auto duration = AnimationPresets[animation.preset].frameMs;
  // The original loop draws every frame, even when rendering is slow. Advance
  // at most one frame per update, so a stall never jumps over animation
  // content.
  animation.elapsedMs += std::min(elapsedMs, duration);
  if (animation.elapsedMs < duration)
    return;
  animation.elapsedMs -= duration;
  if (animation.frame + 1 < animation.count) {
    ++animation.frame;
  } else {
    animation.playing = false;
    vm_.unblock();
  }
}

void PmailGame::pump(size_t maxSteps) {
  std::lock_guard lock(mutex_);
  if (waiting_ || choosing_ || animating() || staffRoll_.active)
    return;
  if (audioWait_ != AudioWait::None) {
    bool playing = false;
    if (audioWait_ == AudioWait::SeSkippable)
      playing = sePlaying && sePlaying();
    else if (audioWait_ == AudioWait::Voice ||
             audioWait_ == AudioWait::VoiceSkippable)
      playing = voicePlaying && voicePlaying();
    else if (audioWait_ == AudioWait::Time)
      playing = SDL_GetTicks() < waitUntil_;
    if (playing)
      return;
    audioWait_ = AudioWait::None;
    vm_.unblock();
  }
  vm_.run(maxSteps);
  while (vm_.ended() && !waiting_ && !choosing_ && popCall())
    vm_.run(maxSteps);
  if (vm_.ended())
    waiting_ = false;
}

bool PmailGame::click() {
  std::lock_guard lock(mutex_);
  if (choosing_ || animating())
    return false;
  // pmail.exe:0x423a00 exits its timed-wait loop as soon as an input event
  // is available, so the retail 5-second dramatic pauses are skippable.
  if (audioWait_ == AudioWait::Time) {
    audioWait_ = AudioWait::None;
    waitUntil_ = 0;
    vm_.unblock();
    return true;
  }
  if (!waiting_)
    return false;
  if (title_)
    return titleChoose(titleIndex_);
  if (!messageQueue_.empty()) {
    showNext();
    return true;
  }
  message_.clear();
  speakerName_.clear();
  waiting_ = false;
  vm_.unblock();
  return true;
}

bool PmailGame::choose(size_t i) {
  std::lock_guard lock(mutex_);
  if (!choosing_ || i >= choiceText_.size())
    return false;
  const std::string selectedText = choiceText_[i];
  if (i < choiceValue_.size())
    vm_.setVar(choiceVar_, uint32_t(choiceValue_[i]));
  choosing_ = false;
  choiceText_.clear();
  choiceValue_.clear();
  if (onChoiceSelected && !selectedText.empty())
    onChoiceSelected(selectedText);
  vm_.unblock();
  ++sceneVersion_;
  return true;
}

void PmailGame::titleHover(int index) {
  std::lock_guard lock(mutex_);
  titleHover_ = (index >= 0 && index < titleCount_) ? index : -1;
  if (titleHover_ >= 0)
    titleIndex_ = titleHover_;
}

void PmailGame::titleMove(int delta) {
  std::lock_guard lock(mutex_);
  if (!title_ || titleCount_ <= 0)
    return;
  titleIndex_ += delta;
  if (titleIndex_ < 0)
    titleIndex_ = titleCount_ - 1;
  if (titleIndex_ >= titleCount_)
    titleIndex_ = 0;
  titleHover_ = titleIndex_;
}

bool PmailGame::titleChoose(int index) {
  std::lock_guard lock(mutex_);
  if (!title_)
    return false;
  if (index < 0 || index >= titleCount_)
    return false;
  // 0x42ccd9: the value written back is the image slot id (1..3).
  vm_.setVar(titleChoiceVar_, uint32_t(index + 1));
  title_ = false;
  waiting_ = false;
  vm_.unblock();
  return true;
}

void PmailGame::extrasHover(int index) {
  std::lock_guard lock(mutex_);
  if (!extrasMenu_)
    return;
  extrasIndex_ = index >= 0 && index <= 4 ? index : 0;
}

void PmailGame::extrasMove(int delta) {
  std::lock_guard lock(mutex_);
  if (!extrasMenu_ || delta == 0)
    return;
  // 0x41ec16/0x41ec66: the arrow keys wrap between 1 and 4, while 0 (no
  // item) enters the list at either end.
  if (delta < 0)
    extrasIndex_ = extrasIndex_ <= 1 ? 4 : extrasIndex_ - 1;
  else
    extrasIndex_ =
        extrasIndex_ == 0 || extrasIndex_ == 4 ? 1 : extrasIndex_ + 1;
}

bool PmailGame::extrasChoose(int index) {
  std::lock_guard lock(mutex_);
  if (!extrasMenu_ || index < 1 || index > 4)
    return false;
  // 0x4225c0 writes the menu result into the variable argument (0x402a50).
  vm_.setVar(extrasMenuVar_, uint32_t(index));
  extrasMenu_ = false;
  extrasIndex_ = 0;
  vm_.unblock();
  ++sceneVersion_;
  return true;
}

namespace {
std::string LowerAscii(std::string value) {
  for (auto &c : value)
    if (c >= 'A' && c <= 'Z')
      c = char(c - 'A' + 'a');
  return value;
}

int GalleryCategoryOf(int tab) {
  return int(GalleryCategoryFirst) + std::clamp(tab, 0, 7);
}
} // namespace

std::vector<int> PmailGame::extrasViewerCatalog() const {
  std::vector<int> out;
  if (extrasViewerMode_ == ExtrasViewerMode::CgGrid) {
    const int category = GalleryCategoryOf(extrasViewerCategory_);
    for (int i = 0; i < int(std::size(GalleryCgTable)); ++i)
      if (GalleryCgTable[i].category == category)
        out.push_back(i);
  } else if (extrasViewerMode_ == ExtrasViewerMode::Recollection) {
    const int category = GalleryCategoryOf(extrasViewerCategory_);
    for (int i = 0; i < int(std::size(GalleryExtraTable)); ++i)
      if (GalleryExtraTable[i].category == category &&
          unlockedExtras_.count(GalleryExtraTable[i].slot) != 0)
        out.push_back(i);
  }
  return out;
}

int PmailGame::extrasViewerPageCount() const {
  std::lock_guard lock(mutex_);
  if (extrasViewerMode_ == ExtrasViewerMode::Music)
    return std::max(1, (int(musicTrackIds_.size()) + 23) / 24);
  const auto catalog = extrasViewerCatalog();
  return std::max(1, (int(catalog.size()) + GalleryPageSize - 1) /
                         GalleryPageSize);
}

bool PmailGame::extrasViewerSlotLocked(int slot) const {
  std::lock_guard lock(mutex_);
  if (slot < 1 || slot > GalleryPageSize)
    return true;
  const auto catalog = extrasViewerCatalog();
  const std::size_t index =
      std::size_t(extrasViewerPage_ * GalleryPageSize + slot - 1);
  return index >= catalog.size();
}

std::vector<std::string> PmailGame::extrasViewerCells() const {
  std::lock_guard lock(mutex_);
  std::vector<std::string> out;
  if (extrasViewerMode_ == ExtrasViewerMode::Music) {
    const int first = std::clamp(extrasViewerTrackFirst_, 0,
                                 std::max(0, int(musicTrackIds_.size()) - 1));
    for (int i = first; i < int(musicTrackIds_.size()) && i < first + 24; ++i)
      out.push_back(musicTrackIds_[size_t(i)]);
    return out;
  }
  if (extrasViewerMode_ == ExtrasViewerMode::None)
    return out;
  const auto catalog = extrasViewerCatalog();
  for (int slot = 0; slot < GalleryPageSize; ++slot) {
    const std::size_t index =
        std::size_t(extrasViewerPage_ * GalleryPageSize + slot);
    if (index >= catalog.size()) {
      out.emplace_back();
      continue;
    }
    const int row = catalog[index];
    out.emplace_back(LowerAscii(extrasViewerMode_ == ExtrasViewerMode::CgGrid
                                    ? GalleryCgTable[row].file
                                    : GalleryExtraTable[row].cg));
  }
  return out;
}

int PmailGame::extrasViewerTrackCount() const {
  std::lock_guard lock(mutex_);
  return int(musicTrackIds_.size());
}

void PmailGame::extrasViewerHover(int slot) {
  std::lock_guard lock(mutex_);
  if (extrasViewerMode_ == ExtrasViewerMode::None ||
      extrasViewerMode_ == ExtrasViewerMode::Music)
    return;
  if (slot >= 1 && slot <= GalleryPageSize && !extrasViewerSlotLocked(slot))
    extrasViewerSlot_ = slot;
}

void PmailGame::clampExtrasViewerSelection() {
  if (extrasViewerMode_ == ExtrasViewerMode::Music) {
    if (musicTrackIds_.empty()) {
      extrasViewerTrack_ = 0;
      extrasViewerTrackFirst_ = 0;
      return;
    }
    extrasViewerTrack_ =
        std::clamp(extrasViewerTrack_, 1, int(musicTrackIds_.size()));
    if (extrasViewerTrack_ <= extrasViewerTrackFirst_)
      extrasViewerTrackFirst_ = extrasViewerTrack_ - 1;
    if (extrasViewerTrack_ > extrasViewerTrackFirst_ + 24)
      extrasViewerTrackFirst_ = extrasViewerTrack_ - 24;
    extrasViewerTrackFirst_ =
        std::clamp(extrasViewerTrackFirst_, 0,
                   std::max(0, int(musicTrackIds_.size()) - 1));
    return;
  }
  const int pages = extrasViewerPageCount();
  extrasViewerPage_ = std::clamp(extrasViewerPage_, 0, pages - 1);
  const auto catalog = extrasViewerCatalog();
  if (catalog.empty()) {
    extrasViewerSlot_ = 0;
    extrasViewerExtraIndex_ = -1;
    return;
  }
  extrasViewerSlot_ = std::clamp(extrasViewerSlot_, 1, GalleryPageSize);
  if (extrasViewerSlotLocked(extrasViewerSlot_)) {
    extrasViewerSlot_ = 0;
    for (int slot = 1; slot <= GalleryPageSize; ++slot)
      if (!extrasViewerSlotLocked(slot)) {
        extrasViewerSlot_ = slot;
        break;
      }
  }
  const std::size_t index = std::size_t(extrasViewerPage_ * GalleryPageSize +
                                        std::max(0, extrasViewerSlot_ - 1));
  extrasViewerExtraIndex_ = index < catalog.size() ? catalog[index] : -1;
}

void PmailGame::extrasViewerMove(int dx, int dy) {
  std::lock_guard lock(mutex_);
  if (extrasViewerMode_ == ExtrasViewerMode::Music) {
    if (!musicTrackIds_.empty() && (dx || dy))
      extrasViewerTrack_ += dy != 0 ? dy : dx;
    clampExtrasViewerSelection();
    return;
  }
  if (extrasViewerMode_ == ExtrasViewerMode::None || extrasViewerFullView_)
    return;
  const auto catalog = extrasViewerCatalog();
  if (catalog.empty())
    return;
  int slot = extrasViewerSlot_ > 0 ? extrasViewerSlot_ - 1 : 0;
  int row = slot / 4, col = slot % 4;
  int step = dx != 0 ? dx : dy;
  const bool horizontal = dx != 0;
  for (int guard = 0; guard < GalleryPageSize; ++guard) {
    if (horizontal) {
      col += step;
      if (col < 0) {
        col = 3;
        row = (row + 3) % 3;
      } else if (col > 3) {
        col = 0;
        row = (row + 1) % 3;
      }
    } else {
      row = (row + step + 3) % 3;
    }
    const int candidate = row * 4 + col + 1;
    if (!extrasViewerSlotLocked(candidate)) {
      extrasViewerSlot_ = candidate;
      clampExtrasViewerSelection();
      return;
    }
  }
}

void PmailGame::extrasViewerPageBy(int delta) {
  std::lock_guard lock(mutex_);
  if (extrasViewerMode_ == ExtrasViewerMode::None || delta == 0)
    return;
  const int pages = extrasViewerPageCount();
  if (extrasViewerMode_ == ExtrasViewerMode::Music) {
    if (!musicTrackIds_.empty())
      extrasViewerTrack_ += delta * 24;
    clampExtrasViewerSelection();
    return;
  }
  extrasViewerPage_ = std::clamp(extrasViewerPage_ + delta, 0, pages - 1);
  extrasViewerSlot_ = 0;
  clampExtrasViewerSelection();
}

void PmailGame::extrasViewerCategoryBy(int delta) {
  std::lock_guard lock(mutex_);
  if (extrasViewerMode_ == ExtrasViewerMode::None || delta == 0 ||
      extrasViewerMode_ == ExtrasViewerMode::Music)
    return;
  extrasViewerCategory_ =
      (extrasViewerCategory_ + delta + GalleryCategoryCount) %
      GalleryCategoryCount;
  extrasViewerPage_ = 0;
  extrasViewerSlot_ = 0;
  clampExtrasViewerSelection();
}

void PmailGame::extrasViewerSetCategory(int category) {
  std::lock_guard lock(mutex_);
  if (extrasViewerMode_ == ExtrasViewerMode::None ||
      extrasViewerMode_ == ExtrasViewerMode::Music)
    return;
  const int clamped = std::clamp(category, 0, int(GalleryCategoryCount) - 1);
  if (clamped == extrasViewerCategory_)
    return;
  extrasViewerCategory_ = clamped;
  extrasViewerPage_ = 0;
  extrasViewerSlot_ = 0;
  clampExtrasViewerSelection();
}

bool PmailGame::extrasViewerActivate() {
  std::lock_guard lock(mutex_);
  if (extrasViewerMode_ == ExtrasViewerMode::Music) {
    if (extrasViewerTrack_ >= 1 &&
        extrasViewerTrack_ <= int(musicTrackIds_.size()) && playMusic)
      playMusic(musicTrackIds_[size_t(extrasViewerTrack_ - 1)]);
    return true;
  }
  if (extrasViewerMode_ == ExtrasViewerMode::None)
    return false;
  if (extrasViewerFullView_) {
    extrasViewerFullView_ = false;
    ++sceneVersion_;
    return true;
  }
  if (extrasViewerSlot_ < 1 || extrasViewerSlotLocked(extrasViewerSlot_))
    return false;
  clampExtrasViewerSelection();
  if (extrasViewerExtraIndex_ < 0)
    return false;
  if (extrasViewerMode_ == ExtrasViewerMode::CgGrid) {
    extrasViewerFullView_ = true;
    ++sceneVersion_;
    return true;
  }
  // 回想 MODE returns the selected slot (1..32) so the script can show the
  // associated comment.
  closeExtrasViewer(extrasViewerExtraIndex_ + 1);
  return true;
}

void PmailGame::extrasViewerBack() {
  std::lock_guard lock(mutex_);
  if (extrasViewerMode_ == ExtrasViewerMode::None)
    return;
  if (extrasViewerFullView_) {
    extrasViewerFullView_ = false;
    ++sceneVersion_;
    return;
  }
  closeExtrasViewer(0);
}

void PmailGame::extrasViewerSetTrack(int track) {
  std::lock_guard lock(mutex_);
  if (extrasViewerMode_ != ExtrasViewerMode::Music)
    return;
  extrasViewerTrack_ = track;
  clampExtrasViewerSelection();
}

void PmailGame::extrasViewerTrackBy(int delta) {
  std::lock_guard lock(mutex_);
  if (extrasViewerMode_ != ExtrasViewerMode::Music || delta == 0)
    return;
  extrasViewerTrack_ += delta;
  clampExtrasViewerSelection();
}

void PmailGame::extrasViewerStopMusic() {
  std::lock_guard lock(mutex_);
  if (extrasViewerMode_ != ExtrasViewerMode::Music)
    return;
  if (stopMusic)
    stopMusic();
}

void PmailGame::extrasViewerToggleRepeat() {
  std::lock_guard lock(mutex_);
  if (extrasViewerMode_ != ExtrasViewerMode::Music)
    return;
  extrasViewerRepeat_ = !extrasViewerRepeat_;
  ++sceneVersion_;
}

void PmailGame::closeExtrasViewer(int result) {
  if (extrasViewerMode_ == ExtrasViewerMode::None)
    return;
  if (extrasViewerMode_ == ExtrasViewerMode::Recollection)
    vm_.setVar(extrasViewerVar_, uint32_t(std::max(0, result)));
  extrasViewerMode_ = ExtrasViewerMode::None;
  extrasViewerFullView_ = false;
  extrasViewerSlot_ = 0;
  extrasViewerCategory_ = 0;
  extrasViewerPage_ = 0;
  extrasViewerExtraIndex_ = -1;
  extrasViewerTrack_ = 0;
  extrasViewerTrackFirst_ = 0;
  extrasViewerRepeat_ = false;
  musicTrackIds_.clear();
  if (stopMusic)
    stopMusic();
  vm_.unblock();
  ++sceneVersion_;
}

// A diagnostic signature must not dump copyrighted dialogue strings, player
// names or other private text from native arguments. Keep only references and
// numeric values that help identify the original function's calling convention.
std::string NativeSignature(const ::Pmail::Vm &vm,
                            const std::vector<::Pmail::Arg> &args) {
  std::string out = " argc=" + std::to_string(args.size()) + " [";
  const auto shown = std::min(args.size(), std::size_t{24});
  for (std::size_t i = 0; i < shown; ++i) {
    if (i)
      out += ", ";
    const auto &arg = args[i];
    switch (arg.tag()) {
    case 0:
      out += "int:" + std::to_string(int16_t(arg.value()));
      break;
    case 1:
      out += "var:" + std::to_string(arg.value()) + "=" +
             std::to_string(vm.var(arg.value()));
      break;
    case 2:
      out += "strref:" + std::to_string(arg.object()) + ":" +
             std::to_string(arg.value());
      break;
    case 4:
      out += "frame:" + std::to_string(arg.value());
      break;
    default:
      out += "tag:" + std::to_string(arg.tag()) + ":" +
             std::to_string(arg.value());
      break;
    }
  }
  if (args.size() > shown)
    out += ", ...";
  out += "]";
  return out;
}

void PmailGame::native(::Pmail::Vm &vm, int id,
                       const std::vector<::Pmail::Arg> &args) {
  std::lock_guard lock(mutex_);
  if (trace) {
    std::string t = "sys " + std::to_string(id) + "(";
    for (size_t i = 0; i < args.size(); ++i) {
      if (i)
        t += ", ";
      if (args[i].tag() == 2)
        t += '"' + vm.text(args[i]) + '"';
      else if (args[i].tag() == 1)
        t += "v" + std::to_string(args[i].value());
      else
        t += std::to_string(vm.value(args[i]));
    }
    t += ")";
    trace(t);
  }
  if (returningToTitle_ && id != 213 && id != 214)
    return;
  if (onNative && !returningToTitle_)
    onNative(id, args);

  auto argStr = [&](size_t i) -> std::string {
    return i < args.size() ? vm.text(args[i]) : std::string();
  };
  auto argInt = [&](size_t i) -> int {
    return i < args.size() ? int(vm.value(args[i])) : 0;
  };
  auto firstString = [&]() -> std::string {
    for (const auto &a : args)
      if (a.tag() == 2)
        return vm.text(a);
    return {};
  };

  switch (id) {
  case 1: {
    flushLines();
    if (!message_.empty()) {
      waiting_ = true;
      vm.block();
    }
    break;
  }
  case 4: {
    flushLines();
    // pmail.exe:0x423440 captures ONLY the first tag==2 string argument
    // (args[2..3] in PRO1.obj carry trailing metadata and must not display).
    const bool haveQueued = !message_.empty() || !messageQueue_.empty();
    if (!haveQueued) {
      const std::string raw = firstString();
      if (!raw.empty()) {
        std::string utf = raw;
        try {
          utf = ::Kotonoha::ShiftJisToUtf8(raw);
        } catch (...) {
        }
        messageQueue_.push_back(expandPlayerNames(utf));
        if (message_.empty())
          showNext();
      }
    }
    if (!message_.empty()) {
      waiting_ = true;
      vm.block();
    }
    break;
  }
  case 7: {
    // pmail.exe:0x423250 — interactive route choice menu (252 call sites in
    // script.dat; SYS 9 has 0 calls). Consumes queued op-0x09 lines and writes
    // the 1-based selection (1..N) into the variable reference argument.
    choiceText_.clear();
    choiceValue_.clear();
    choiceVar_ = 0;
    for (const auto &a : args) {
      if (a.tag() == 1) {
        choiceVar_ = a.value();
        break;
      }
    }
    auto &queued = vm.lines();
    int idx = 1;
    for (const auto &l : queued) {
      const std::string raw = vm.textAt(l.object, l.index);
      if (raw.empty())
        continue;
      std::string utf = raw;
      try {
        utf = ::Kotonoha::ShiftJisToUtf8(raw);
      } catch (...) {
      }
      choiceText_.push_back(expandPlayerNames(utf));
      choiceValue_.push_back(idx++);
    }
    queued.clear();
    if (choiceText_.empty()) {
      for (const auto &a : args) {
        if (a.tag() == 2) {
          const std::string raw = vm.text(a);
          if (raw.empty())
            continue;
          std::string utf = raw;
          try {
            utf = ::Kotonoha::ShiftJisToUtf8(raw);
          } catch (...) {
          }
          choiceText_.push_back(expandPlayerNames(utf));
          choiceValue_.push_back(idx++);
        }
      }
    }
    if (!choiceText_.empty()) {
      choosing_ = true;
      vm.block();
      ++sceneVersion_;
    }
    break;
  }
  case 21: {
    // pmail.exe:0x423400 — compiled-out debug string trace; resolves tag==2
    // string references (caching their owning object) and returns immediately.
    for (const auto &a : args)
      if (a.tag() == 2)
        (void)vm.text(a);
    break;
  }
  case 2:
  case 3:
  case 23:
    message_.clear();
    speakerName_.clear();
    break;
  case 5:
    for (const auto &a : args)
      if (a.tag() == 2)
        speakerName_ =
            expandPlayerNames(::Kotonoha::ShiftJisToUtf8(vm.text(a)));
    break;
  case 9: {
    // Choice: args include option strings and a target var ref.
    choiceText_.clear();
    choiceValue_.clear();
    choiceVar_ = 0;
    for (const auto &a : args) {
      if (a.tag() == 2)
        choiceText_.push_back(
            expandPlayerNames(::Kotonoha::ShiftJisToUtf8(vm.text(a))));
      else if (a.tag() == 1)
        choiceVar_ = a.value();
      else if (a.tag() == 0)
        choiceValue_.push_back(int(a.value()));
    }
    if (choiceValue_.size() < choiceText_.size()) {
      choiceValue_.resize(choiceText_.size());
      for (size_t i = 0; i < choiceValue_.size(); ++i)
        if (choiceValue_[i] == 0)
          choiceValue_[i] = int(i + 1);
    }
    if (!choiceText_.empty()) {
      choosing_ = true;
      vm.block();
    }
    break;
  }
  case 16: {
    // Scene change (pmail.exe SYS 16). Story scripts are chained, so keep the
    // caller on the stack to come back to the title loop in start.obj.
    std::string name;
    for (const auto &arg : args)
      if (arg.tag() == 2) {
        name = vm.text(arg);
        if (!name.empty())
          break;
      }
    if (!name.empty()) {
      // Resolve the destination *before* modifying the caller stack.
      // Missing optional/retail scripts must be observable, not create a
      // dangling return frame that corrupts later route transitions.
      const CallFrame caller{scriptName_, vm.returnPc()};
      if (loadScript(name, true)) {
        callStack_.push_back(caller);
        clearComputerOverlay();
      } else {
        log("Missing PMAIL script: " + name);
      }
    }
    break;
  }
  case 28: {
    std::string name = firstString();
    if (!name.empty()) {
      // Resolve the destination *before* modifying the caller stack.
      // Missing optional/retail scripts must be observable, not create a
      // dangling return frame that corrupts later route transitions.
      const CallFrame caller{scriptName_, vm.returnPc()};
      if (loadScript(name, true)) {
        callStack_.push_back(caller);
        clearComputerOverlay();
      } else {
        log("Missing PMAIL script: " + name);
      }
    }
    break;
  }
  case 29:
    popCall();
    break;
  case 33: {
    int duration = argInt(0);
    if (duration > 0) {
      waitUntil_ = SDL_GetTicks() + uint64_t(std::min(duration, 60000));
      audioWait_ = AudioWait::Time;
      vm.block();
    }
    break;
  }
  case 48:
    for (const auto &arg : args)
      if (arg.tag() == 2) {
        background_ = vm.text(arg);
        sprites_.clear();
        animations_.clear();
        clearComputerOverlay();
        filled_ = false;
        ++sceneVersion_;
        break;
      }
    break;
  case 49: {
    Sprite s;
    s.name = firstString();
    std::vector<int> ints;
    for (const auto &a : args)
      if (a.tag() == 0)
        ints.push_back(int(int16_t(a.value())));

    if (!ints.empty()) {
      int offset = 0;
      if (s.name.empty()) {
        auto it = spriteSlots_.find(ints[0]);
        if (it != spriteSlots_.end()) {
          s.name = it->second;
        }
        offset = 1;
      }

      if (ints.size() >= offset + 1)
        s.x = ints[offset];
      if (ints.size() >= offset + 2)
        s.y = ints[offset + 1];
      if (ints.size() >= offset + 3)
        s.id = ints[offset + 2];
      for (size_t i = offset + 3; i < ints.size() && i - (offset + 3) < 3; ++i)
        s.tint[i - (offset + 3)] = ints[i];
    }

    if (!s.name.empty()) {
      sprites_.erase(
          std::remove_if(sprites_.begin(), sprites_.end(),
                         [&](const Sprite &o) { return o.id == s.id; }),
          sprites_.end());
      sprites_.push_back(std::move(s));
      ++sceneVersion_;
    }
    break;
  }
  case 51: {
    const int preset = argInt(0);
    if (preset < 0 || size_t(preset) >= AnimationPresets.size())
      break;
    const auto &definition = AnimationPresets[size_t(preset)];
    const auto count =
        animationFrameCount ? animationFrameCount(definition.name) : 0;
    if (!count || count > 1024)
      throw std::runtime_error(std::string("Cannot play PMAIL animation: ") +
                               definition.name);
    animations_.push_back({uint32_t(preset), int(int16_t(argInt(1))),
                           int(int16_t(argInt(2))), 0, uint32_t(count), 0,
                           true});
    vm.block();
    break;
  }
  case 50: {
    // Handler 0x424060 stores a record in a 25-slot icon table
    // (valid indices 0..24), preserving two coordinates. The original
    // sprite-pointer table at 0xf88b2a still requires identification.
    const int slot = argInt(0);
    if (slot >= 0 && slot < int(ComputerIconLimit)) {
      computerIcons_[size_t(slot)] = {true, int32_t(int16_t(argInt(1))),
                                      int32_t(int16_t(argInt(2)))};
      computerOverlayVisible_ = true;
      ++sceneVersion_;
    }
    break;
  }
  case 52: {
    // Original pmail.exe keeps 10 positioned text slots (9 x 32-bit words
    // each). Text IDs resolve through the VM and are rendered by the stage.
    if (computerText_.size() >= ComputerTextLimit)
      break;
    const auto raw = firstString();
    if (raw.empty())
      break;
    std::vector<int> values;
    for (const auto &arg : args)
      if (arg.tag() == 0)
        values.push_back(int(int16_t(arg.value())));
    if (values.size() < 6)
      break;
    std::string text = raw;
    try {
      text = ::Kotonoha::ShiftJisToUtf8(raw);
    } catch (...) {
    }
    ComputerText slot;
    slot.text = expandPlayerNames(text);
    slot.x = std::clamp(values[0], -2048, 2048);
    slot.y = std::clamp(values[1], -2048, 2048);
    slot.width = std::clamp(values[2], 0, 4096);
    slot.height = std::clamp(values[3], 0, 4096);
    slot.size = std::clamp(values[4], 1, 256);
    slot.color = std::clamp(values[5], 0, 255);
    computerText_.push_back(std::move(slot));
    computerOverlayVisible_ = true;
    ++sceneVersion_;
    break;
  }
  case 80: {
    // The native engine stores a short clock label for the computer screen.
    const auto raw = firstString();
    try {
      computerClock_ = ::Kotonoha::ShiftJisToUtf8(raw);
    } catch (...) {
      computerClock_ = raw;
    }
    if (computerClock_.size() > 32)
      computerClock_.resize(32);
    computerOverlayVisible_ = true;
    ++sceneVersion_;
    break;
  }
  case 160:
  case 162:
  case 164: {
    // Both opcodes append to the same 15-entry chat log. SYS 160 triggers
    // immediate redraw in the original; the SDL scene redraws each frame.
    const auto raw = firstString();
    if (raw.empty())
      break;
    std::string text = raw;
    try {
      text = ::Kotonoha::ShiftJisToUtf8(raw);
    } catch (...) {
    }
    if (chatHistory_.size() == ChatHistoryLimit)
      chatHistory_.erase(chatHistory_.begin());
    chatHistory_.push_back({argInt(0), expandPlayerNames(text)});
    computerOverlayVisible_ = true;
    ++sceneVersion_;
    break;
  }
  case 17:
    // pmail.exe:0x422570 clears the VM string buffer (0x401e30), processes
    // the pending message queue (0x409700) and stores text mode 0 (0x425060).
    // Retail calls this immediately before switching story objects, so stale
    // dialogue and text-surface references must not survive the transition.
    message_.clear();
    messageQueue_.clear();
    speakerName_.clear();
    waitUntil_ = 0;
    audioWait_ = AudioWait::None;
    textMode_ = 0;
    currentTextObject_ = currentTextIndex_ = -1;
    nextTextObject_ = nextTextIndex_ = -1;
    ++sceneVersion_;
    break;
  case 18:
  case 25: {
    // pmail.exe:0x422bb0 / 0x422c20 extract {objectId, stringIndex} from the
    // string reference (object id in bits 20+, index in the low word) and
    // store it at 0xf87d6e/0x72 (SYS 18, current) or 0xf87d76/0x7a (SYS 25,
    // next). Both enqueue a flag-0/flag-1 record through 0x409430, the
    // saveable native message-history queue.
    int objectId = -1;
    int stringIndex = -1;
    for (const auto &arg : args)
      if (arg.tag() == 2) {
        objectId = int(arg.object());
        stringIndex = int(arg.value());
        break;
      }
    if (id == 18) {
      currentTextObject_ = objectId;
      currentTextIndex_ = stringIndex;
      nextTextObject_ = nextTextIndex_ = -1;
    } else {
      nextTextObject_ = objectId;
      nextTextIndex_ = stringIndex;
    }
    ++sceneVersion_;
    break;
  }
  case 19:
    // pmail.exe:0x4223e0 -> 0x4251d0 creates the eight native text surfaces
    // from the current/next text references. Kotonoha rasterizes text per
    // frame, so only the lifecycle state is observable.
    textSurfacesReady_ = true;
    ++sceneVersion_;
    break;
  case 20:
    // pmail.exe:0x4223f0 -> 0x4250e0/0x425100 releases those text surfaces.
    textSurfacesReady_ = false;
    ++sceneVersion_;
    break;
  case 24:
    // pmail.exe:0x422410 clears the two message-display flags, the 15-line
    // chat transcript (0x407700) and the display window (0x425000).
    message_.clear();
    messageQueue_.clear();
    speakerName_.clear();
    chatHistory_.clear();
    ++sceneVersion_;
    break;
  case 216: {
    // pmail.exe:0x4227e0 stores the text mode at 0xa8add8. Mode 0 also
    // resets the message display and chat transcript.
    textMode_ = argInt(0);
    if (textMode_ == 0) {
      message_.clear();
      messageQueue_.clear();
      speakerName_.clear();
      chatHistory_.clear();
    }
    ++sceneVersion_;
    break;
  }
  case 32:
    // pmail.exe:0x423910 -> 0x41af20(1, 1) redraws the 8x4 save/load slot
    // icon grid at (722, 560). The SDL pause menu owns save/load slots, so
    // the native icon atlas has no VM-visible equivalent.
    break;
  case 34: {
    // pmail.exe:0x423a00: count = value * [0xf5b47a] / 800; each iteration
    // waits at least 10 ms and polls input. With the retail calibration this
    // is a value-millisecond pause (PRO1 uses 5000 for dramatic beats).
    // Keep it non-blocking and let a click end the wait like native input.
    const int requested = std::max(0, argInt(0));
    if (requested > 0) {
      waitUntil_ = SDL_GetTicks() + uint64_t(requested);
      audioWait_ = AudioWait::Time;
      vm.block();
    }
    break;
  }
  case 53: {
    // pmail.exe:0x424e70 -> 0x4011b0 appends the retail L_/S_ and I5/I3
    // suffixes to the stem (PRO1 uses "pm01") and plays %sMOVIE\%s.AVI.
    // Playback blocks the script until the movie ends or is skipped.
    const std::string stem = firstString();
    if (!stem.empty()) {
      lastRequestedMovie_ = stem;
      if (playMovie && playMovie(stem)) {
        movieWait_ = true;
        vm_.block();
      }
    }
    break;
  }
  case 65:
    // pmail.exe:0x424490 copies the back buffer to its saved framebuffer.
    // SDL owns the render targets in this port; there is no VM state.
    break;
  case 225:
  case 226: {
    // pmail.exe:0x422830 posts WM_COMMAND 0xbba (SYS 225, load) or 0xbbb
    // (SYS 226, save), the original save/load dialogs. The host owns the SDL
    // save UI, so block the VM until that dialog closes.
    const bool load = (id == 225);
    saveMenuWait_ = true;
    saveMenuLoad_ = load;
    if (openSaveMenu && openSaveMenu(load))
      vm_.block();
    else
      saveMenuWait_ = false;
    break;
  }
  case 137:
    // Routed to PmailAudio before this switch. pmail.exe:0x4248f0 builds a
    // per-day sound name (V<cat>5<variant><cat>00<day>) and plays it on the
    // SE bus, which the following SYS 101 waits for.
    break;
  case 176:
  case 178:
  case 192:
  case 194: {
    // Paired native families, recovered from the retail dispatch table:
    // 176/178 -> 0x417d80 (chat panel), 192/194 -> 0x411320
    // (mail panel). Both consume a string reference and an integer.
    // 176/192 additionally call 0x4036e0 and trigger a redraw.
    auto &panel = id < 192 ? chatPanel_ : mailPanel_;
    const auto raw = firstString();
    if (!raw.empty()) {
      std::string utf = raw;
      try {
        utf = ::Kotonoha::ShiftJisToUtf8(raw);
      } catch (...) {
      }
      if (panel.entries.size() >= ComputerPanelLimit)
        panel.entries.erase(panel.entries.begin());
      auto line = expandPlayerNames(utf);
      if (line.size() > 4096)
        line.resize(4096); // matches the bounded save reader
      panel.entries.push_back({argInt(0), std::move(line)});
      panel.active = true;
      computerOverlayVisible_ = true;
      ++sceneVersion_;
    }
    break;
  }
  case 177:
    // Native 0x423700 resets the chat-family control at 0xf8a776.
    chatPanel_ = {};
    ++sceneVersion_;
    break;
  case 193:
    // Native 0x423890 resets the mail-family control at 0xf8a796.
    mailPanel_ = {};
    ++sceneVersion_;
    break;
  case 179: {
    // Original copies up to seven signed numeric parameters into
    // 0xf8a77a..0xf8a792, which configure the chat panel.
    chatPanel_.params = {};
    size_t index = 0;
    for (const auto &a : args)
      if (a.tag() == 0 && index < chatPanel_.params.size())
        chatPanel_.params[index++] = int16_t(a.value());
    chatPanel_.active = true;
    computerOverlayVisible_ = true;
    ++sceneVersion_;
    break;
  }
  case 195:
    // Original 0x4238a0 configures the mail-family selection at 0xf8a79a.
    mailPanel_.selection = int16_t(argInt(0));
    mailPanel_.active = true;
    computerOverlayVisible_ = true;
    ++sceneVersion_;
    break;
  case 161:
    chatHistory_.clear();
    ++sceneVersion_;
    break;
  case 163:
    // Original redraw/commit operation; the renderer is already retained.
    ++sceneVersion_;
    break;
  case 64: {
    // pmail.exe:0x4244b0 -> 0x40c3a0 scene transition (type, param, speed).
    // Mode 7 (0x4244e9) explicitly loads tall CG hiro17.DIB (800x970) and pans.
    const int type = argInt(0);
    const int param = argInt(1);
    const int speed = argInt(2);
    if (type == 7) {
      background_ = "hiro17";
      sprites_.clear();
      animations_.clear();
      clearComputerOverlay();
      filled_ = false;
    }
    lastTransition_ = {type, param, speed, ++transitionVersion_};
    ++sceneVersion_;
    if (onTransition)
      onTransition(type, param, speed);
    break;
  }
  case 69: {
    int c[3] = {0, 0, 0}, n = 0;
    for (const auto &a : args)
      if (a.tag() == 0 && n < 3)
        c[n++] = std::clamp(int(a.value()), 0, 255);
    background_.clear();
    sprites_.clear();
    animations_.clear();
    clearComputerOverlay();
    filled_ = true;
    fillColor_ = uint32_t(c[0] << 16 | c[1] << 8 | c[2]);
    ++sceneVersion_;
    break;
  }
  case 101:
    if (sePlaying && sePlaying()) {
      audioWait_ = AudioWait::SeSkippable;
      vm.block();
    }
    break;
  case 128:
    voiceActor_ = argInt(0);
    break;
  case 131:
  case 133:
    if (voicePlaying && voicePlaying()) {
      audioWait_ = (id == 131) ? AudioWait::Voice : AudioWait::VoiceSkippable;
      vm.block();
    }
    break;
  case 96:
  case 97:
  case 98:
  case 112:
  case 132:
  case 99:
  case 100:
  case 115:
  case 116:
    // These are implemented by the PmailAudio runtime callback, which runs
    // before the VM's presentation-specific switch. Do not report them as
    // missing just because their audio effect lives in another component.
    break;
  case 213: {
    // Title menu: SYS 213(mode, &var). mode 0 shows and blocks, mode 1 hides.
    // pmail.exe:0x4230e0 -> 0x42c950 fills `var` with 1..3 (or 0 on a miss).
    const int mode = args.empty() ? 0 : argInt(0);
    uint32_t var = 0;
    bool haveVar = false;
    for (const auto &arg : args)
      if (arg.tag() == 1) {
        var = arg.value();
        haveVar = true;
      }
    if (haveVar)
      titleChoiceVar_ = var;
    if (mode != 0 && haveVar) {
      title_ = false;
      break;
    }
    titleCount_ = 3;
    titleIndex_ = 0;
    titleHover_ = 0;
    vm_.setVar(titleChoiceVar_, 0);
    title_ = true;
    waiting_ = true;
    vm.block();
    break;
  }
  case 211: {
    // pmail.exe:0x422780 -> 0x40f070 ending staff roll (pm_end.obj).
    // Args: (heroineCategory 3..9, bgmTrack 23|26).
    const int cat = std::clamp(argInt(0), 3, 9);
    const int bgm = argInt(1);
    if (bgm > 0 && playMusic) {
      char track[32];
      std::snprintf(track, sizeof(track), "sound/mm%03d", bgm);
      playMusic(track);
    }
    message_.clear();
    messageQueue_.clear();
    speakerName_.clear();
    sprites_.clear();
    animations_.clear();
    clearComputerOverlay();
    filled_ = false;
    staffRoll_.category = cat;
    staffRoll_.bgmTrack = bgm;
    staffRoll_.elapsedMs = 0;
    staffRoll_.durationMs = 48000;
    staffRoll_.vignettes = endingVignettes(cat);
    char card[32];
    std::snprintf(card, sizeof(card), "END%02d_%02d", cat,
                  interactiveStaffRoll_ ? 0 : 3);
    background_ = card;
    if (interactiveStaffRoll_) {
      staffRoll_.active = true;
      vm.block();
    } else {
      staffRoll_.active = false;
    }
    ++sceneVersion_;
    break;
  }
  case 214: {
    // pmail.exe:0x422600 — registers ending .EDG cut-in lists per heroine
    // category (start.obj:23..713) as well as sprite/voice associations.
    const int slot = argInt(0);
    const std::string name = args.size() > 1 ? vm.text(args[1]) : firstString();
    if (slot >= 0 && !name.empty()) {
      spriteSlots_[slot] = name;
      auto &list = endingVignettes_[slot];
      if (std::find(list.begin(), list.end(), name) == list.end())
        list.push_back(name);
    }
    break;
  }
  case 215: {
    // pmail.exe:0x422650 concatenates the string arguments, resolves the
    // name against the 32-entry table at 0x44f394 and sets the corresponding
    // global flag 0x7933c0[0xc00 + index]. Unknown names are ignored.
    std::string name;
    for (const auto &arg : args) {
      if (arg.tag() != 2)
        continue;
      name += vm.text(arg);
      if (name.size() >= 255) {
        name.resize(255);
        break;
      }
    }
    if (isExtraName(name) && unlockedExtras_.insert(std::move(name)).second)
      ++extrasVersion_;
    break;
  }
  case 208: {
    // pmail.exe:0x41d810. MUSIC MODE: 183-entry playlist, player controls and
    // repeat toggle. The track catalog lives in external data, so the host
    // supplies the available BGM ids and owns playback (SYS 112 bus).
    musicTrackIds_.clear();
    if (musicTracks) {
      musicTrackIds_ = musicTracks();
      musicTrackIds_.erase(
          std::remove_if(musicTrackIds_.begin(), musicTrackIds_.end(),
                         [](const std::string &id) { return id.empty(); }),
          musicTrackIds_.end());
    }
    extrasViewerMode_ = ExtrasViewerMode::Music;
    extrasViewerCategory_ = 0;
    extrasViewerPage_ = 0;
    extrasViewerSlot_ = 0;
    extrasViewerExtraIndex_ = -1;
    extrasViewerFullView_ = false;
    extrasViewerVar_ = 0;
    extrasViewerTrack_ = musicTrackIds_.empty() ? 0 : 1;
    extrasViewerTrackFirst_ = 0;
    vm_.block();
    ++sceneVersion_;
    break;
  }
  case 209: {
    // pmail.exe:0x405390. CG MODE: 8 category tabs, pages of 12 thumbnails,
    // page arrows and EXIT. Returns no value.
    extrasViewerMode_ = ExtrasViewerMode::CgGrid;
    extrasViewerCategory_ = 0;
    extrasViewerPage_ = 0;
    extrasViewerSlot_ = 0;
    extrasViewerExtraIndex_ = -1;
    extrasViewerFullView_ = false;
    extrasViewerVar_ = 0;
    clampExtrasViewerSelection();
    vm_.block();
    ++sceneVersion_;
    break;
  }
  case 210: {
    // pmail.exe:0x42a4c0. KAIROKU MODE: the same 12-per-page grid, but only
    // unlocked slots are listed. Returns the selected slot (1..32) through
    // the first variable argument, or 0 when the player leaves.
    uint32_t var = 0;
    bool haveVar = false;
    for (const auto &arg : args) {
      if (arg.tag() == 1) {
        var = arg.value();
        haveVar = true;
        break;
      }
    }
    extrasViewerMode_ = ExtrasViewerMode::Recollection;
    extrasViewerCategory_ = 0;
    extrasViewerPage_ = 0;
    extrasViewerSlot_ = 0;
    extrasViewerExtraIndex_ = -1;
    extrasViewerFullView_ = false;
    extrasViewerVar_ = var;
    if (haveVar) {
      const int value = int(vm_.var(var));
      if (value >= 1 && value <= int(std::size(GalleryExtraTable)) &&
          unlockedExtras_.count(GalleryExtraTable[value - 1].slot) != 0) {
        extrasViewerCategory_ =
            GalleryExtraTable[value - 1].category - GalleryCategoryFirst;
        const auto catalog = extrasViewerCatalog();
        for (int i = 0; i < int(catalog.size()); ++i)
          if (catalog[size_t(i)] == value - 1) {
            extrasViewerPage_ = i / GalleryPageSize;
            extrasViewerSlot_ = i % GalleryPageSize + 1;
            break;
          }
      }
    }
    clampExtrasViewerSelection();
    vm_.block();
    ++sceneVersion_;
    break;
  }
  case 212: {
    // pmail.exe:0x4225c0 -> 0x41e9f0. The first variable argument receives the
    // selection (1 CG MODE, 2 KAIROKU MODE, 3 MUSIC MODE, 4 EXIT). The native
    // menu is modal: the VM stays blocked until the player chooses.
    uint32_t var = 0;
    bool haveVar = false;
    for (const auto &arg : args) {
      if (arg.tag() == 1) {
        var = arg.value();
        haveVar = true;
        break;
      }
    }
    if (!haveVar)
      break;
    extrasMenuVar_ = var;
    vm_.setVar(var, 0);
    extrasIndex_ = 0;
    extrasMenu_ = true;
    vm_.block();
    ++sceneVersion_;
    break;
  }
  default: {
    // Unknown calls must remain visible rather than masquerading as working
    // no-ops. Capture the argument contract at each script+PC with a bounded
    // cache; repeated frames cannot flood the diagnostic log.
    const uint32_t pc = vm.returnPc();
    const auto key = std::make_tuple(id, scriptName_, pc);
    const auto found = unhandledNativeSites_.find(key);
    const bool distinct = found == unhandledNativeSites_.end();
    std::string signature;
    if (distinct && unhandledNativeSites_.size() < NativeGapLimit) {
      signature = NativeSignature(vm, args);
      unhandledNativeSites_.emplace(
          key, NativeGap{id, scriptName_, pc, signature, 1});
    } else if (!distinct) {
      ++found->second.hits;
    }
    const bool firstId = unhandledNativeIds_.insert(id).second;
    const char *verbose = SDL_getenv("PMAIL_NATIVE_VERBOSE");
    if (firstId ||
        (distinct && verbose && *verbose && std::strcmp(verbose, "0") != 0)) {
      if (signature.empty() && !distinct)
        signature = found->second.signature;
      log("Unhandled PMAIL SYS " + std::to_string(id) + " at " + scriptName_ +
          ":" + std::to_string(pc) + signature);
    }
    break;
  }
  }
}

void PmailGame::log(const std::string &s) {
  if (trace)
    trace("! " + s);
  else
    SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "[PmailPort] %s", s.c_str());
}

} // namespace Kotonoha::PmailPort
