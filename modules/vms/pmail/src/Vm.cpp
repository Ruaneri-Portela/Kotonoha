#include "Pmail/Vm.hpp"
#include "Pmail/StateIO.hpp"
#include <algorithm>
#include <array>
#include <cctype>
#include <cstdlib>
#include <cstring>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <vector>

#ifdef PMAIL_VM_TRACE
#define PMAIL_VM_DEBUG std::cout
#else
#define PMAIL_VM_DEBUG                                                         \
  if (false)                                                                   \
  std::cout
#endif

namespace Pmail {

namespace {
uint32_t rd32(const std::vector<uint8_t> &d, size_t o) {
  return o + 4 <= d.size() ? d[o] | (d[o + 1] << 8) | (d[o + 2] << 16) |
                                 (uint32_t(d[o + 3]) << 24)
                           : 0;
}
uint16_t rd16(const std::vector<uint8_t> &d, size_t o) {
  return o + 2 <= d.size() ? uint16_t(d[o] | (d[o + 1] << 8)) : 0;
}
} // namespace

size_t ScriptObject::parse(const std::string &objName,
                           std::vector<uint8_t> bytes) {
  name = objName;
  data = std::move(bytes);
  codeOffset = 0;
  codeWords = 0;
  strings.clear();
  id = 0;

  // The decoded PMAIL object begins with a 16-bit user-table offset and a
  // 16-bit string count. Each string descriptor is {offset, length}, both
  // little-endian 16-bit values. start.obj has 251 descriptors (0x3f0 bytes).
  size_t base = 0;
  if (data.size() >= 0x20 && std::memcmp(data.data(), "STACKFile:", 10) == 0)
    base = 0x20;
  if (data.size() < base + 8)
    return 0;
  const uint16_t usrOff = rd16(data, base);
  const uint16_t count = rd16(data, base + 2);
  if (usrOff < 4 || base + usrOff + 4 > data.size() ||
      size_t(count) * 4 + 4 > usrOff)
    return 0;
  const uint32_t tableSize = rd32(data, base + usrOff);
  if (tableSize < 4 || tableSize > data.size() - base - usrOff)
    return 0;
  codeOffset = uint32_t(base + usrOff + tableSize);
  codeWords = (data.size() - codeOffset) / 4;
  if (codeWords == 0)
    return 0;
  id = rd16(data, data.size() - 4);
  strings.reserve(count);
  for (uint16_t i = 0; i < count; ++i) {
    const size_t descriptor = base + 4 + size_t(i) * 4;
    const size_t offset = base + rd16(data, descriptor);
    const size_t length = rd16(data, descriptor + 2);
    if (offset > data.size() || length > data.size() - offset) {
      strings.emplace_back();
      continue;
    }
    size_t actual = 0;
    while (actual < length && data[offset + actual] != 0)
      ++actual;
    strings.emplace_back(reinterpret_cast<const char *>(data.data() + offset),
                         actual);
  }

  PMAIL_VM_DEBUG << "[PmailVm Debug] ScriptObject::parse: name=" << name
                 << ", id=0x" << std::hex << id << std::dec << ", base=" << base
                 << ", codeOffset=" << codeOffset << ", codeWords=" << codeWords
                 << ", data_size=" << data.size() << std::endl;

  return data.size();
}

uint32_t ScriptObject::word(size_t pc) const {
  return pc < codeWords ? rd32(data, codeOffset + pc * 4) : 0;
}

const std::string &ScriptObject::str(size_t idx) const {
  static const std::string empty;
  return idx < strings.size() ? strings[idx] : empty;
}

Vm::Vm(Host &host) : host_(host), vars_(0x10000, 0) {}

void Vm::reset(bool clearVariables) {
  if (clearVariables)
    vars_.assign(0x10000, 0);
  stack_.clear();
  lines_.clear();
  known_.clear();
  cur_.reset();
  for (auto &slot : usr_)
    slot = {};
  pc_ = acc_ = curFramePc_ = 0;
  rng_ = 12345;
  blocked_ = false;
  ended_ = true;
}

std::string Vm::serialize() const {
  StateIO::Writer out;
  out.string("PMAIL-VM-1");
  auto object = [&](const ObjectPtr &obj) {
    out.boolean(bool(obj));
    if (!obj)
      return;
    out.string(obj->name);
    out.u32(obj->id);
    out.u64(StateIO::fingerprint(obj->data.data(), obj->data.size()));
  };
  out.u32(uint32_t(vars_.size()));
  for (auto v : vars_)
    out.u32(v);
  out.u32(uint32_t(known_.size()));
  for (const auto &[id, obj] : known_) {
    out.u32(id);
    object(obj);
  }
  object(cur_);
  out.u32(pc_);
  out.u32(acc_);
  out.u32(curFramePc_);
  out.u32(rng_);
  out.boolean(blocked_);
  out.boolean(ended_);
  out.u32(uint32_t(stack_.size()));
  for (const auto &entry : stack_) {
    out.u32(entry.word);
    object(entry.obj);
  }
  out.u32(uint32_t(lines_.size()));
  for (const auto &line : lines_) {
    out.u32(line.object);
    out.u32(line.index);
  }
  for (const auto &slot : usr_) {
    out.boolean(slot.valid);
    object(slot.obj);
    out.u32(uint16_t(slot.argc));
    out.u32(uint16_t(slot.pc));
  }
  return out.finish();
}

void Vm::deserialize(const std::string &data) {
  StateIO::Reader in(data);
  if (in.string(32) != "PMAIL-VM-1")
    throw std::runtime_error("Unsupported PMAIL VM save version");
  auto object = [&]() -> ObjectPtr {
    if (!in.boolean())
      return {};
    const auto name = in.string(256);
    const auto id = in.u32();
    const auto hash = in.u64();
    auto obj = host_.loadByName(name);
    if (!obj || obj->id != id ||
        StateIO::fingerprint(obj->data.data(), obj->data.size()) != hash)
      throw std::runtime_error(
          "PMAIL saved script differs from installed game: " + name);
    return obj;
  };
  std::vector<uint32_t> vars(in.count(0x10000));
  if (vars.size() != 0x10000)
    throw std::runtime_error("Invalid PMAIL variable banks");
  for (auto &v : vars)
    v = in.u32();
  std::map<uint32_t, ObjectPtr> known;
  const auto knownCount = in.count(4096);
  for (uint32_t i = 0; i < knownCount; ++i) {
    const auto id = in.count(4095);
    const auto obj = object();
    if (!obj || (obj->id & 0xfff) != id || !known.emplace(id, obj).second)
      throw std::runtime_error("Invalid PMAIL object registry");
  }
  auto current = object();
  const auto pc = in.u32(), acc = in.u32(), framePc = in.u32(), rng = in.u32();
  const bool blocked = in.boolean(), ended = in.boolean();
  if ((!current && !ended) || (current && pc > current->codeWords))
    throw std::runtime_error("Invalid PMAIL program counter");
  std::vector<Entry> stack(in.count(65536));
  for (auto &entry : stack) {
    entry.word = in.u32();
    entry.obj = object();
    if (entry.obj && ((entry.word >> 16) & 15) == 4 &&
        (entry.word & 0xffff) > entry.obj->codeWords)
      throw std::runtime_error("Invalid PMAIL return frame");
  }
  std::vector<Line> lines(in.count(128));
  for (auto &line : lines) {
    line.object = in.count(4095);
    line.index = in.count(65535);
    const auto it = known.find(line.object);
    if (it == known.end() || line.index >= it->second->strings.size())
      throw std::runtime_error("Invalid PMAIL queued text");
  }
  std::array<UsrSlot, 256> slots;
  for (auto &slot : slots) {
    slot.valid = in.boolean();
    slot.obj = object();
    slot.argc = int16_t(in.count(65535));
    slot.pc = int16_t(in.count(65535));
    if (slot.valid &&
        (!slot.obj || slot.pc < 0 || size_t(slot.pc) >= slot.obj->codeWords))
      throw std::runtime_error("Invalid PMAIL user function");
  }
  in.end();
  // No interpreter fields change until all bytes and object identities
  // validate.
  vars_ = std::move(vars);
  known_ = std::move(known);
  cur_ = std::move(current);
  stack_ = std::move(stack);
  lines_ = std::move(lines);
  std::copy(slots.begin(), slots.end(), std::begin(usr_));
  pc_ = pc;
  acc_ = acc;
  curFramePc_ = framePc;
  rng_ = rng;
  blocked_ = blocked;
  ended_ = ended;
}

void Vm::swapState(Vm &other) noexcept {
  using std::swap;
  swap(vars_, other.vars_);
  swap(stack_, other.stack_);
  swap(lines_, other.lines_);
  swap(known_, other.known_);
  swap(cur_, other.cur_);
  for (size_t i = 0; i < 256; ++i)
    swap(usr_[i], other.usr_[i]);
  swap(pc_, other.pc_);
  swap(acc_, other.acc_);
  swap(curFramePc_, other.curFramePc_);
  swap(rng_, other.rng_);
  swap(blocked_, other.blocked_);
  swap(ended_, other.ended_);
}

void Vm::registerObject(const ObjectPtr &obj) {
  if (!obj)
    return;
  known_[obj->id & 0xfff] = obj;

  // pmail.exe:0x402066-0x40210f copies each 8-byte user function record
  // into a 256-entry dispatch table. The first byte is the slot, followed
  // by three signed 16-bit values (argument count and entry PC among them).
  const auto &data = obj->data;
  if (data.size() < 8)
    return;
  size_t base = 0;
  if (data.size() >= 0x20 && std::memcmp(data.data(), "STACKFile:", 10) == 0)
    base = 0x20;
  const size_t table = base + rd16(data, base);
  if (table + 4 > data.size())
    return;
  const uint32_t tableSize = rd32(data, table);
  if (tableSize < 4 || tableSize > data.size() - table ||
      (tableSize - 4) % 8 != 0)
    return;
  for (size_t p = table + 4; p + 8 <= table + tableSize; p += 8) {
    const uint8_t slot = data[p];
    UsrSlot &u = usr_[slot];
    u.valid = true;
    u.obj = obj;
    u.argc = int16_t(rd16(data, p + 2));
    u.pc = int16_t(rd16(data, p + 4));
  }
}

void Vm::unregisterObject(const ObjectPtr &obj) {
  for (auto &u : usr_) {
    if (u.valid && u.obj == obj)
      u = {};
  }
}

void Vm::start(const ObjectPtr &obj, uint32_t pc) {
  PMAIL_VM_DEBUG << "[PmailVm Debug] start called: obj="
                 << (obj ? obj->name : "null") << ", pc=" << pc
                 << ", size=" << (obj ? obj->data.size() : 0) << std::endl;
  cur_ = obj;
  pc_ = pc;
  stack_.clear();
  lines_.clear();
  acc_ = 0;
  blocked_ = false;
  ended_ = false;
}

void Vm::jumpTo(const ObjectPtr &obj, uint32_t pc) {
  cur_ = obj;
  pc_ = pc;
}

void Vm::setVar(uint32_t id, uint32_t v) {
  // Match SnowRadish-like variable widths by bank.
  switch ((id >> 12) & 0xf) {
  case 0:
    v &= 1;
    break;
  case 1:
    v = uint32_t(int32_t(int8_t(v)));
    break;
  case 2:
    v &= 0xff;
    break;
  case 3:
    v = uint32_t(int32_t(int16_t(v)));
    break;
  default:
    v &= 0xffff;
    break;
  }
  const uint32_t idx = id & 0xffff;
  if (idx >= vars_.size())
    vars_.resize(idx + 1, 0);
  vars_[idx] = v;
}

int32_t Vm::value(const Arg &a) const {
  if (a.tag() == 1)
    return int32_t(var(a.value()));
  return a.value();
}

ObjectPtr Vm::findObject(uint32_t id12) {
  if (cur_ && (cur_->id & 0xfff) == id12)
    return cur_;
  auto it = known_.find(id12);
  if (it != known_.end())
    return it->second;
  ObjectPtr o = host_.loadById(id12);
  if (o)
    registerObject(o);
  return o;
}

std::string Vm::textAt(uint32_t object12, uint32_t idx) {
  ObjectPtr o = findObject(object12);
  return o ? o->str(idx) : std::string();
}

std::string Vm::text(const Arg &a) {
  return a.tag() == 2 ? textAt(a.object(), a.value()) : std::string();
}

uint32_t Vm::peek(size_t n) const {
  return n >= 1 && n <= stack_.size() ? stack_[stack_.size() - n].word : 0;
}

int32_t Vm::evalExpr(const std::string &e) {
  std::vector<int32_t> st;
  std::istringstream in(e);
  std::string t;
  auto pop = [&]() {
    if (st.empty())
      return int32_t(0);
    int32_t v = st.back();
    st.pop_back();
    return v;
  };
  while (in >> t) {
    if (!t.empty() && t[0] == '$') {
      st.push_back(int32_t(var(uint32_t(std::atoi(t.c_str() + 1)))));
    } else if (uint8_t(t[0]) == 0xff && t.size() >= 3) {
      uint32_t v = peek(size_t(std::atoi(t.c_str() + 2))) & 0xffff;
      if (t[1] == 'F')
        v = var(v);
      st.push_back(int32_t(v));
    } else if (t == "@") {
      rng_ = rng_ * 1103515245u + 12345u;
      st.push_back(int32_t((rng_ >> 16) & 0x7fff));
    } else if (std::isdigit(uint8_t(t[0])) || (t[0] == '-' && t.size() > 1)) {
      st.push_back(std::atoi(t.c_str()));
    } else {
      const int32_t b = pop(), a = pop();
      const uint32_t ua = uint32_t(a), sh = uint32_t(b) & 31;
      int32_t r = 0;
      if (t == "*")
        r = a * b;
      else if (t == "/")
        r = b ? a / b : 0;
      else if (t == "%")
        r = b ? a % b : 0;
      else if (t == "+")
        r = a + b;
      else if (t == "-")
        r = a - b;
      else if (t == ">>")
        r = a >> sh;
      else if (t == "<<")
        r = int32_t(ua << sh);
      else if (t == ">>>")
        r = int32_t(sh ? (ua >> sh) | (ua << (32 - sh)) : ua);
      else if (t == "<<<")
        r = int32_t(sh ? (ua << sh) | (ua >> (32 - sh)) : ua);
      else if (t == ">")
        r = a > b;
      else if (t == "<")
        r = a < b;
      else if (t == ">=")
        r = a >= b;
      else if (t == "<=")
        r = a <= b;
      else if (t == "==")
        r = a == b;
      else if (t == "!=")
        r = a != b;
      else if (t == "&")
        r = a & b;
      else if (t == "^")
        r = a ^ b;
      else if (t == "|")
        r = a | b;
      else if (t == "&&")
        r = a && b;
      else if (t == "||")
        r = a || b;
      st.push_back(r);
    }
  }
  return st.empty() ? 0 : st.back();
}

bool Vm::step() {
  if (ended_ || !cur_)
    return false;

  // PMAIL/SnowRadish bytecode is word-addressed (4 bytes per instruction).
  // The legacy implementation used byte addressing; prefer codeOffset/codeWords
  // when available, but keep a fallback for transitional parses.
  uint32_t w = 0;
  if (cur_->codeWords > 0 && pc_ < cur_->codeWords) {
    w = cur_->word(pc_++);
  } else if (cur_->codeWords == 0 && pc_ + 4 <= cur_->data.size()) {
    // Fallback: treat pc_ as byte offset (legacy)
    w = rd32(cur_->data, pc_);
    pc_ += 4;
  } else {
    PMAIL_VM_DEBUG << "[PmailVm Debug] step: PC out of bounds! pc=" << pc_
                   << ", codeWords=" << cur_->codeWords
                   << ", dataSize=" << cur_->data.size() << std::endl;
    ended_ = true;
    return false;
  }

  PMAIL_VM_DEBUG << "[PmailVm Debug] instruction pc(word)=" << (pc_ - 1)
                 << ": 0x" << std::hex << std::setw(8) << std::setfill('0') << w
                 << std::dec << std::endl;

  const uint32_t mode = (w >> 16) & 0xff, imm = w & 0xffff, op = w >> 24;
  uint32_t val = 0;
  int kind = 0; // 3 forward, 4 backward, 1 variable, 2 string, 5 raw word
  uint32_t argc = 0;
  switch (mode) {
  case 0:
    val = 0;
    break;
  case 1:
    val = 1;
    break;
  case 2:
    val = (w >> 8) & 0xff;
    break;
  case 3:
    val = imm;
    break;
  case 4:
    val = (w >> 8) & 0xff;
    argc = w & 0xff;
    break;
  case 5:
    val = uint32_t(evalExpr(cur_->str(imm)));
    kind = 5;
    break;
  case 0x10:
    val = imm;
    kind = 3;
    break;
  case 0x11:
    val = imm;
    kind = 4;
    break;
  case 0x20:
    val = imm;
    kind = 1;
    break;
  case 0x40:
  case 0x42:
  case 0x44:
    val = peek(imm);
    kind = 5;
    break;
  case 0x41:
  case 0x43:
  case 0x45:
    val = 0;
    kind = 5;
    break;
  case 0x80:
    val = imm;
    kind = 2;
    break;
  default:
    break;
  }
  const uint32_t v16 = val & 0xffff;
  const uint32_t acc16 = acc_ & 0xffff;
  const uint32_t objBits = uint32_t(cur_->id) << 20;

  auto jump = [&]() {
    if (kind == 3)
      pc_ += v16;
    if (kind == 4)
      pc_ -= v16;
  };

  switch (op) {
  case 0x00:
    PMAIL_VM_DEBUG << "[PmailVm Debug] HALT/END" << std::endl;
    pc_ = 0;
    acc_ = 0;
    ended_ = true;
    return false;
  case 0x01:
    jump();
    break;
  case 0x02:
    if (acc16)
      jump();
    break;
  case 0x03:
    if (!acc16)
      jump();
    break;
  case 0x04: { // return
    if (stack_.empty()) {
      ended_ = true;
      return false;
    }
    Entry e = stack_.back();
    stack_.pop_back();
    if (((e.word >> 16) & 0xf) == 4)
      pc_ = e.word & 0xffff;
    if (e.obj)
      cur_ = e.obj;
    break;
  }
  case 0x05: { // native/sys call
    PMAIL_VM_DEBUG << "[PmailVm Debug] CALL_NATIVE(sys) id=" << val
                   << " argc=" << argc << std::endl;
    stack_.push_back({pc_ | 0x40000, cur_});
    curFramePc_ = pc_;
    std::vector<Arg> args;
    args.reserve(argc);
    // Args are expected on stack by convention in SnowRadish; gather
    // best-effort.
    for (uint32_t i = 0; i < argc; ++i) {
      const size_t idx =
          stack_.size() > (argc + 1 - i) ? stack_.size() - (argc + 1 - i) : 0;
      args.push_back({idx < stack_.size() ? stack_[idx].word : 0});
    }
    host_.native(*this, int(val), args);
    // Pop frame marker if host didn't consume it.
    if (!stack_.empty() && ((stack_.back().word >> 16) & 0xf) == 4 &&
        (stack_.back().word & 0xffff) == curFramePc_)
      stack_.pop_back();
    break;
  }
  case 0x06: { // usr call
    PMAIL_VM_DEBUG << "[PmailVm Debug] CALL_USR id=" << (val & 0xff)
                   << std::endl;
    UsrSlot &s = usr_[val & 0xff];
    if (!s.valid) {
      host_.log("usr " + std::to_string(val) + " missing");
      break;
    }
    stack_.push_back({pc_ | 0x40000, cur_});
    cur_ = s.obj;
    pc_ = uint32_t(s.pc);
    break;
  }
  case 0x09:
    if (((acc_ >> 16) & 0xf) == 2) {
      lines_.push_back({acc_ >> 20, acc16});
      if (lines_.size() > 128)
        lines_.erase(lines_.begin());
    }
    break;
  case 0x0a:
    lines_.clear();
    break;
  case 0x10: // store to var (when kind==1)
    PMAIL_VM_DEBUG << "[PmailVm Debug] STORE op=0x10 kind=" << kind
                   << " val=" << v16 << " acc=" << acc16 << std::endl;
    if (kind == 1)
      setVar(v16, acc16);
    break;
  case 0x11: // load/set accumulator based on kind
    PMAIL_VM_DEBUG << "[PmailVm Debug] LOAD_ACC op=0x11 kind=" << kind
                   << " val=" << v16 << std::endl;
    switch (kind) {
    case 0:
      acc_ = v16 | objBits;
      break;
    case 1:
      acc_ = v16 | 0x10000 | objBits;
      break;
    case 2:
      acc_ = v16 | 0x20000 | objBits;
      break;
    case 5:
      acc_ = val;
      break;
    default:
      break;
    }
    break;
  case 0x20:
    PMAIL_VM_DEBUG << "[PmailVm Debug] PUSH_STACK op=0x20 acc=0x" << std::hex
                   << acc_ << std::dec << std::endl;
    stack_.push_back({acc_, nullptr});
    break;
  case 0x21:
    PMAIL_VM_DEBUG << "[PmailVm Debug] POP_STACK op=0x21 count=" << v16
                   << std::endl;
    for (uint32_t i = 0; i < v16 && !stack_.empty(); ++i) {
      acc_ = stack_.back().word;
      stack_.pop_back();
    }
    break;
  default:
    PMAIL_VM_DEBUG << "[PmailVm Debug] NOP (Unknown Opcode Bypass) op=0x"
                   << std::hex << int(op) << " mode=0x" << int(mode)
                   << " imm=0x" << imm << std::dec << " kind=" << kind
                   << " val=" << val << std::endl;
    break;
  }

  return !ended_;
}

size_t Vm::run(size_t maxSteps) {
  size_t n = 0;
  while (n < maxSteps && !blocked_ && step())
    ++n;
  return n;
}

} // namespace Pmail
