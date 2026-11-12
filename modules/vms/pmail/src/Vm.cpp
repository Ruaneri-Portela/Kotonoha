#include "Pmail/Vm.hpp"
#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <vector>

namespace Pmail {

namespace {
uint32_t rd32(const std::vector<uint8_t> &d, size_t o) {
  return o + 4 <= d.size() ? d[o] | (d[o + 1] << 8) | (d[o + 2] << 16) |
                                 (uint32_t(d[o + 3]) << 24)
                           : 0;
}
} // namespace

size_t ScriptObject::parse(const std::string &objName,
                           std::vector<uint8_t> bytes) {
  name = objName;
  data = std::move(bytes);
  if (data.size() < 12)
    return 0;

  const uint8_t kpm7[] = {'k', 'P', 'M', '7'};
  const uint8_t pm7[] = {'p', 'm', '7'};
  auto it_k =
      std::search(data.begin(), data.end(), std::begin(kpm7), std::end(kpm7));
  auto it_p =
      std::search(data.begin(), data.end(), std::begin(pm7), std::end(pm7));
  auto it_magic = (it_k != data.end()) ? it_k : it_p;

  if (it_magic == data.end())
    return 0;

  size_t magic_pos = std::distance(data.begin(), it_magic);

  // Find filename (after magic, could be preceded by a null)
  size_t start_search = magic_pos + 4;

  // Skip any leading nulls after magic
  while (start_search < data.size() && data[start_search] == 0) {
    start_search++;
  }

  size_t name_start = start_search;
  size_t end_of_name = name_start;

  // Find the end of the name (the first null after the name starts)
  while (end_of_name < data.size() && data[end_of_name] != 0) {
    end_of_name++;
  }

  if (name_start < end_of_name) {
    name = std::string(data.begin() + name_start, data.begin() + end_of_name);
  } else {
    name = "";
    end_of_name = magic_pos + 4;
  }

  // Extract strings from metadata (between end_of_name and code_start)
  strings.clear();
  size_t metadata_start = end_of_name;

  // Skip any nulls after filename
  while (metadata_start < data.size() && data[metadata_start] == 0) {
    metadata_start++;
  }

  // Find the end of the metadata by looking for the last printable character
  size_t last_string_end = metadata_start;
  std::string current_string;
  for (size_t i = metadata_start; i < data.size(); ++i) {
    if (data[i] >= 32 && data[i] <= 126) {
      current_string += static_cast<char>(data[i]);
    } else {
      if (!current_string.empty()) {
        strings.push_back(current_string);
        last_string_end = i;
        current_string.clear();
      }
    }
  }
  if (!current_string.empty()) {
    strings.push_back(current_string);
    last_string_end = metadata_start + current_string.size();
  }

  // The code start is the first 4-byte aligned index after the last string.
  size_t code_start = last_string_end;
  if (code_start % 4 != 0) {
    code_start += (4 - (code_start % 4));
  }

  // If this heuristic fails (e.g. code_start is too small), fall back to
  // searching for opcodes.
  if (code_start >= data.size() || code_start < metadata_start) {
    std::vector<uint8_t> potential_opcodes = {0x73, 0x03, 0x7b, 0x7d,
                                              0x7f, 0x80, 0x71};
    code_start = data.size();
    for (size_t i = metadata_start; i + 3 < data.size(); ++i) {
      for (auto op : potential_opcodes) {
        if (data[i] == op) {
          code_start = i;
          goto found_fallback;
        }
      }
    }
  found_fallback:;
  }

  if (code_start >= data.size())
    return 0;

  std::cout << "[PmailVm Debug] ScriptObject::parse: name=" << name
            << ", code_start=" << code_start << ", data_size=" << data.size()
            << std::endl;

  codeOffset = code_start;
  codeWords = (data.size() - codeOffset) / 4;
  id = 0;
  if (data.size() >= 4) {
    // Best-effort: PMAIL object IDs are not fully reversed yet.
    // Keep a stable 12-bit-ish value if tail bytes look plausible.
    id = data[data.size() - 4] | (uint16_t(data[data.size() - 3]) << 8);
  }

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

void Vm::registerObject(const ObjectPtr &obj) {
  if (obj)
    known_[obj->id & 0xfff] = obj;
}

void Vm::unregisterObject(const ObjectPtr &obj) {
  for (auto &u : usr_) {
    if (u.valid && u.obj == obj)
      u = {};
  }
}

void Vm::start(const ObjectPtr &obj, uint32_t pc) {
  std::cout << "[PmailVm Debug] start called: obj="
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
    known_[id12] = o;
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
    std::cout << "[PmailVm Debug] step: PC out of bounds! pc=" << pc_
              << ", codeWords=" << cur_->codeWords
              << ", dataSize=" << cur_->data.size() << std::endl;
    ended_ = true;
    return false;
  }

  std::cout << "[PmailVm Debug] instruction pc(word)=" << (pc_ - 1) << ": 0x"
            << std::hex << std::setw(8) << std::setfill('0') << w << std::dec
            << std::endl;

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
    std::cout << "[PmailVm Debug] HALT/END" << std::endl;
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
    std::cout << "[PmailVm Debug] CALL_NATIVE(sys) id=" << val
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
    std::cout << "[PmailVm Debug] CALL_USR id=" << (val & 0xff) << std::endl;
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
    std::cout << "[PmailVm Debug] STORE op=0x10 kind=" << kind << " val=" << v16
              << " acc=" << acc16 << std::endl;
    if (kind == 1)
      setVar(v16, acc16);
    break;
  case 0x11: // load/set accumulator based on kind
    std::cout << "[PmailVm Debug] LOAD_ACC op=0x11 kind=" << kind
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
    std::cout << "[PmailVm Debug] PUSH_STACK op=0x20 acc=0x" << std::hex << acc_
              << std::dec << std::endl;
    stack_.push_back({acc_, nullptr});
    break;
  case 0x21:
    std::cout << "[PmailVm Debug] POP_STACK op=0x21 count=" << v16 << std::endl;
    for (uint32_t i = 0; i < v16 && !stack_.empty(); ++i) {
      acc_ = stack_.back().word;
      stack_.pop_back();
    }
    break;
  default:
    std::cout << "[PmailVm Debug] NOP (Unknown Opcode Bypass) op=0x" << std::hex
              << int(op) << " mode=0x" << int(mode) << " imm=0x" << imm
              << std::dec << " kind=" << kind << " val=" << val << std::endl;
    break;
  }

  return !ended_;
}

size_t Vm::run(size_t maxSteps) {
  size_t n = 0;
  while (n < maxSteps && !blocked_ && step()) {
    ++n;
  }
  return n;
}

} // namespace Pmail
