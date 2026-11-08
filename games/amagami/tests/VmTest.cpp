#include <Amagami/Vm.hpp>
#include <cmath>
#include <iostream>
#include <stdexcept>
using namespace Amagami;
void require(bool b, const char *s) {
  if (!b)
    throw std::runtime_error(s);
}
VmValue evaluate(Bytes code, std::vector<Value> constants = {}) {
  Vm vm;
  Script s;
  s.name = "Synthetic";
  Method m;
  m.name = "run";
  m.code = std::move(code);
  s.methods.push_back(std::move(m));
  s.constants = std::move(constants);
  vm.add(std::move(s));
  vm.call(vm.instance("Synthetic"), "run");
  vm.run();
  require(vm.finished(), "VM returns");
  return vm.result();
}
int main() {
  try {
    require(evaluate({0x6c, 0, 0x24, 5, 0x24, 3, 0x11, 3, 2}).number == 2,
            "Integer subtraction operand order");
    require(
        evaluate({0x24, 7, 3, 0xff}).number == 7,
        "Opcode 3 returns immediately without running following instructions");
    Value floatingOne;
    floatingOne.type = Value::Float;
    floatingOne.bits = 0x3f800000;
    require(std::abs(evaluate({0x50, 0, 0x50, 0, 0x10, 0x24, 3, 0x13, 3, 2},
                              {floatingOne})
                         .number -
                     2.0 / 3) < 0.000001,
            "Original Float arithmetic retains fractional camera coordinates");
    require(evaluate({0x24, 5, 0x24, 2, 0x13, 3, 2}).number == 2,
            "Integer division retains original truncation");
    for (const auto &[op, expected] :
         std::vector<std::pair<uint8_t, bool>>{{0x18, false},
                                               {0x19, true},
                                               {0x1a, false},
                                               {0x1b, true},
                                               {0x1c, false},
                                               {0x1d, true}}) {
      auto v = evaluate({0x6c, 0, 0x24, 5, 0x24, 3, op, 3, 2});
      require(v.kind == VmValue::Boolean && v.truth() == expected,
              "Recovered comparison opcode");
    }
    require(evaluate({0x24, 1, 0x24, 1, 7, 3, 2}).truth(),
            "Opcode 7 is identity equality");
    require(!evaluate({0x24, 1, 0x24, 1, 6, 3, 2}).truth(),
            "Opcode 6 is identity inequality");
    require(evaluate({0x24, 1, 0x24, 2, 6, 3, 2}).truth(),
            "Identity inequality of different values");
    Value textA, textB;
    textA.type = textB.type = Value::String;
    textA.text = "eventA";
    textB.text = "eventB";
    require(
        !evaluate({0x50, 0, 0x50, 1, 0x18, 3, 2}, {textA, textB}).truth(),
        "String value equality does not compare uninitialized numeric slots");
    require(evaluate({0x50, 0, 0x50, 1, 0x1d, 3, 2}, {textA, textB}).truth(),
            "String value inequality distinguishes original event identifiers");
    // Branch displacement is relative to the opcode, not the following byte.
    require(evaluate({0x6c, 0, 0x29, 0x45, 6, 0, 0x24, 1, 3, 0x24, 2, 3, 2})
                    .number == 2,
            "False branch displacement");
    Value arr;
    arr.type = Value::Array;
    Value num;
    num.type = Value::Integer;
    num.bits = 42;
    arr.elements.push_back(num);
    require(evaluate({0x6c, 0, 0x50, 0, 0x24, 0, 0x64, 3, 2}, {arr}).number ==
                42,
            "Nested literal array lookup");
    for (const uint8_t left : {uint8_t(0x28), uint8_t(0x29)})
      for (const uint8_t right : {uint8_t(0x28), uint8_t(0x29)}) {
        require(evaluate({left, right, 0x0c, 3, 2}).truth() ==
                    (left == 0x28 && right == 0x28),
                "Boolean AND opcode");
        require(evaluate({left, right, 0x0d, 3, 2}).truth() ==
                    (left == 0x28 || right == 0x28),
                "Boolean OR opcode");
        for (const uint8_t op : {uint8_t(0x15), uint8_t(0x16), uint8_t(0x17)}) {
          const auto result = evaluate({left, right, op, 3, 2});
          const bool a = left == 0x28, b = right == 0x28;
          require(result.kind == VmValue::Boolean &&
                      result.truth() == (op == 0x15   ? a && b
                                         : op == 0x16 ? a || b
                                                      : a != b),
                  "Boolean bitwise operators preserve their type for "
                  "GameParam.isSkip");
        }
      }
    // Special constants (nil/true/false) compute directly in the original
    // handlers; only integers/objects fall back to `&&`/`||`. `and` answers
    // the left operand when the right is true, else false; `or` answers the
    // left operand when the right is false, else true.
    require(evaluate({0x25, 0x28, 0x0c, 3, 2}).kind == VmValue::Nil,
            "nil and true returns the left operand");
    require(evaluate({0x25, 0x29, 0x0c, 3, 2}).kind == VmValue::Boolean &&
                !evaluate({0x25, 0x29, 0x0c, 3, 2}).truth(),
            "nil and false returns false");
    require(evaluate({0x28, 0x25, 0x0c, 3, 2}).kind == VmValue::Boolean &&
                !evaluate({0x28, 0x25, 0x0c, 3, 2}).truth(),
            "true and nil returns false");
    require(evaluate({0x25, 0x29, 0x0d, 3, 2}).kind == VmValue::Nil,
            "nil or false returns the left operand");
    require(evaluate({0x25, 0x28, 0x0d, 3, 2}).kind == VmValue::Boolean &&
                evaluate({0x25, 0x28, 0x0d, 3, 2}).truth(),
            "nil or true returns true");
    require(evaluate({0x29, 0x25, 0x0d, 3, 2}).kind == VmValue::Boolean &&
                evaluate({0x29, 0x25, 0x0d, 3, 2}).truth(),
            "false or nil returns true, matching the PS2 handler");
    require(
        evaluate({0x69, 8, 0, 0x24, 7, 0x6b, 0, 0, 0x2a, 3, 2}).number == 7,
        "Caught throw preserves its original value and relative handler PC");
    require(evaluate({0x69, 8, 0, 0x25, 0x24, 0, 0x64, 2, 0x2a, 3, 2}).text ==
                "VM field read outside object",
            "Invalid native array access enters script catch");
    require(
        evaluate({0x69, 9, 0, 0x24, 5, 0x6a, 3, 2, 0, 0x24, 9, 3, 2}).number ==
            5,
        "Normal try removes its handler and keeps the result");
    require(
        evaluate({0x69, 8, 0, 0x24, 7, 0x6b, 0, 0, 0x68, 0x2a, 3, 2}).kind ==
            VmValue::Nil,
        "Clearing an exception resets its script-visible slot");
    {
      Vm nested;
      Script script;
      script.name = "ExceptionFrames";
      Value fail;
      fail.type = Value::Symbol;
      fail.text = "fail";
      script.constants.push_back(fail);
      script.methods.push_back(
          {"run",
           0,
           false,
           {0x69, 9, 0, 0x26, 0x30, 0, 0, 0x6a, 2, 0x2a, 3, 2}});
      script.methods.push_back({"fail", 0, false, {0x24, 42, 0x6b, 2}});
      nested.add(std::move(script));
      nested.call(nested.instance("ExceptionFrames"), "run");
      nested.run();
      require(nested.finished() && nested.result().number == 42,
              "Exception unwinds a called method into its caller handler");
    }
    require(evaluate({0x69, 16, 0, 0x69, 8, 0, 0x24, 5, 0x6b, 0, 0, 0x2a, 0x6b,
                      0, 0, 0, 0x2a, 3, 2})
                    .number == 5,
            "Rethrowing inside a catch reaches the outer handler");
    require(
        evaluate({0x69, 11, 0, 0x24, 4, 0x24, 0, 0x13, 0x6a, 3, 2, 0x2a, 3, 2})
                .text == "Division by zero",
        "Script arithmetic faults enter the registered handler");
    bool unknownRejected = false;
    try {
      evaluate({0x69, 5, 0, 0xff, 2, 0x24, 1, 3, 2});
    } catch (const std::runtime_error &e) {
      unknownRejected =
          std::string(e.what()).find("Unsupported opcode") != std::string::npos;
    }
    require(unknownRejected,
            "Script catch never hides unsupported interpreter instructions");
    {
      Vm collections;
      auto array = std::make_shared<VmObject>();
      array->type = "Array";
      array->fields = {VmValue::num(1), VmValue::num(2)};
      auto original = VmValue::ref(array, VmValue::Array);
      collections.call(original, "clone");
      auto copy = collections.result();
      collections.call(copy, "atPut", {VmValue::num(0), VmValue::num(9)});
      require(original.object->fields[0].number == 1 &&
                  copy.object->fields[0].number == 9,
              "Array clone owns a separate element buffer");
      auto larger = std::make_shared<VmObject>();
      larger->type = "Array";
      larger->fields.resize(4);
      auto destination = VmValue::ref(larger, VmValue::Array);
      collections.call(destination, "copyFrom", {original});
      require(larger->fields[1].number == 2 &&
                  larger->fields[2].kind == VmValue::Nil,
              "Array copyFrom preserves unused destination capacity");

      auto shorter = std::make_shared<VmObject>();
      shorter->type = "Array";
      shorter->fields.resize(1);
      collections.call(VmValue::ref(shorter, VmValue::Array), "copyFrom",
                       {original});
      require(shorter->fields[0].number == 1,
              "Array copyFrom clips Vector backing capacity to logical "
              "destination size");

      Script klass;
      klass.name = "Indexed";
      klass.methods.push_back({"[]", 1, false, {0x22, 0, 0x24, 1, 0x10, 3, 2}});
      klass.methods.push_back(
          {"atPut", 2, false, {0x22, 0, 0x22, 1, 0x10, 3, 2}});
      klass.methods.push_back({"read", 0, false, {0x26, 0x24, 4, 0x64, 3, 2}});
      klass.methods.push_back(
          {"write", 0, false, {0x26, 0x24, 4, 0x24, 8, 0x65, 3, 2}});
      klass.methods.push_back({"initialize", 0, false, {0x24, 99, 3, 2}});
      collections.add(klass);
      collections.call(collections.instance("Indexed"), "read");
      collections.run();
      require(collections.result().number == 5,
              "Index opcode dispatches [] for script-defined collections");
      collections.call(collections.instance("Indexed"), "write");
      collections.run();
      require(
          collections.result().number == 12,
          "Index assignment opcode dispatches atPut for script collections");
      collections.call(VmValue::str("Indexed", VmValue::Class), "new");
      collections.run();
      require(collections.result().object &&
                  collections.result().object->type == "Indexed",
              "Allocator returns the object even when initialize returns "
              "another value");
      collections.call(VmValue::str("Class", VmValue::Class), "findClass",
                       {VmValue::str("Indexed", VmValue::Symbol)});
      require(collections.result().kind == VmValue::Class &&
                  collections.result().text == "Indexed",
              "Class.findClass resolves registered SCF classes");
      collections.call(VmValue::str("Class", VmValue::Class), "findClass",
                       {VmValue::str("Missing", VmValue::Symbol)});
      require(collections.result().kind == VmValue::Nil,
              "Class.findClass returns nil for a missing class");
    }
    {
      Vm text;
      auto name = VmValue::str("A美也");
      text.call(name, "length");
      require(text.result().number == 3,
              "SCF String length counts CP932 characters, not UTF-8 bytes");
      text.call(name, "[]", {VmValue::num(1)});
      require(text.result().number == 0x94fc,
              "String indexing returns original packed CP932 character codes");
      auto alias = name;
      text.call(name, "atPut", {VmValue::num(0), VmValue::num(0x819c)});
      require(alias.asText() == "●美也",
              "Indexed String assignment mutates its shared SCF identity");
      text.call(name, "clone");
      const auto independent = text.result();
      text.call(name, "append", {VmValue::str("!")});
      require(alias.asText() == "●美也!" && independent.asText() == "●美也",
              "String clone separates storage while aliases retain mutations");
      text.call(VmValue::num(0x819c), "asChar");
      require(text.result().asText() == "●",
              "Integer.asChar decodes CP932 units");
      text.call(VmValue::str("Symbol", VmValue::Class), "new",
                {VmValue::str("NodeTable")});
      require(text.result().kind == VmValue::Symbol &&
                  text.result().text == "NodeTable",
              "Symbol.new interns text as a SCF selector value");
      text.call(VmValue::str("ByteArray", VmValue::Class), "new",
                {VmValue::num(4)});
      require(text.result().object->type == "ByteArray" &&
                  text.result().object->fields[3].number == 0 &&
                  text.result().object->fields[3].kind == VmValue::Number,
              "Native ByteArray allocation initializes zero bytes");
      const auto fault =
          evaluate({0x69, 9, 0, 0x50, 0, 0x24, 2, 0x64, 2, 0x2a, 3, 2}, {arr});
      require(
          fault.object &&
              fault.object->type == "ArrayIndexOutOfBoundsException",
          "Collection bounds faults remain typed for Vector's original catch");
    }
    {
      Vm scoped;
      Script owner;
      owner.name = "NativeOwner";
      owner.methods.push_back({"nativeMethod", 1, false, {0x34, 7, 2}});
      scoped.add(owner);
      bool observed = false;
      scoped.scopedPrimitive =
          [&](const std::string &klass, const std::string &method,
              bool classMethod, const VmValue &receiver, unsigned index,
              const std::vector<VmValue> &args) -> std::optional<VmValue> {
        observed = klass == "NativeOwner" && method == "nativeMethod" &&
                   !classMethod && receiver.object && index == 7 &&
                   args.size() == 1 && args[0].number == 42;
        return VmValue::num(17);
      };
      scoped.call(scoped.instance("NativeOwner"), "nativeMethod",
                  {VmValue::num(42)});
      scoped.run();
      require(
          observed && scoped.result().number == 17,
          "Primitive dispatch exposes declaration owner, method and arguments");
    }
    Vm vm;
    Script s;
    s.name = "Yield";
    Method m;
    m.name = "run";
    m.code = {0x26, 0x30, 0, 0, 3, 2};
    s.methods.push_back(m);
    Value symbol;
    symbol.type = Value::Symbol;
    symbol.text = "choose";
    s.constants.push_back(symbol);
    vm.add(s);
    vm.native = [&](const VmValue &, const std::string &name,
                    const std::vector<VmValue> &) -> std::optional<VmValue> {
      if (name != "choose")
        return std::nullopt;
      vm.pause();
      return VmValue::num(0);
    };
    vm.call(vm.instance("Yield"), "run");
    vm.run();
    require(vm.paused() && !vm.finished(), "Native choice suspends bytecode");
    vm.resume(VmValue::num(7));
    vm.run();
    require(vm.finished() && vm.result().number == 7,
            "Answer resumes suspended call result");
    bool failed = false;
    try {
      evaluate({0xff});
    } catch (const std::exception &) {
      failed = true;
    }
    require(failed, "Unknown opcode fails closed");
    failed = false;
    try {
      evaluate({5});
    } catch (const std::exception &) {
      failed = true;
    }
    require(failed, "Stack underflow rejected");
    std::cout << "Amagami VM passed\n";
    return 0;
  } catch (const std::exception &e) {
    std::cerr << e.what() << '\n';
    return 1;
  }
}
