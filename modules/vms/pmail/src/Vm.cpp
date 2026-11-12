#include "Pmail/Vm.hpp"
#include <iostream>
#include <algorithm>
#include <vector>

namespace Pmail {

namespace {
uint8_t read_u8(const std::vector<uint8_t>& data, size_t& pc) {
    if (pc >= data.size()) return 0;
    return data[pc++];
}

uint16_t read_u16(const std::vector<uint8_t>& data, size_t& pc) {
    if (pc + 2 > data.size()) return 0;
    uint16_t val = data[pc] | (data[pc + 1] << 8);
    pc += 2;
    return val;
}

uint32_t read_u32(const std::vector<uint8_t>& data, size_t& pc) {
    if (pc + 4 > data.size()) return 0;
    uint32_t val = data[pc] | (data[pc + 1] << 8) | (data[pc + 2] << 16) | (uint32_t(data[pc + 3]) << 24);
    pc += 4;
    return val;
}
} // namespace

bool ScriptObject::parse(const std::string &objName, std::vector<uint8_t> bytes) {
  name = objName;
  data = std::move(bytes);
  if (data.size() < 12) return false;

  auto it_k = std::search(data.begin(), data.end(), "kPM7", "kPM7" + 4);
  auto it_p = std::search(data.begin(), data.end(), "pm7", "pm7" + 3);
  auto it_magic = (it_k != data.end()) ? it_k : it_p;

  if (it_magic == data.end()) return false;
  size_t magic_pos = std::distance(data.begin(), it_magic);

  // Find filename (after magic, could be preceded by a null)
  size_t start_search = magic_pos + 4;
  size_t end_of_name = start_search;
  while (end_of_name < data.size() && data[end_of_name] != 0) {
    end_of_name++;
  }
  
  size_t name_start = start_search;
  if (name_start < data.size() && data[name_start] == 0) {
    name_start++;
  }
  
  if (name_start < end_of_name) {
    name = std::string(data.begin() + name_start, data.begin() + end_of_name);
  } else {
    name = "";
  }

  // Find code by searching for potential opcodes
  std::vector<uint8_t> potential_opcodes = {0x73, 0x03, 0x7b, 0x7d, 0x7f, 0x80, 0x71};
  size_t code_start = data.size();
  for (size_t i = end_of_name + 1; i < data.size(); ++i) {
    for (auto op : potential_opcodes) {
      if (data[i] == op) {
        code_start = i;
        goto found;
      }
    }
  }

found:
  if (code_start == data.size()) return false;

  codeOffset = code_start;
  codeWords = (data.size() - codeOffset) / 4;

  // Extract strings from metadata (between end_of_name and code_start)
  strings.clear();
  size_t metadata_end = code_start;
  size_t metadata_start = end_of_name;
  
  // Skip any nulls after filename
  while (metadata_start < metadata_end && metadata_start < data.size() && data[metadata_start] == 0) {
    metadata_start++;
  }

  std::string current_string;
  for (size_t i = metadata_start; i < metadata_end; ++i) {
    if (data[i] >= 32 && data[i] <= 126) {
      current_string += (char)data[i];
    } else {
      if (current_string.length() >= 3) {
        strings.push_back(current_string);
      }
      current_string.clear();
    }
  }
  if (current_string.length() >= 3) {
    strings.push_back(current_string);
  }

  return true;
}

uint32_t ScriptObject::word(size_t pc) const {
  if (pc + 4 <= data.size()) {
    return data[pc] | (data[pc + 1] << 8) | (data[pc + 2] << 16) | (uint32_t(data[pc + 3]) << 24);
  }
  return 0;
}

const std::string &ScriptObject::str(size_t idx) const {
  static const std::string empty;
  return idx < strings.size() ? strings[idx] : empty;
}

Vm::Vm(Host &host) : host_(host), vars_(0x1000, 0) {}

void Vm::registerObject(const ObjectPtr &obj) {
  if (obj) known_[obj->id & 0xfff] = obj;
}

void Vm::unregisterObject(const ObjectPtr &obj) {
  for (auto &u : usr_) {
    if (u.valid && u.obj == obj) u = {};
  }
}

void Vm::start(const ObjectPtr &obj, uint32_t pc) {
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
  if (id < vars_.size()) {
    vars_[id] = v;
  }
}

int32_t Vm::value(const Arg &a) const {
  if (a.tag() == 1) return int32_t(var(a.value()));
  return a.value();
}

std::string Vm::text(const Arg &a) {
  return a.tag() == 2 ? textAt(a.object(), a.value()) : std::string();
}

std::string Vm::textAt(uint32_t object12, uint32_t idx) {
  ObjectPtr o = findObject(object12);
  return o ? o->str(idx) : std::string();
}

ObjectPtr Vm::findObject(uint32_t id12) {
  if (cur_ && (cur_->id & 0xfff) == id12) return cur_;
  auto it = known_.find(id12);
  if (it != known_.end()) return it->second;
  return host_.loadById(id12);
}

bool Vm::step() {
  if (ended_ || !cur_) return false;

  const auto& data = cur_->data;
  if (pc_ + 4 > data.size()) {
    ended_ = true;
    return false;
  }

  uint32_t ins = data[pc_] | (data[pc_ + 1] << 8) | (data[pc_ + 2] << 16) | (uint32_t(data[pc_ + 3]) << 24);
  pc_ += 4;

  uint8_t op = ins & 0xFF;
  uint8_t mode = (ins >> 8) & 0xFF;
  uint16_t imm = ins >> 16;

  switch (op) {
    case 0x00: // HALT
      ended_ = true;
      break;

    case 0x03: { // JUMP / BRANCH
      if (mode == 0) {
        pc_ += imm;
      } else if (mode == 1) { // JUMP IF ACC != 0
        if (acc_ != 0) pc_ += imm;
      } else if (mode == 2) { // JUMP IF ACC == 0
        if (acc_ == 0) pc_ += imm;
      }
      break;
    }

    case 0x73: { // LOAD_CONST / SET_REG
      if (mode == 0) acc_ = imm;
      else setVar(mode, imm);
      break;
    }

    case 0x71: { // LOAD_ADDR
      acc_ = imm;
      break;
    }

    case 0x7b: { // STORE_8
      setVar(mode, imm & 0xFF);
      break;
    }
    case 0x7d: { // STORE_16
      setVar(mode, imm & 0xFFFF);
      break;
    }
    case 0x7f: { // STORE_32
      if (pc_ + 4 <= cur_->data.size()) {
        // We need to access the anonymous namespace functions. 
        // Since we are in the same file, we can just use them.
        // But we need to pass pc_ by reference.
        // However, the anonymous namespace functions are not in the class.
        // Let's just inline the read_u32 logic to be safe.
        uint32_t val = cur_->data[pc_] | (cur_->data[pc_ + 1] << 8) | (cur_->data[pc_ + 2] << 16) | (uint32_t(cur_->data[pc_ + 3]) << 24);
        pc_ += 4;
        setVar(mode, val);
      }
      break;
    }
    case 0x80: { // STORE_ANY
      setVar(mode, imm);
      break;
    }

    default:
      // For now, just skip unknown opcodes
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
