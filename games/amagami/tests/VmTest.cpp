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
