#include <Amagami/Vm.hpp>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <sstream>
#include <stdexcept>

namespace Amagami {
VmValue VmValue::num(double n, bool floating) {
  VmValue v;
  v.kind = Number;
  v.number = n;
  v.floating = floating || std::trunc(n) != n;
  return v;
}
VmValue VmValue::str(std::string s, Kind k) {
  VmValue v;
  v.kind = k;
  v.text = std::move(s);
  return v;
}
VmValue VmValue::ref(std::shared_ptr<VmObject> o, Kind k) {
  VmValue v;
  v.kind = k;
  v.object = std::move(o);
  return v;
}
bool VmValue::truth() const {
  return kind != Nil && (kind != Boolean || number != 0);
}
namespace {
class ScriptFault : public std::runtime_error {
public:
  using std::runtime_error::runtime_error;
};
VmValue boolean(bool b) {
  auto v = VmValue::num(b);
  v.kind = VmValue::Boolean;
  return v;
}
bool equal(const VmValue &a, const VmValue &b) {
  if (a.kind != b.kind)
    return false;
  if (a.kind == VmValue::Number || a.kind == VmValue::Boolean)
    return a.number == b.number;
  if (a.kind == VmValue::Text || a.kind == VmValue::Symbol ||
      a.kind == VmValue::Class)
    return a.text == b.text;
  return a.object == b.object;
}
} // namespace
void Vm::add(Script s) {
  const auto key = s.name;
  scripts_.insert_or_assign(key, std::move(s));
}
Vm Vm::fork(const VmValue &receiver, const std::string &method) const {
  Vm child = *this;
  child.frames_.clear();
  child.paused_ = false;
  child.result_ = {};
  child.exception_ = {};
  child.call(receiver, method);
  return child;
}
void Vm::reset() {
  frames_.clear();
  classes_.clear();
  result_ = {};
  exception_ = {};
  paused_ = false;
}
VmValue Vm::instance(const std::string &name) {
  auto o = std::make_shared<VmObject>();
  o->type = name;
  o->fields.resize(256);
  return VmValue::ref(std::move(o));
}
VmValue &Vm::classField(const std::string &name, size_t slot) {
  if (slot > 65535)
    throw std::runtime_error("VM field index exceeds limit");
  auto &o = classes_[name];
  if (!o) {
    o = std::make_shared<VmObject>();
    o->type = name;
  }
  if (o->fields.size() <= slot)
    o->fields.resize(slot + 1);
  return o->fields[slot];
}
VmValue Vm::constant(const Value &c) {
  switch (c.type) {
  case Value::Integer:
    return VmValue::num(int32_t(c.bits));
  case Value::Float: {
    float f;
    std::memcpy(&f, &c.bits, 4);
    return VmValue::num(f, true);
  }
  case Value::False:
    return boolean(false);
  case Value::True:
    return boolean(true);
  case Value::String:
    return VmValue::str(c.text);
  case Value::Symbol:
    return VmValue::str(c.text, VmValue::Symbol);
  case Value::Class:
    return VmValue::str(c.text, VmValue::Class);
  case Value::Array: {
    auto o = std::make_shared<VmObject>();
    o->type = "Array";
    for (const auto &v : c.elements)
      o->fields.push_back(constant(v));
    return VmValue::ref(o, VmValue::Array);
  }
  default:
    return {};
  }
}
VmValue Vm::field(const VmValue &v, size_t slot) const {
  if (v.kind == VmValue::Class) {
    auto it = classes_.find(v.text);
    if (it == classes_.end() || it->second->fields.size() <= slot)
      return {};
    return it->second->fields[slot];
  }
  if (!v.object || slot >= v.object->fields.size())
    throw ScriptFault("VM field read outside object");
  return v.object->fields[slot];
}
void Vm::setField(const VmValue &v, size_t slot, VmValue value) {
  if (v.kind == VmValue::Class) {
    classField(v.text, slot) = std::move(value);
    return;
  }
  if (!v.object || slot >= v.object->fields.size())
    throw ScriptFault("VM field write outside object");
  v.object->fields[slot] = std::move(value);
}
void Vm::deliver(VmValue v) {
  if (frames_.empty())
    result_ = std::move(v);
  else
    frames_.back().stack.push_back(std::move(v));
}
void Vm::call(const VmValue &v, const std::string &s, std::vector<VmValue> a) {
  send(v, s, std::move(a));
}
void Vm::send(VmValue receiver, std::string selector, std::vector<VmValue> args,
              bool super) {
  if (selector == "perform" && args.size() == 1 &&
      args[0].kind == VmValue::Symbol) {
    auto method = args[0].text;
    send(std::move(receiver), method, {});
    return;
  }
  if (selector == "at" && args.size() == 1 && receiver.kind == VmValue::Array) {
    deliver(field(receiver, size_t(args[0].number)));
    return;
  }
  if (selector == "put" && args.size() == 2 &&
      receiver.kind == VmValue::Array) {
    setField(receiver, size_t(args[0].number), args[1]);
    deliver(receiver);
    return;
  }
  if (selector == "length" && args.empty() && receiver.kind == VmValue::Array) {
    deliver(VmValue::num(receiver.object->fields.size()));
    return;
  }
  if (receiver.kind == VmValue::Number && args.empty() &&
      (selector == "toInteger" || selector == "toFloat")) {
    deliver(VmValue::num(selector == "toInteger" ? std::trunc(receiver.number)
                                                 : receiver.number,
                         selector == "toFloat"));
    return;
  }
  if (native)
    if (auto result = native(receiver, selector, args)) {
      deliver(std::move(*result));
      return;
    }
  if (receiver.kind == VmValue::Class && selector == "new") {
    if (receiver.text == "Array" && args.size() == 1 &&
        args[0].kind == VmValue::Number) {
      const double size = args[0].number;
      if (size < 0 || size > 65535 || std::trunc(size) != size)
        throw ScriptFault("Invalid Array allocation size");
      auto object = std::make_shared<VmObject>();
      object->type = "Array";
      object->fields.resize(size_t(size));
      deliver(VmValue::ref(std::move(object), VmValue::Array));
      return;
    }
    // Script classes inherit the native allocator, which invokes initialize
    // with the same arguments and returns the initialized receiver.
    auto type = receiver.text;
    for (unsigned depth = 0; depth < 64 && !type.empty(); ++depth) {
      const auto found = scripts_.find(type);
      if (found == scripts_.end())
        break;
      for (const auto &method : found->second.methods)
        if (!method.classMethod && method.name == "initialize" &&
            method.arguments == args.size()) {
          auto object = instance(receiver.text);
          send(std::move(object), "initialize", std::move(args));
          return;
        }
      type = found->second.parent;
    }
  }
  std::string type = receiver.kind == VmValue::Class ? receiver.text
                     : receiver.object               ? receiver.object->type
                                                     : "";
  if (super && !frames_.empty())
    type = frames_.back().script->parent;
  for (unsigned depth = 0; depth < 64 && !type.empty(); ++depth) {
    auto it = scripts_.find(type);
    if (it == scripts_.end())
      break;
    for (const auto &m : it->second.methods)
      if (m.name == selector && m.arguments == args.size() &&
          m.classMethod == (receiver.kind == VmValue::Class)) {
        if (frames_.size() >= 256)
          throw std::runtime_error("VM call depth exceeds limit");
        Frame f;
        f.script = &it->second;
        f.method = &m;
        f.self = receiver;
        f.result = receiver;
        f.locals = std::move(args);
        frames_.push_back(std::move(f));
        return;
      }
    type = it->second.parent;
  }
  throw std::runtime_error("Unhandled SCF call " + type + "." + selector + "/" +
                           std::to_string(args.size()) + " at " + location());
}
std::string Vm::location() const {
  if (frames_.empty())
    return "finished";
  const auto &f = frames_.back();
  return f.script->name + "." + f.method->name + ":" + std::to_string(f.pc);
}
void Vm::resume(std::optional<VmValue> result) {
  if (result) {
    if (frames_.empty() || frames_.back().stack.empty())
      throw std::runtime_error("No pending VM result");
    frames_.back().stack.back() = std::move(*result);
  }
  paused_ = false;
}
void Vm::raise(VmValue value) {
  const auto at = location();
  exception_ = std::move(value);
  while (!frames_.empty()) {
    auto &frame = frames_.back();
    if (!frame.handlers.empty()) {
      const auto handler = frame.handlers.back();
      frame.handlers.pop_back();
      frame.stack.resize(handler.stackSize);
      frame.pc = handler.target;
      return;
    }
    frames_.pop_back();
  }
  throw std::runtime_error("Uncaught SCF exception at " + at + ": " +
                           exception_.text);
}
const Vm::OpcodeInfo *Vm::opcodeInfo(uint8_t opcode) {
  static const std::map<uint8_t, OpcodeInfo> table{
      {0x00, {"nop", 0, true}},
      {0x01, {"marker", 1, true}},
      {0x02, {"return", 0, true}},
      {0x03, {"result", 0, true}},
      {0x04, {"dup", 0, true}},
      {0x05, {"pop", 0, true}},
      {0x06, {"notEqualIdentity", 0, true}},
      {0x07, {"equalIdentity", 0, true}},
      {0x08, {"classField", 2, true}},
      {0x09, {"classField16", 3, true}},
      {0x0a, {"storeClassField", 2, true}},
      {0x0b, {"storeClassField16", 3, true}},
      {0x0c, {"and", 0, true}},
      {0x0d, {"or", 0, true}},
      {0x10, {"add", 0, true}},
      {0x11, {"subtract", 0, true}},
      {0x12, {"multiply", 0, true}},
      {0x13, {"divide", 0, true}},
      {0x14, {"modulo", 0, true}},
      {0x15, {"bitAnd", 0, true}},
      {0x16, {"bitOr", 0, true}},
      {0x17, {"bitXor", 0, true}},
      {0x18, {"equal", 0, true}},
      {0x19, {"greater", 0, true}},
      {0x1a, {"less", 0, true}},
      {0x1b, {"greaterEqual", 0, true}},
      {0x1c, {"lessEqual", 0, true}},
      {0x1d, {"notEqual", 0, true}},
      {0x1e, {"negate", 0, true}},
      {0x1f, {"bitNot", 0, true}},
      {0x20, {"field", 1, true}},
      {0x21, {"storeField", 1, true}},
      {0x22, {"local", 1, true}},
      {0x23, {"storeLocal", 1, true}},
      {0x24, {"int8", 1, true}},
      {0x25, {"nil", 0, true}},
      {0x26, {"self", 0, true}},
      {0x28, {"true", 0, true}},
      {0x29, {"false", 0, true}},
      {0x2a, {"exception", 0, true}},
      {0x2b, {"not", 0, true}},
      {0x30, {"send", 2, true}},
      {0x31, {"super", 2, true}},
      {0x32, {"send16", 3, true}},
      {0x33, {"super16", 3, true}},
      {0x34, {"primitive", 1, true}},
      {0x40, {"jump", 1, true}},
      {0x41, {"jump16", 2, true}},
      {0x44, {"ifFalse", 1, true}},
      {0x45, {"ifFalse16", 2, true}},
      {0x50, {"literal", 1, true}},
      {0x51, {"literal16", 2, true}},
      {0x64, {"at", 0, true}},
      {0x65, {"put", 0, true}},
      {0x68, {"clearException", 0, true}},
      {0x69, {"exceptionHandler", 2, true}},
      {0x6a, {"endHandler", 0, true}},
      {0x6b, {"throw", 0, true}},
      {0x6c, {"locals", 1, true}},
      {0x6d, {"marker16", 2, true}},
  };
  const auto found = table.find(opcode);
  return found == table.end() ? nullptr : &found->second;
}
void Vm::run(size_t budget) {
  while (!frames_.empty() && !paused_ && budget--) {
    auto &f = frames_.back();
    const size_t origin = f.pc;
    auto byte = [&]() {
      if (f.pc >= f.method->code.size())
        throw std::runtime_error("Truncated bytecode at " + location());
      return f.method->code[f.pc++];
    };
    auto word = [&]() {
      unsigned n = byte();
      return n | (unsigned(byte()) << 8);
    };
    auto pop = [&]() {
      if (f.stack.empty())
        throw std::runtime_error("Stack underflow at " + location());
      auto v = f.stack.back();
      f.stack.pop_back();
      return v;
    };
    auto push = [&](VmValue v) {
      if (f.stack.size() > 65535)
        throw std::runtime_error("Stack exceeds limit");
      f.stack.push_back(std::move(v));
    };
    auto literal = [&](size_t i) {
      return constant(f.script->constants.at(i));
    };
    auto jump = [&](int offset) {
      auto p = int64_t(origin) + offset;
      if (p < 0 || size_t(p) >= f.method->code.size())
        throw std::runtime_error("Invalid branch at " + location());
      f.pc = size_t(p);
    };
    uint8_t op = byte();
    try {
      switch (op) {
      case 0:
        break;
      case 1:
        byte();
        break;
      case 2: {
        auto value = f.result;
        frames_.pop_back();
        deliver(std::move(value));
        break;
      }
      case 3:
        f.result = pop();
        break;
      case 4: {
        auto v = pop();
        push(v);
        push(v);
        break;
      }
      case 5:
        pop();
        break;
      case 6:
      case 7: {
        auto b = pop(), a = pop();
        push(boolean(op == 7 ? equal(a, b) : !equal(a, b)));
        break;
      }
      case 0x0c:
      case 0x0d: {
        // Native handlers 0x11f1f0 / 0x11f2d8: strict Boolean AND / OR.
        const auto b = pop(), a = pop();
        if (a.kind != VmValue::Boolean || b.kind != VmValue::Boolean)
          throw std::runtime_error("Boolean operands required at " +
                                   location());
        push(boolean(op == 0x0c ? a.truth() && b.truth()
                                : a.truth() || b.truth()));
        break;
      }
      case 8:
      case 9:
      case 10:
      case 11: {
        size_t index = op & 1 ? word() : byte();
        size_t slot = byte();
        auto v = literal(index);
        if (op < 10)
          push(field(v, slot));
        else
          setField(v, slot, pop());
        break;
      }
      case 0x20:
        push(field(f.self, byte()));
        break;
      case 0x21: {
        auto n = byte();
        setField(f.self, n, pop());
        break;
      }
      case 0x22:
        push(f.locals.at(byte()));
        break;
      case 0x23: {
        auto n = byte();
        f.locals.at(n) = pop();
        break;
      }
      case 0x24:
        push(VmValue::num(int8_t(byte())));
        break;
      case 0x25:
        push({});
        break;
      case 0x26:
        push(f.self);
        break;
      case 0x28:
        push(boolean(true));
        break;
      case 0x29:
        push(boolean(false));
        break;
      case 0x2b: {
        auto a = pop();
        push(boolean(!a.truth()));
        break;
      }
      case 0x30:
      case 0x31:
      case 0x32:
      case 0x33: {
        unsigned count = byte(), index = op & 2 ? word() : byte();
        auto sel = f.script->constants.at(index).text;
        std::vector<VmValue> args(count);
        for (size_t i = count; i > 0; --i)
          args[i - 1] = pop();
        auto receiver = pop();
        send(std::move(receiver), std::move(sel), std::move(args), op & 1);
        break;
      }
      case 0x34:
        throw std::runtime_error("Unhandled SCF primitive " +
                                 std::to_string(byte()) + " at " + location());
      case 0x40:
      case 0x41: {
        int offset = op & 1 ? int16_t(word()) : int8_t(byte());
        jump(offset);
        break;
      }
      case 0x44:
      case 0x45: {
        int offset = op & 1 ? int16_t(word()) : int8_t(byte());
        if (!pop().truth())
          jump(offset);
        break;
      }
      case 0x50:
        push(literal(byte()));
        break;
      case 0x51:
        push(literal(word()));
        break;
      case 0x64: {
        auto index = pop(), a = pop();
        push(field(a, size_t(index.number)));
        break;
      }
      case 0x65: {
        auto value = pop(), index = pop(), a = pop();
        setField(a, size_t(index.number), value);
        push(a);
        break;
      }
      case 0x2a:
        push(exception_);
        break;
      case 0x68:
        exception_ = {};
        break;
      case 0x69: {
        const size_t target = origin + word();
        size_t handlerCount = 0;
        for (const auto &frame : frames_)
          handlerCount += frame.handlers.size();
        if (target >= f.method->code.size() || handlerCount >= 30)
          throw std::runtime_error("Invalid SCF exception handler at " +
                                   location());
        f.handlers.push_back({target, f.stack.size()});
        break;
      }
      case 0x6a:
        if (f.handlers.empty())
          throw std::runtime_error("SCF handler stack underflow at " +
                                   location());
        f.handlers.pop_back();
        break;
      case 0x6b:
        raise(pop());
        break;
      case 0x6c: {
        unsigned n = byte();
        f.locals.resize(f.locals.size() + n);
        break;
      }
      case 0x6d:
        word();
        break;
      case 0x1e: {
        auto a = pop();
        a.number = -a.number;
        push(a);
        break;
      }
      case 0x1f: {
        auto a = pop();
        a.number = ~int32_t(a.number);
        push(a);
        break;
      }
      default:
        if (op >= 0x10 && op <= 0x1d) {
          auto b = pop(), a = pop();
          double x = a.number, y = b.number;
          const bool floating = a.floating || b.floating;
          VmValue v;
          switch (op) {
          case 0x10:
            v = VmValue::num(x + y, floating);
            break;
          case 0x11:
            v = VmValue::num(x - y, floating);
            break;
          case 0x12:
            v = VmValue::num(x * y, floating);
            break;
          case 0x13:
            if (!y)
              throw ScriptFault("Division by zero");
            v = VmValue::num(floating ? x / y : std::trunc(x / y), floating);
            break;
          case 0x14:
            if (!y)
              throw ScriptFault("Modulo by zero");
            v = VmValue::num(std::fmod(x, y), floating);
            break;
          case 0x15:
            v = a.kind == VmValue::Boolean && b.kind == VmValue::Boolean
                    ? boolean(a.truth() && b.truth())
                    : VmValue::num(int32_t(x) & int32_t(y));
            break;
          case 0x16:
            v = a.kind == VmValue::Boolean && b.kind == VmValue::Boolean
                    ? boolean(a.truth() || b.truth())
                    : VmValue::num(int32_t(x) | int32_t(y));
            break;
          case 0x17:
            v = a.kind == VmValue::Boolean && b.kind == VmValue::Boolean
                    ? boolean(a.truth() != b.truth())
                    : VmValue::num(int32_t(x) ^ int32_t(y));
            break;
          case 0x18:
            v = boolean(x == y);
            break;
          case 0x19:
            v = boolean(x > y);
            break;
          case 0x1a:
            v = boolean(x < y);
            break;
          case 0x1b:
            v = boolean(x >= y);
            break;
          case 0x1c:
            v = boolean(x <= y);
            break;
          case 0x1d:
            v = boolean(x != y);
            break;
          }
          push(std::move(v));
          break;
        }
        throw std::runtime_error("Unsupported opcode " + std::to_string(op) +
                                 " at " + location());
      }
    } catch (const ScriptFault &fault) {
      raise(VmValue::str(fault.what()));
    }
  }
  if (!frames_.empty() && !paused_ && budget == size_t(-1))
    throw std::runtime_error("SCF instruction budget exhausted at " +
                             location());
}
} // namespace Amagami
