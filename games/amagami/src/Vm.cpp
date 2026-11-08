#include <Amagami/Vm.hpp>
#include <Kotonoha/localization/Encoding.hpp>
#include <algorithm>
#include <cmath>
#include <cstdint>
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
  if (k == Text) {
    v.object = std::make_shared<VmObject>();
    v.object->type = "String";
    v.object->stringValue = v.text;
  }
  return v;
}
VmValue VmValue::ref(std::shared_ptr<VmObject> o, Kind k) {
  VmValue v;
  v.kind = k;
  v.object = std::move(o);
  return v;
}
const std::string &VmValue::asText() const {
  return kind == Text && object && object->stringValue ? *object->stringValue
                                                       : text;
}
bool VmValue::truth() const {
  return kind != Nil && (kind != Boolean || number != 0);
}
namespace {
class ScriptFault : public std::runtime_error {
public:
  explicit ScriptFault(const std::string &message,
                       std::string type = "Exception")
      : std::runtime_error(message), type(std::move(type)) {}
  std::string type;
};
VmValue boolean(bool b) {
  auto v = VmValue::num(b);
  v.kind = VmValue::Boolean;
  return v;
}
size_t collectionIndex(const VmValue &value) {
  if (value.kind != VmValue::Number || !std::isfinite(value.number) ||
      value.number < 0 || value.number > 0x7fffffff ||
      std::trunc(value.number) != value.number)
    throw ScriptFault("Invalid collection index",
                      "ArrayIndexOutOfBoundsException");
  return size_t(value.number);
}
std::vector<unsigned> stringCharacters(const std::string &text) {
  const auto sjis = Kotonoha::ConvertEncoding(text, "UTF-8", "CP932");
  std::vector<unsigned> out;
  for (size_t i = 0; i < sjis.size(); ++i) {
    const unsigned lead = uint8_t(sjis[i]);
    if (((lead >= 0x81 && lead <= 0x9f) || (lead >= 0xe0 && lead <= 0xfc)) &&
        i + 1 < sjis.size())
      out.push_back((lead << 8) | uint8_t(sjis[++i]));
    else
      out.push_back(lead);
  }
  return out;
}
std::string charactersString(const std::vector<unsigned> &characters) {
  std::string bytes;
  for (const unsigned c : characters) {
    if (c > 0xffff)
      throw ScriptFault("Character outside CP932", "IllegalArgument");
    if (c > 0xff)
      bytes.push_back(char(c >> 8));
    bytes.push_back(char(c));
  }
  return Kotonoha::ShiftJisToUtf8(bytes);
}
bool equal(const VmValue &a, const VmValue &b) {
  if (a.kind != b.kind)
    return false;
  if (a.kind == VmValue::Number || a.kind == VmValue::Boolean)
    return a.number == b.number;
  if (a.kind == VmValue::Text || a.kind == VmValue::Symbol ||
      a.kind == VmValue::Class)
    return a.asText() == b.asText();
  return a.object == b.object;
}
std::string scfToString(const VmValue &v) {
  switch (v.kind) {
  case VmValue::Number:
    if (std::trunc(v.number) == v.number)
      return std::to_string(int64_t(v.number));
    return std::to_string(v.number);
  case VmValue::Boolean:
    return v.number != 0 ? "True" : "False";
  case VmValue::Text:
  case VmValue::Symbol:
  case VmValue::Class:
    return v.asText();
  case VmValue::Nil:
    return "nil";
  default:
    return {};
  }
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
void Vm::resetExecution() {
  frames_.clear();
  result_ = {};
  exception_ = {};
  paused_ = false;
}
void Vm::reset() {
  resetExecution();
  classes_.clear();
}
VmValue Vm::instance(const std::string &name) {
  auto o = std::make_shared<VmObject>();
  o->type = name;
  o->fields.resize(256);
  return VmValue::ref(std::move(o));
}
std::string Vm::typeOf(const VmValue &v) const {
  switch (v.kind) {
  case VmValue::Nil:
    return "Nil";
  case VmValue::Boolean:
    return v.number != 0 ? "True" : "False";
  case VmValue::Number:
    return v.floating ? "Float" : "Integer";
  case VmValue::Text:
    return "String";
  case VmValue::Symbol:
    return "Symbol";
  case VmValue::Class:
    return "Class";
  case VmValue::Array:
    return v.object ? v.object->type : "Array";
  case VmValue::Object:
  default:
    return v.object ? v.object->type : std::string();
  }
}
bool Vm::isKindOf(const std::string &type, const std::string &klass) const {
  std::string t = type;
  for (size_t depth = 0; depth < 256 && !t.empty(); ++depth) {
    if (t == klass)
      return true;
    auto it = scripts_.find(t);
    if (it == scripts_.end())
      return false;
    t = it->second.parent;
  }
  return false;
}
VmValue Vm::classFieldValue(const std::string &name, size_t slot) const {
  const auto found = classes_.find(name);
  return found != classes_.end() && found->second &&
                 slot < found->second->fields.size()
             ? found->second->fields[slot]
             : VmValue{};
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
  if (!v.object)
    throw ScriptFault("VM field read outside object",
                      v.kind == VmValue::Array
                          ? "ArrayIndexOutOfBoundsException"
                          : "Exception");
  if (slot >= v.object->fields.size()) {
    if (v.kind == VmValue::Array)
      throw ScriptFault("VM field read outside object",
                        "ArrayIndexOutOfBoundsException");
    return {};
  }
  return v.object->fields[slot];
}
void Vm::setField(const VmValue &v, size_t slot, VmValue value) {
  if (v.kind == VmValue::Class) {
    classField(v.text, slot) = std::move(value);
    return;
  }
  if (!v.object)
    throw ScriptFault("VM field write outside object",
                      v.kind == VmValue::Array
                          ? "ArrayIndexOutOfBoundsException"
                          : "Exception");
  if (slot >= v.object->fields.size()) {
    if (v.kind == VmValue::Array)
      throw ScriptFault("VM field write outside object",
                        "ArrayIndexOutOfBoundsException");
    return;
  }
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
  if ((selector == "performWith" || selector == "performWithArguments") &&
      args.size() >= 2 && args[0].kind == VmValue::Symbol) {
    const auto method = args[0].text;
    std::vector<VmValue> callArgs(args.begin() + 1, args.end());
    if (selector == "performWithArguments" && args.size() == 2 &&
        args[1].kind == VmValue::Array && args[1].object) {
      callArgs = args[1].object->fields;
    }
    send(std::move(receiver), method, std::move(callArgs));
    return;
  }
  if (receiver.kind == VmValue::Class && selector == "name" && args.empty()) {
    deliver(VmValue::str(receiver.text));
    return;
  }
  if (selector == "isKindOf" && args.size() == 1 &&
      args[0].kind == VmValue::Class) {
    auto result = VmValue::num(isKindOf(typeOf(receiver), args[0].text));
    result.kind = VmValue::Boolean;
    deliver(result);
    return;
  }
  if (selector == "getClass" && args.empty()) {
    deliver(VmValue::str(typeOf(receiver), VmValue::Class));
    return;
  }
  if (receiver.kind == VmValue::Class && receiver.text == "Class" &&
      selector == "findClass" && args.size() == 1 &&
      (args[0].kind == VmValue::Symbol || args[0].kind == VmValue::Text)) {
    deliver(hasClass(args[0].asText())
                ? VmValue::str(args[0].asText(), VmValue::Class)
                : VmValue{});
    return;
  }
  if ((receiver.kind == VmValue::Text || receiver.kind == VmValue::Symbol) &&
      selector == "length" && args.empty()) {
    deliver(VmValue::num(stringCharacters(receiver.asText()).size()));
    return;
  }
  if (receiver.kind == VmValue::Text &&
      (selector == "at" || selector == "[]") && args.size() == 1) {
    const auto characters = stringCharacters(receiver.asText());
    const auto index = collectionIndex(args[0]);
    if (index >= characters.size())
      throw ScriptFault("String index outside text",
                        "ArrayIndexOutOfBoundsException");
    deliver(VmValue::num(characters[index]));
    return;
  }
  if (receiver.kind == VmValue::Text &&
      (selector == "put" || selector == "atPut") && args.size() == 2) {
    auto characters = stringCharacters(receiver.asText());
    const auto index = collectionIndex(args[0]);
    if (index >= characters.size())
      throw ScriptFault("String index outside text",
                        "ArrayIndexOutOfBoundsException");
    const auto character = collectionIndex(args[1]);
    characters[index] = unsigned(character);
    *receiver.object->stringValue = charactersString(characters);
    deliver(receiver);
    return;
  }
  if (receiver.kind == VmValue::Text && selector == "append" &&
      args.size() == 1 && args[0].kind == VmValue::Text) {
    *receiver.object->stringValue += args[0].asText();
    deliver(receiver);
    return;
  }
  if (receiver.kind == VmValue::Text && args.empty() &&
      (selector == "clone" || selector == "shallowCopy")) {
    deliver(VmValue::str(receiver.asText()));
    return;
  }
  if ((receiver.kind == VmValue::Text || receiver.kind == VmValue::Symbol) &&
      selector == "toString" && args.empty()) {
    deliver(receiver.kind == VmValue::Symbol ? VmValue::str("#" + receiver.text)
                                             : receiver);
    return;
  }
  if (receiver.kind == VmValue::Number && args.empty() &&
      selector == "asChar") {
    deliver(
        VmValue::str(charactersString({unsigned(collectionIndex(receiver))})));
    return;
  }
  if (receiver.kind == VmValue::Number && args.empty() &&
      selector == "toString") {
    deliver(VmValue::str(scfToString(receiver)));
    return;
  }
  if ((selector == "at" || selector == "[]") && args.size() == 1 &&
      receiver.kind == VmValue::Array) {
    deliver(field(receiver, collectionIndex(args[0])));
    return;
  }
  if ((selector == "put" || selector == "atPut") && args.size() == 2 &&
      receiver.kind == VmValue::Array) {
    setField(receiver, collectionIndex(args[0]), args[1]);
    deliver(receiver);
    return;
  }
  if (selector == "length" && args.empty() && receiver.kind == VmValue::Array) {
    deliver(VmValue::num(receiver.object->fields.size()));
    return;
  }
  if (receiver.kind == VmValue::Array && receiver.object && args.empty() &&
      (selector == "clone" || selector == "shallowCopy")) {
    auto copy = std::make_shared<VmObject>(*receiver.object);
    deliver(VmValue::ref(std::move(copy), VmValue::Array));
    return;
  }
  if (receiver.kind == VmValue::Array && receiver.object &&
      selector == "copyFrom" && args.size() == 1 &&
      args[0].kind == VmValue::Array && args[0].object) {
    const auto source = args[0].object->fields;
    // EventNode.packNode copies a Vector's backing image into an Array
    // sized to its logical length. Native copyFrom clips to either buffer.
    std::copy_n(source.begin(),
                std::min(source.size(), receiver.object->fields.size()),
                receiver.object->fields.begin());
    deliver(receiver);
    return;
  }
  if (args.empty() && (selector == "toInteger" || selector == "toFloat") &&
      receiver.kind == VmValue::Number) {
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
    if (receiver.text == "Symbol" && args.size() == 1 &&
        args[0].kind == VmValue::Text) {
      deliver(VmValue::str(args[0].asText(), VmValue::Symbol));
      return;
    }
    if (receiver.text == "String" && args.size() <= 1) {
      if (args.empty() || args[0].kind == VmValue::Number)
        deliver(VmValue::str(""));
      else if (args[0].kind == VmValue::Text)
        deliver(VmValue::str(args[0].asText()));
      else
        throw ScriptFault("Invalid String constructor", "IllegalArgument");
      return;
    }
    if ((receiver.text == "Array" || receiver.text == "ByteArray" ||
         receiver.text == "UShortArray") &&
        args.size() == 1 && args[0].kind == VmValue::Number) {
      const double size = args[0].number;
      if (size < 0 || size > 65535 || std::trunc(size) != size)
        throw ScriptFault("Invalid Array allocation size");
      auto object = std::make_shared<VmObject>();
      object->type = receiver.text;
      object->fields.resize(
          size_t(size), receiver.text == "Array" ? VmValue{} : VmValue::num(0));
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
          const auto before = frames_.size();
          send(object, "initialize", std::move(args));
          if (frames_.size() > before)
            frames_.back().constructedResult = object;
          else if (frames_.empty())
            result_ = object;
          else
            frames_.back().stack.back() = object;
          return;
        }
      type = found->second.parent;
    }
  }
  std::string type =
      receiver.kind == VmValue::Class ? receiver.text : typeOf(receiver);
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
size_t Vm::pc() const {
  if (frames_.empty())
    return 0;
  return frames_.back().pc;
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
      {0x03, {"returnValue", 0, true}},
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
  size_t yieldCounter = 0;
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
        auto value = f.constructedResult.value_or(f.result);
        frames_.pop_back();
        deliver(std::move(value));
        break;
      }
      case 3: {
        // Jump table 0x28eeec: opcode 3 -> 0x11ed88 stores the stack
        // value and falls through to opcode 2's frame unwind (0x11eda0).
        // It is an immediate return, not an assignment to a future result.
        auto value = pop();
        if (f.constructedResult)
          value = *f.constructedResult;
        frames_.pop_back();
        deliver(std::move(value));
        break;
      }
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
        // Handlers 0x11C8E0 / 0x11C950 compute directly whenever both
        // operands are special constants (nil, true, false), and send
        // `&&`/`||` only when one is an integer or an object. `and`
        // answers the left operand when the right one is true, else
        // false; `or` answers the left operand when the right one is
        // false, else true. So `nil and x` is not an error.
        const auto right = pop(), left = pop();
        const auto special = [](const VmValue &v) {
          return v.kind == VmValue::Nil || v.kind == VmValue::Boolean;
        };
        if (special(left) && special(right)) {
          if (op == 0x0c) {
            const bool rightTrue =
                right.kind == VmValue::Boolean && right.number != 0;
            push(rightTrue ? left : boolean(false));
          } else {
            const bool rightFalse =
                right.kind == VmValue::Boolean && right.number == 0;
            push(rightFalse ? left : boolean(true));
          }
        } else {
          // `send` may push a frame, invalidating `f`; the loop re-reads
          // frames_.back() after this switch arm.
          send(left, op == 0x0c ? "&&" : "||", {right});
        }
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
      case 0x34: {
        const unsigned prim = byte();
        if (scopedPrimitive)
          if (auto result = scopedPrimitive(f.script->name, f.method->name,
                                            f.method->classMethod, f.self, prim,
                                            f.locals)) {
            f.result = *result;
            push(std::move(*result));
            break;
          }
        if (primitive)
          if (auto result = primitive(f.self, prim, f.locals)) {
            f.result = *result;
            push(std::move(*result));
            break;
          }
        const std::string owner = f.self.kind == VmValue::Class ? f.self.text
                                  : f.self.object ? f.self.object->type
                                                  : typeOf(f.self);
        throw std::runtime_error("Unhandled SCF primitive " +
                                 std::to_string(prim) + " for " + owner +
                                 " at " + location());
      }
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
        if ((a.kind == VmValue::Object && a.object) || a.kind == VmValue::Text)
          send(std::move(a), "[]", {index});
        else
          push(field(a, collectionIndex(index)));
        break;
      }
      case 0x65: {
        auto value = pop(), index = pop(), a = pop();
        if ((a.kind == VmValue::Object && a.object) || a.kind == VmValue::Text)
          send(std::move(a), "atPut", {index, value});
        else {
          setField(a, collectionIndex(index), value);
          push(a);
        }
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
          if (op == 0x10 &&
              (a.kind == VmValue::Text || b.kind == VmValue::Text ||
               a.kind == VmValue::Symbol || b.kind == VmValue::Symbol ||
               a.kind == VmValue::Class || b.kind == VmValue::Class)) {
            // Smalltalk-style text concatenation: <text|symbol|class> + any.
            push(VmValue::str(scfToString(a) + scfToString(b)));
            break;
          }
          if ((op == 0x18 || op == 0x1d) &&
              (a.kind != VmValue::Number || b.kind != VmValue::Number)) {
            if (a.kind == VmValue::Object && a.object)
              send(std::move(a), op == 0x18 ? "=" : "<>", {b});
            else
              push(boolean(op == 0x18 ? equal(a, b) : !equal(a, b)));
            break;
          }
          const bool booleanBits = (op >= 0x15 && op <= 0x17) &&
                                   a.kind == VmValue::Boolean &&
                                   b.kind == VmValue::Boolean;
          if (!booleanBits &&
              (a.kind != VmValue::Number || b.kind != VmValue::Number)) {
            // Operandos não numéricos: envia o operador como mensagem ao
            // operando esquerdo, como o intérprete PS2 faz (open-amagami
            // port/src/vm.cpp arith).
            static const char *arithNames[] = {"+", "-",  "*",  "/", "%",
                                               "&", "|",  "^",  "=", ">",
                                               "<", ">=", "<=", "<>"};
            send(std::move(a), arithNames[op - 0x10], {std::move(b)});
            break;
          }
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
              throw ScriptFault("Division by zero", "DivisionByZeroException");
            v = VmValue::num(floating ? x / y : std::trunc(x / y), floating);
            break;
          case 0x14:
            if (!y)
              throw ScriptFault("Modulo by zero", "DivisionByZeroException");
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
      // Yield periodico: permite que o caller (Session) avance o input
      // sintetico e outros estados entre instrucoes SCF, evitando loops
      // apertados que nunca devolvem controle ao runner.
      if (++yieldCounter >= 1000 && shouldYield && shouldYield()) {
        paused_ = true;
        return;
      }
    } catch (const ScriptFault &fault) {
      // Converte ScriptFault em excecao SCF. Se nao houver handler, entrega
      // nil e continua o loop em vez de lancar excecao C++ para fora do
      // run(), permitindo que cenarios headless continuem.
      auto exception = instance(fault.type);
      exception.text = fault.what();
      exception.object->fields[0] = VmValue::str(fault.what());
      try {
        raise(std::move(exception));
      } catch (const std::runtime_error &e) {
        std::string msg = e.what();
        if (msg.rfind("Uncaught SCF exception", 0) == 0) {
          deliver(VmValue{});
          paused_ = true;
          continue;
        }
        throw;
      }
    } catch (const std::exception &e) {
      // Apenas exceções de raise() sem handler (Uncaught SCF exception) e
      // chamadas não tratadas (Unhandled SCF call) são engolidas: entregam
      // nil e pausam a VM, permitindo que cenários headless continuem mesmo
      // quando uma exceção SCF não é capturada por nenhum script. Outros
      // erros (opcode não suportado, etc.) continuam propagando.
      std::string msg = e.what();
      if (msg.rfind("Uncaught SCF exception", 0) == 0 ||
          msg.rfind("Unhandled SCF call", 0) == 0) {
        deliver(VmValue{});
        paused_ = true;
        continue;
      }
      throw;
    }
  }
  if (!frames_.empty() && !paused_ && budget == size_t(-1))
    throw std::runtime_error("SCF instruction budget exhausted at " +
                             location());
}
} // namespace Amagami
