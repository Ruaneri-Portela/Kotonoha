#include "Pmail/Vm.hpp"
#include <algorithm>
#include <iostream>
#include <vector>
#include <iomanip>

namespace Pmail {

namespace {
uint8_t read_u8(const std::vector<uint8_t> &data, size_t &pc) {
  if (pc >= data.size())
    return 0;
  return data[pc++];
}

uint16_t read_u16(const std::vector<uint8_t> &data, size_t &pc) {
  if (pc + 2 > data.size())
    return 0;
  uint16_t val = data[pc] | (data[pc + 1] << 8);
  pc += 2;
  return val;
}

uint32_t read_u32(const std::vector<uint8_t> &data, size_t &pc) {
  if (pc + 4 > data.size())
    return 0;
  uint32_t val = data[pc] | (data[pc + 1] << 8) | (data[pc + 2] << 16) |
                 (uint32_t(data[pc + 3]) << 24);
  pc += 4;
  return val;
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
   auto it_k = std::search(data.begin(), data.end(), std::begin(kpm7), std::end(kpm7));
   auto it_p = std::search(data.begin(), data.end(), std::begin(pm7), std::end(pm7));
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

   // If this heuristic fails (e.g. code_start is too small), fall back to searching for opcodes.
   if (code_start >= data.size() || code_start < metadata_start) {
      std::vector<uint8_t> potential_opcodes = {0x73, 0x03, 0x7b, 0x7d, 0x7f, 0x80, 0x71};
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

   std::cout << "[PmailVm Debug] ScriptObject::parse: name=" << name << ", code_start=" << code_start << ", data_size=" << data.size() << std::endl;

   codeOffset = code_start;
   codeWords = (data.size() - codeOffset) / 4;

   return data.size();
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
   std::cout << "[PmailVm Debug] start called: obj=" << (obj ? obj->name : "null") << ", pc=" << pc << ", size=" << (obj ? obj->data.size() : 0) << std::endl;
   cur_ = obj;
   pc_ = pc;
   stack_.clear();
   lines_.clear();
   acc_ = 0;
   blocked_ = false;
   ended_ = false;
   if (cur_) {
     std::cout << "[PmailVm Debug] start complete: obj=" << cur_->name << ", pc=" << pc_ << ", size=" << cur_->data.size() << ", ended=" << (ended_ ? "true" : "false") << std::endl;
   } else {
     std::cout << "[PmailVm Debug] start complete: obj=null, pc=" << pc_ << ", ended=" << (ended_ ? "true" : "false") << std::endl;
   }
}

void Vm::jumpTo(const ObjectPtr &obj, uint32_t pc) {
   cur_ = obj;
   pc_ = pc;
}

void Vm::setVar(uint32_t id, uint32_t v) {
   if (id < vars_.size()) {
     vars_[id] = v;
   } else {
     vars_.resize(id + 1, 0);
     vars_[id] = v;
   }
}

int32_t Vm::value(const Arg &a) const {
   if (a.tag() == 1)
     return int32_t(var(a.value()));
   return a.value();
}

bool Vm::step() {
   std::cout << "[PmailVm Debug] step called: pc=" << pc_ << ", ended=" << (ended_ ? "true" : "false") << std::endl;
   if (ended_ || !cur_)
     return false;

   const auto &data = cur_->data;
   if (pc_ + 4 > data.size()) {
     std::cout << "[PmailVm Debug] step: PC out of bounds! pc=" << pc_ << ", size=" << data.size() << std::endl;
     ended_ = true;
     return false;
   }

   uint32_t ins = data[pc_] | (data[pc_ + 1] << 8) | (data[pc_ + 2] << 16) |
                  (uint32_t(data[pc_ + 3]) << 24);
   
   std::cout << "[PmailVm Debug] instruction at pc=" << pc_ << ": "
             << std::hex << "0x" << std::setw(8) << std::setfill('0') << ins << std::dec << std::endl;

   pc_ += 4;

   uint8_t op = ins & 0xFF;
   uint8_t mode = (ins >> 8) & 0xFF;
   uint16_t imm = ins >> 16;

   switch (op) {
   case 0x00: { // HALT
     std::cout << "[PmailVm Debug] HALT" << std::endl;
     ended_ = true;
     break;
   }

   case 0x03: { // JUMP / BRANCH
     std::cout << "[PmailVm Debug] JUMP [0x" << std::hex << (int)op << "] mode=" << std::dec << (int)mode << " imm=" << imm << std::endl;
     if (mode == 0) {
       pc_ += imm;
     } else if (mode == 1) { // JUMP IF ACC != 0
       if (acc_ != 0)
         pc_ += imm;
     } else if (mode == 2) { // JUMP IF ACC == 0
       if (acc_ == 0)
         pc_ += imm;
     }
     break;
   }

   case 0x73: { // LOAD_CONST / SET_REG
     std::cout << "[PmailVm Debug] LOAD_CONST/SET_REG [0x" << std::hex << (int)op << "] mode=" << std::dec << (int)mode << " imm=" << imm << std::endl;
     if (mode == 0)
       acc_ = imm;
     else
       setVar(mode, imm);
     break;
   }

   case 0x71: { // LOAD_ADDR
     std::cout << "[PmailVm Debug] LOAD_ADDR [0x" << std::hex << (int)op << "] imm=" << std::dec << imm << std::endl;
     acc_ = imm;
     break;
   }

   case 0x7b: { // STORE_8
     std::cout << "[PmailVm Debug] STORE_8 [0x" << std::hex << (int)op << "] mode=" << std::dec << (int)mode << " imm=" << (int)(imm & 0xFF) << std::endl;
     setVar(mode, imm & 0xFF);
     break;
   }
   case 0x7d: { // STORE_16
     std::cout << "[PmailVm Debug] STORE_16 [0x" << std::hex << (int)op << "] mode=" << std::dec << (int)mode << " imm=" << (int)(imm & 0xFFFF) << std::endl;
     setVar(mode, imm & 0xFFFF);
     break;
   }
   case 0x7f: { // STORE_32
     std::cout << "[PmailVm Debug] STORE_32 [0x" << std::hex << (int)op << "] mode=" << std::dec << (int)mode << " imm=" << imm << std::endl;
     if (pc_ + 4 <= cur_->data.size()) {
       uint32_t val = cur_->data[pc_] | (cur_->data[pc_ + 1] << 8) |
                      (cur_->data[pc_ + 2] << 16) |
                      (uint32_t(cur_->data[pc_ + 3]) << 24);
       pc_ += 4;
       setVar(mode, val);
     }
     break;
   }
   case 0x80: { // STORE_ANY
     std::cout << "[PmailVm Debug] STORE_ANY [0x" << std::hex << (int)op << "] mode=" << std::dec << (int)mode << " imm=" << imm << std::endl;
     setVar(mode, imm);
     break;
   }
   case 0x8a: { // CALL_NATIVE
     std::cout << "[PmailVm Debug] CALL_NATIVE [0x" << std::hex << (int)op << "] mode=" << std::dec << (int)mode << " imm=" << imm << std::endl;
     // Implement later
     break;
   }
   case 0x8d: { // LOAD_OBJECT
     std::cout << "[PmailVm Debug] LOAD_OBJECT [0x" << std::hex << (int)op << "] mode=" << std::dec << (int)mode << " imm=" << imm << std::endl;
     // Implement later
     break;
   }
   case 0x11: { // PUSH_STACK
     std::cout << "[PmailVm Debug] PUSH_STACK [0x" << std::hex << (int)op << "] mode=" << std::dec << (int)mode << " imm=" << imm << std::endl;
     // Implement later
     break;
   }

   default: {
     std::cout << "[PmailVm Debug] NOP (Unknown Opcode Bypass) 0x" << std::hex << (int)op << " mode=0x" << std::hex << (int)mode << " imm=0x" << std::hex << imm << std::dec << " at PC: " << pc_ << std::endl;
     break;
   }
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
