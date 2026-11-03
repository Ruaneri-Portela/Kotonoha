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
