#include "Pmail/Port.hpp"
#include "PmailPack.hpp"

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
  if (!packPath_.empty()) {
    packReader_ = std::make_unique<PmailPack>();
    if (!packReader_->open(packPath_)) {
      std::cerr << "[PmailPort] PmailPack open failed, falling back to scan: "
                << packPath_ << std::endl;
      packReader_.reset();
    }
  }
  indexPack();
}

PmailGame::~PmailGame() = default;

void PmailGame::indexPack() {
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
  ::Pmail::ObjectPtr obj = loadByName(name);
  if (!obj)
    return false;
  if (asMain) {
    if (main_)
      vm_.unregisterObject(main_);
    main_ = obj;
  }
  scriptName_ = obj->name;
  vm_.jumpTo(obj, 0);
  return true;
}

bool PmailGame::init() {
  // Common PMAIL entry scripts vary; try likely names.
  const char *candidates[] = {"start.obj", "pmail.obj", "main.obj",
                              "01m1.obj",  "a.obj",     nullptr};
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

void PmailGame::flushLines() {
  auto &lines = vm_.lines();
  if (lines.empty())
    return;
  for (const auto &l : lines) {
    std::string s = vm_.textAt(l.object, l.index);
    if (!s.empty()) {
      // Best-effort Shift-JIS -> UTF-8 for display/logging.
      std::string utf = s;
      try {
        utf = ::Kotonoha::ShiftJisToUtf8(s);
      } catch (...) {
      }
      messageQueue_.push_back(utf);
      std::cout << "[PmailPort] TEXT: " << utf << std::endl;
    }
  }
  lines.clear();
  if (!messageQueue_.empty()) {
    message_ = std::move(messageQueue_.front());
    messageQueue_.pop_front();
  }
}

void PmailGame::pump(size_t maxSteps) {
  if (waiting_)
    return;
  if (waitingForSe_) {
    if (sePlaying && sePlaying())
      return;
    waitingForSe_ = false;
    vm_.unblock();
  }
  if (waitingForTime_) {
    if (SDL_GetTicks() < waitUntil_)
      return;
    waitingForTime_ = false;
    vm_.unblock();
  }
  vm_.run(maxSteps);
  if (vm_.ended()) {
    // End of script: stop waiting.
    waiting_ = false;
  }
}

bool PmailGame::click() {
  if (!waiting_)
    return false;
  if (title_) {
    vm_.setVar(titleChoiceVar_, 1); // original title: Start
    title_ = false;
  } else {
    if (!messageQueue_.empty()) {
      message_ = std::move(messageQueue_.front());
      messageQueue_.pop_front();
      return true;
    }
    message_.clear();
  }
  waiting_ = false;
  vm_.unblock();
  return true;
}

void PmailGame::native(::Pmail::Vm &vm, int id,
                       const std::vector<::Pmail::Arg> &args) {
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
  if (onNative)
    onNative(id, args);

  auto argStr = [&](size_t i) -> std::string {
    return i < args.size() ? vm.text(args[i]) : std::string();
  };
  auto argInt = [&](size_t i) -> int {
    return i < args.size() ? int(vm.value(args[i])) : 0;
  };

  // Conservative defaults: do not block forever on unknown natives.
  // Specific PMAIL syscalls must be implemented from RE for full playability.
  switch (id) {
  case 1:
  case 4:
  case 7: {
    flushLines();
    std::string text;
    // sys 4 seen as (type, text, ?, ?) - collect string args as message.
    for (size_t i = 0; i < args.size(); ++i) {
      if (args[i].tag() == 2) {
        std::string s = vm.text(args[i]);
        if (!s.empty()) {
          if (!text.empty()) text += "\n";
          text += s;
        }
      }
    }
    if (!text.empty()) {
      std::string utf = text;
      try { utf = ::Kotonoha::ShiftJisToUtf8(text); } catch (...) {}
      messageQueue_.push_back(utf);
      if (message_.empty()) {
        message_ = std::move(messageQueue_.front());
        messageQueue_.pop_front();
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
  case 131:
  case 133:
    // text/window control (best-effort no-op for now)
    message_.clear();
    break;
  case 16:
    for (const auto &arg : args)
      if (arg.tag() == 2 && loadScript(vm.text(arg), true))
        break;
    break;
  case 17:
  case 18:
  case 19:
    // title/menu related markers
    break;
  case 33: {
    int duration = argInt(0);
    if (duration > 0) {
      waitUntil_ = SDL_GetTicks() + uint64_t(std::min(duration, 60000));
      waitingForTime_ = true;
      vm.block();
    }
    break;
  }
  case 48:
    for (const auto &arg : args)
      if (arg.tag() == 2) {
        background_ = vm.text(arg);
        break;
      }
    break;
  case 64:
  case 69:
    // transition/effect (visual sync not fully implemented)
    break;
  case 99:
  case 100:
  case 115:
  case 116:
  case 132:
    // stop audio channels (handled via onNative/runtime hooks too)
    break;
  case 101:
    if (sePlaying && sePlaying()) {
      waitingForSe_ = true;
      vm.block();
    }
    break;
  case 213:
    for (const auto &arg : args)
      if (arg.tag() == 1)
        titleChoiceVar_ = arg.value();
    // Default to "Start" (1) so the script can progress; real UI can override.
    vm_.setVar(titleChoiceVar_, 1);
    title_ = true;
    waiting_ = true;
    vm.block();
    break;
  case 214:
    // sprite/image registration (resource preload) - not yet implemented
    break;
  default:
    break;
  }
}

void PmailGame::log(const std::string &s) {
  if (trace)
    trace("! " + s);
}

} // namespace Kotonoha::PmailPort
