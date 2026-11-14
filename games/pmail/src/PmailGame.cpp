#include "Pmail/Port.hpp"
#include "PmailPack.hpp"
#include <Pmail/StateIO.hpp>

#include <Kotonoha/localization/Encoding.hpp>
#include <SDL3/SDL.h>

#include <algorithm>
#include <cctype>
#include <cstring>
#include <iostream>

namespace Kotonoha::PmailPort {

namespace {
std::string lower(std::string s) {
  std::transform(s.begin(), s.end(), s.begin(),
                 [](unsigned char c) { return char(std::tolower(c)); });
  return s;
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
      std::cerr << "[PmailPort] PmailPack index unavailable, falling back to scan: "
                << packPath_ << std::endl;
      packReader_.reset();
    }
  }
  indexPack();
}

PmailGame::~PmailGame() = default;

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
      if (!obj->parse(rawName, std::move(bytes)))
        return nullptr;
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
    obj->parse(e.name, std::move(bytes));
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

bool PmailGame::init() {
  std::lock_guard lock(mutex_);
  // pmail.exe:0x422357-0x4223df loads pmail.obj (the user-function library
  // with slots 3, 112, 113, ...) and start.obj, then runs start.obj at pc 0.
  lib_ = loadByName("pmail.obj");
  if (lib_) {
    vm_.registerObject(lib_);
    std::cout << "[PmailPort] Registered library: pmail.obj" << std::endl;
  }
  const char *candidates[] = {"start.obj", "main.obj", "01m1.obj", "a.obj",
                              nullptr};
  ::Pmail::ObjectPtr start;
  for (int i = 0; candidates[i]; ++i) {
    start = loadByName(candidates[i]);
    if (start) {
      std::cout << "[PmailPort] Starting script: " << candidates[i]
                << std::endl;
      break;
    }
  }
  if (!start && !entries_.empty()) {
    start = loadByName(entries_.front().name);
    if (start)
      std::cout << "[PmailPort] Starting first indexed script: "
                << entries_.front().name << std::endl;
  }
  if (!start)
    return false;
  main_ = start;
  scriptName_ = start->name;
  vm_.registerObject(main_);
  vm_.start(start, 0);
  waiting_ = false;
  title_ = false;
  message_.clear();
  messageQueue_.clear();
  return true;
}

std::string PmailGame::serialize() const {
  std::lock_guard lock(mutex_);
  if (returningToTitle_)
    throw std::runtime_error("Cannot save while returning to title");
  ::Pmail::StateIO::Writer out;
  out.string("PMAIL-GAME-2");
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
  return out.finish();
}

void PmailGame::deserialize(const std::string &data) {
  std::lock_guard lock(mutex_);
  ::Pmail::StateIO::Reader in(data);
  const auto version = in.string(32);
  if (version != "PMAIL-GAME-1" && version != "PMAIL-GAME-2")
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
  if (version == "PMAIL-GAME-2") {
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
  returningToTitle_ = false;
  ++sceneVersion_;
}

void PmailGame::returnToTitle() {
  std::lock_guard lock(mutex_);
  // Preserve persistent game flags, but discard suspended story call frames.
  vm_.reset(false);
  callStack_.clear();
  sprites_.clear();
  spriteSlots_.clear();
  animations_.clear();
  background_.clear();
  message_.clear();
  speakerName_.clear();
  messageQueue_.clear();
  choiceText_.clear();
  choiceValue_.clear();
  choosing_ = false;
  audioWait_ = AudioWait::None;
  waitUntil_ = 0;
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
    std::cout << "[PmailPort] TEXT: " << utf << std::endl;
    messageQueue_.push_back(std::move(utf));
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

void PmailGame::advanceAnimation(uint32_t elapsedMs) {
  std::lock_guard lock(mutex_);
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
  if (waiting_ || choosing_ || animating())
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
  if (i < choiceValue_.size())
    vm_.setVar(choiceVar_, uint32_t(choiceValue_[i]));
  choosing_ = false;
  choiceText_.clear();
  choiceValue_.clear();
  vm_.unblock();
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
  case 1:
  case 4:
  case 7: {
    flushLines();
    // sys 4 carries text as string args in PMAIL; avoid duplicating if already
    // queued.
    bool haveQueued = !message_.empty() || !messageQueue_.empty();
    if (!haveQueued) {
      std::string text;
      for (size_t i = 0; i < args.size(); ++i) {
        if (args[i].tag() == 2) {
          std::string s = vm.text(args[i]);
          if (!s.empty()) {
            if (!text.empty())
              text += "\n";
            text += s;
          }
        }
      }
      if (!text.empty()) {
        std::string utf = text;
        try {
          utf = ::Kotonoha::ShiftJisToUtf8(text);
        } catch (...) {
        }
        std::cout << "[PmailPort] TEXT: " << utf << std::endl;
        messageQueue_.push_back(std::move(utf));
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
  case 2:
  case 3:
  case 23:
    message_.clear();
    speakerName_.clear();
    break;
  case 5:
    for (const auto &a : args)
      if (a.tag() == 2)
        speakerName_ = ::Kotonoha::ShiftJisToUtf8(vm.text(a));
    break;
  case 9: {
    // Choice: args include option strings and a target var ref.
    choiceText_.clear();
    choiceValue_.clear();
    choiceVar_ = 0;
    for (const auto &a : args) {
      if (a.tag() == 2)
        choiceText_.push_back(::Kotonoha::ShiftJisToUtf8(vm.text(a)));
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
      callStack_.push_back({scriptName_, vm.returnPc()});
      loadScript(name, true);
    }
    break;
  }
  case 28: {
    std::string name = firstString();
    if (!name.empty()) {
      callStack_.push_back({scriptName_, vm.returnPc()});
      loadScript(name, true);
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
  case 64:
  case 69:
    if (id == 69) {
      int c[3] = {0, 0, 0}, n = 0;
      for (const auto &a : args)
        if (a.tag() == 0 && n < 3)
          c[n++] = std::clamp(int(a.value()), 0, 255);
      background_.clear();
      sprites_.clear();
      animations_.clear();
      filled_ = true;
      fillColor_ = uint32_t(c[0] << 16 | c[1] << 8 | c[2]);
      ++sceneVersion_;
    }
    // Transitions not fully timed yet; do not block.
    break;
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
  case 132:
  case 99:
  case 100:
  case 115:
  case 116:
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
  case 214: {
    // sprite/resource registration/preload: (slot, name, ?, ?)
    int slot = argInt(0);
    std::string name = args.size() > 1 ? vm.text(args[1]) : firstString();
    if (slot >= 0 && !name.empty())
      spriteSlots_[slot] = name;
    break;
  }
  default:
    break;
  }
}

void PmailGame::log(const std::string &s) {
  if (trace)
    trace("! " + s);
}

} // namespace Kotonoha::PmailPort
