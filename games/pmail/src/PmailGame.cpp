#include "Pmail/Port.hpp"

#include <Kotonoha/localization/Encoding.hpp>

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

PmailGame::PmailGame(std::vector<uint8_t> scriptPack)
    : pack_(std::move(scriptPack)), vm_(*this) {
  indexPack();
}

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
  // Lazy index by loading names
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
      if (!message_.empty())
        message_ += "\n";
      message_ += utf;
      std::cout << "[PmailPort] TEXT: " << utf << std::endl;
    }
  }
  lines.clear();
}

void PmailGame::pump(size_t maxSteps) {
  if (waiting_)
    return;
  vm_.run(maxSteps);
  flushLines();
  if (vm_.ended()) {
    // End of script: stop waiting.
    waiting_ = false;
  }
}

bool PmailGame::click() {
  if (!waiting_)
    return false;
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
      t += std::to_string(args[i].raw);
    }
    t += ")";
    trace(t);
  }
  std::cout << "[PmailPort] native sys id=" << id << " argc=" << args.size()
            << std::endl;
  if (onNative)
    onNative(id, args);

  // Conservative defaults: do not block forever on unknown natives.
  // Specific PMAIL syscalls (text, wait, graphics, sound, choices, etc.)
  // must be implemented from RE for full playability.
  switch (id) {
  case 0:
    break;
  default:
    break;
  }
}

void PmailGame::log(const std::string &s) {
  std::cout << "[PmailPort][VM] " << s << std::endl;
  if (trace)
    trace("! " + s);
}

} // namespace Kotonoha::PmailPort
