#include <SummerRadish/ImoutoScript.hpp>
#include <algorithm>
#include <stdexcept>

namespace SummerRadish {
namespace {
void requireScr(bool ok, const char *msg) {
  if (!ok)
    throw std::runtime_error(msg);
}

uint16_t readU16(const Bytes &b, size_t p) {
  requireScr(p + 2 <= b.size(), "Truncated .SCR uint16");
  return uint16_t(b[p]) | (uint16_t(b[p + 1]) << 8);
}

uint32_t readU32(const Bytes &b, size_t p) {
  requireScr(p + 4 <= b.size(), "Truncated .SCR uint32");
  return uint32_t(b[p]) | (uint32_t(b[p + 1]) << 8) |
         (uint32_t(b[p + 2]) << 16) | (uint32_t(b[p + 3]) << 24);
}

std::string readUtf16Z(const Bytes &b, size_t &p) {
  size_t start = p;
  size_t units = 0;
  while (true) {
    requireScr(p + 2 <= b.size(), "Unterminated UTF-16 string in .SCR");
    uint16_t ch = readU16(b, p);
    p += 2;
    if (ch == 0)
      break;
    ++units;
  }
  return utf16(b, start, units);
}
} // namespace

ImoutoScriptData parseImoutoScript(const Bytes &bytes) {
  ImoutoScriptData out;
  size_t p = 0;
  // 1. Source path (UTF-16LE, null-terminated + extra 0x0000)
  out.sourcePath = readUtf16Z(bytes, p);
  requireScr(p + 2 <= bytes.size() && readU16(bytes, p) == 0,
             "Missing .SCR source path double-null terminator");
  p += 2;

  // 2. Variable name table (UTF-16LE strings until empty string)
  while (true) {
    std::string name = readUtf16Z(bytes, p);
    if (name.empty())
      break;
    out.varNames.push_back(std::move(name));
  }

  // 3. String constant table (UTF-16LE strings until empty string)
  while (true) {
    std::string str = readUtf16Z(bytes, p);
    if (str.empty())
      break;
    out.strings.push_back(std::move(str));
  }

  // 4. Bytecode stream: 12-byte instructions + 2-byte trailing 0x0000 sentinel
  requireScr(bytes.size() >= p + 2, "Truncated .SCR bytecode stream");
  const size_t codeBytes = bytes.size() - p - 2;
  requireScr(codeBytes % 12 == 0, "Misaligned .SCR bytecode stream");
  requireScr(readU16(bytes, bytes.size() - 2) == 0,
             "Invalid .SCR EOF sentinel");
  const size_t count = codeBytes / 12;
  out.code.resize(count);
  for (size_t i = 0; i < count; ++i) {
    out.code[i].op = readU32(bytes, p);
    out.code[i].a1 = int32_t(readU32(bytes, p + 4));
    out.code[i].a2 = int32_t(readU32(bytes, p + 8));
    p += 12;
  }
  return out;
}

void ImoutoVM::reset() {
  slots.fill(ImoutoSlot{});
  persistentVars.clear();
  readBits.clear();
  currentScriptName.clear();
  script = {};
  sp = 0;
  pc = 0;
  currentLine = 0;
  waiting = false;
  finished = false;
  lastSetBitWasRead = false;
  scriptChanged_ = false;
}

void ImoutoVM::loadScript(std::string name, const Bytes &bytes) {
  currentScriptName = canonical(std::move(name));
  script = parseImoutoScript(bytes);
  sp = 0;
  pc = 0;
  currentLine = 0;
  waiting = false;
  finished = false;
  scriptChanged_ = true;

  // Bind named variables to slots[0x400 + i], restoring persistent values.
  for (size_t i = kVarBase; i < kSlotCount; ++i)
    slots[i] = ImoutoSlot::makeInt(0);
  for (size_t i = 0; i < script.varNames.size() && kVarBase + i < kSlotCount;
       ++i) {
    std::string varName = script.varNames[i];
    if (!varName.empty() && varName[0] == '%')
      varName.erase(0, 1);
    auto it = persistentVars.find(varName);
    slots[kVarBase + i] =
        ImoutoSlot::makeInt(it != persistentVars.end() ? it->second : 0);
  }
}

void ImoutoVM::push(ImoutoSlot slot) {
  requireScr(sp < kStackLimit, "ImoutoVM evaluation stack overflow");
  slots[sp++] = std::move(slot);
}

ImoutoSlot ImoutoVM::pop() {
  requireScr(sp > 0, "ImoutoVM evaluation stack underflow");
  return slots[--sp];
}

int32_t ImoutoVM::popInt() {
  ImoutoSlot s = pop();
  if (s.type == ImoutoSlot::Type::Int)
    return s.intVal;
  if (s.type == ImoutoSlot::Type::Ref && s.refSlot < kSlotCount)
    return slots[s.refSlot].intVal;
  return 0;
}

size_t ImoutoVM::resolveSlot(int32_t base, int32_t mode) {
  size_t idx = 0;
  if (mode == 1) {
    requireScr(base >= 0 && size_t(base) < kSlotCount, "ImoutoVM slot OOB");
    idx = size_t(base);
  } else if (mode == 2) {
    int32_t off = popInt();
    int64_t target = int64_t(base) + int64_t(off);
    requireScr(target >= 0 && size_t(target) < kSlotCount,
               "ImoutoVM indexed slot OOB");
    idx = size_t(target);
  } else {
    int32_t off = popInt();
    requireScr(base >= 0 && size_t(base) < kSlotCount,
               "ImoutoVM indirect base OOB");
    int64_t target = int64_t(slots[size_t(base)].refSlot) + int64_t(off);
    requireScr(target >= 0 && size_t(target) < kSlotCount,
               "ImoutoVM indirect slot OOB");
    idx = size_t(target);
  }
  return idx;
}

bool ImoutoVM::testAndSetReadBit(int32_t bitId) {
  if (bitId < 0)
    return false;
  auto &bits = readBits[currentScriptName];
  const size_t byteIdx = size_t(bitId) >> 3;
  const uint8_t mask = uint8_t(1u << (bitId & 7));
  if (bits.size() <= byteIdx)
    bits.resize(std::max<size_t>(512, byteIdx + 64), 0);
  const bool wasRead = (bits[byteIdx] & mask) != 0;
  bits[byteIdx] |= mask;
  return wasRead;
}

int32_t ImoutoVM::systemReg(size_t index) const {
  if (index >= kSlotCount)
    return 0;
  return slots[index].intVal;
}

void ImoutoVM::setSystemReg(size_t index, int32_t value) {
  if (index < kSlotCount)
    slots[index] = ImoutoSlot::makeInt(value);
}

void ImoutoVM::resume() {
  if (!waiting)
    return;
  waiting = false;
  run();
}

void ImoutoVM::run(size_t maxSteps) {
  size_t steps = 0;
  while (!waiting && !finished && pc < script.code.size()) {
    requireScr(++steps <= maxSteps, "ImoutoVM step limit exceeded");
    const ImoutoInstruction ins = script.code[pc++];
    switch (ins.op) {
    case 0x00: { // PUSH_VAR
      const size_t idx = resolveSlot(ins.a1, ins.a2);
      push(slots[idx]);
      break;
    }
    case 0x01: { // PUSH_REF
      const size_t idx = resolveSlot(ins.a1, ins.a2);
      push(ImoutoSlot::makeRef(idx));
      break;
    }
    case 0x02: { // PUSH_CONST
      if (ins.a1 == 1) {
        push(ImoutoSlot::makeInt(ins.a2));
      } else if (ins.a1 == 2) {
        requireScr(ins.a2 >= 0 && size_t(ins.a2) < script.strings.size(),
                   "ImoutoVM string constant OOB");
        push(ImoutoSlot::makeString(script.strings[size_t(ins.a2)]));
      } else {
        push(ImoutoSlot::makeInt(0));
      }
      break;
    }
    case 0x03: { // POP
      (void)pop();
      break;
    }
    case 0x04: { // ASSIGN
      ImoutoSlot rhs = pop();
      ImoutoSlot lhs = pop();
      requireScr(lhs.type == ImoutoSlot::Type::Ref && lhs.refSlot < kSlotCount,
                 "ImoutoVM ASSIGN requires slot ref");
      slots[lhs.refSlot] = rhs;
      if (lhs.refSlot >= kVarBase && rhs.type == ImoutoSlot::Type::Int) {
        const size_t vi = lhs.refSlot - kVarBase;
        if (vi < script.varNames.size()) {
          std::string varName = script.varNames[vi];
          if (!varName.empty() && varName[0] == '%')
            varName.erase(0, 1);
          persistentVars[varName] = rhs.intVal;
        }
      }
      push(rhs);
      break;
    }
    case 0x05: { // ADD
      int32_t b = popInt();
      int32_t a = popInt();
      push(ImoutoSlot::makeInt(a + b));
      break;
    }
    case 0x06: { // SUB
      int32_t b = popInt();
      int32_t a = popInt();
      push(ImoutoSlot::makeInt(a - b));
      break;
    }
    case 0x07: { // MUL
      int32_t b = popInt();
      int32_t a = popInt();
      push(ImoutoSlot::makeInt(a * b));
      break;
    }
    case 0x08: { // DIV
      int32_t b = popInt();
      int32_t a = popInt();
      requireScr(b != 0, "ImoutoVM division by zero");
      push(ImoutoSlot::makeInt(a / b));
      break;
    }
    case 0x09: { // MOD
      int32_t b = popInt();
      int32_t a = popInt();
      requireScr(b != 0, "ImoutoVM modulo by zero");
      push(ImoutoSlot::makeInt(a % b));
      break;
    }
    case 0x0A: { // NEG
      push(ImoutoSlot::makeInt(-popInt()));
      break;
    }
    case 0x0B: { // CMP
      int32_t b = popInt();
      int32_t a = popInt();
      bool r = false;
      switch (ins.a1) {
      case 0x21:
        r = (a == b);
        break;
      case 0x22:
        r = (a != b);
        break;
      case 0x23:
        r = (a < b);
        break;
      case 0x24:
        r = (a <= b);
        break;
      case 0x25:
        r = (a > b);
        break;
      case 0x26:
        r = (a >= b);
        break;
      default:
        throw std::runtime_error("ImoutoVM unknown CMP op");
      }
      push(ImoutoSlot::makeInt(r ? 1 : 0));
      break;
    }
    case 0x0C: { // END
      ImoutoSlot v = sp > 0 ? pop() : ImoutoSlot::makeInt(0);
      sp = 0;
      push(v);
      finished = true;
      break;
    }
    case 0x0D: { // JMP
      requireScr(ins.a1 >= 0 && size_t(ins.a1) <= script.code.size(),
                 "ImoutoVM JMP target OOB");
      pc = size_t(ins.a1);
      break;
    }
    case 0x0E: { // JZ
      ImoutoSlot v = pop();
      if (v.type == ImoutoSlot::Type::Int && v.intVal == 0) {
        if (ins.a2 != 0)
          push(ImoutoSlot::makeInt(0));
        requireScr(ins.a1 >= 0 && size_t(ins.a1) <= script.code.size(),
                   "ImoutoVM JZ target OOB");
        pc = size_t(ins.a1);
      }
      break;
    }
    case 0x0F: { // JNZ
      ImoutoSlot v = pop();
      if (!(v.type == ImoutoSlot::Type::Int && v.intVal == 0)) {
        if (ins.a2 != 0)
          push(ImoutoSlot::makeInt(1));
        requireScr(ins.a1 >= 0 && size_t(ins.a1) <= script.code.size(),
                   "ImoutoVM JNZ target OOB");
        pc = size_t(ins.a1);
      }
      break;
    }
    case 0x10: { // CALLMOD
      const int32_t argc = ins.a1;
      requireScr(argc >= 1 && size_t(argc) <= sp, "ImoutoVM CALLMOD bad argc");
      std::vector<ImoutoSlot> all(static_cast<size_t>(argc));
      for (int32_t i = argc - 1; i >= 0; --i)
        all[size_t(i)] = pop();
      requireScr(all[0].type == ImoutoSlot::Type::String,
                 "ImoutoVM CALLMOD arg0 must be string");
      std::vector<ImoutoSlot> params(all.begin() + 1, all.end());
      scriptChanged_ = false;
      int32_t ret = 0;
      if (onCall)
        ret = onCall(all[0].strVal, params);
      if (!scriptChanged_)
        push(ImoutoSlot::makeInt(ret));
      break;
    }
    case 0x11: { // PRINT
      for (int32_t i = 0; i < ins.a1 && sp > 0; ++i)
        (void)pop();
      break;
    }
    case 0x12:
    case 0x14: // MODLOG
      break;
    case 0x13: // LINE
      currentLine = ins.a1;
      break;
    case 0x15: // SETBIT
      lastSetBitWasRead = testAndSetReadBit(ins.a1);
      break;
    default:
      throw std::runtime_error("ImoutoVM unknown opcode: " +
                               std::to_string(ins.op));
    }
  }
}

} // namespace SummerRadish
