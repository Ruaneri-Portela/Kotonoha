#include <Pmail/Vm.hpp>
#include <cstdint>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
struct Host final : Pmail::Host {
  std::vector<std::string> diagnostics;
  void native(Pmail::Vm &, int, const std::vector<Pmail::Arg> &) override {}
  Pmail::ObjectPtr loadByName(const std::string &) override { return {}; }
  Pmail::ObjectPtr loadById(uint32_t) override { return {}; }
  void log(const std::string &message) override {
    diagnostics.push_back(message);
  }
};

void require(bool value, const char *message) {
  if (!value)
    throw std::runtime_error(message);
}

Pmail::ObjectPtr makeExpressions() {
  auto obj = std::make_shared<Pmail::ScriptObject>();
  obj->name = "synthetic-expression.obj";
  obj->id = 7;
  // Every expression is evaluated by the real opcode 0x11/mode 0x05 and its
  // result is stored by opcode 0x10/mode 0x20. Comparisons preserve the
  // full 32-bit intermediate instead of masking overflow to 16 bits.
  obj->strings = {"2147483647 1 + 0 <", // wrap to INT_MIN
                  "2147483647 1 + -2147483648 ==",
                  "50000 50000 * 0 <",   // wrapped multiplication
                  "-2147483648 1 - 0 >", // wrap to INT_MAX
                  "-2147483648 -1 / -2147483648 ==",
                  "-2147483648 -1 % 0 ==",
                  "-1 1 >> -1 ==",  // arithmetic right shift
                  "1 32 << 1 ==",   // masked x86 shift count
                  "1 8 <<< 256 ==", // rotate left 8
                  "256 8 >>> 1 ==", // rotate right 8
                  "10 0 / 0 ==",    // defensive zero divisor
                  "123456789 987654321 + 0 >="};
  obj->codeOffset = 0;
  obj->codeWords = obj->strings.size() * 2 + 1;
  obj->data.assign(obj->codeWords * 4, 0);
  auto put = [&](size_t pos, uint32_t instruction) {
    for (unsigned b = 0; b < 4; ++b)
      obj->data[pos * 4 + b] = uint8_t(instruction >> (b * 8));
  };
  for (size_t i = 0; i < obj->strings.size(); ++i) {
    put(i * 2, 0x11050000u | uint32_t(i));
    put(i * 2 + 1, 0x10204000u | uint32_t(i));
  }
  put(obj->codeWords - 1, 0);
  return obj;
}
} // namespace

int main() {
  try {
    Host host;
    Pmail::Vm vm(host);
    auto obj = makeExpressions();
    for (int iteration = 0; iteration < 2; ++iteration) {
      vm.registerObject(obj);
      vm.start(obj);
      vm.run(1000);
      require(vm.ended(), "Synthetic expression program did not terminate");
      const std::vector<uint32_t> expected{1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1};
      for (size_t i = 0; i < expected.size(); ++i)
        if (vm.var(uint32_t(0x4000 + i)) != expected[i])
          throw std::runtime_error(
              "Unexpected x86 expression result at index " + std::to_string(i));
      vm.reset();
    }
    auto unsupported = std::make_shared<Pmail::ScriptObject>();
    unsupported->id = 9;
    unsupported->name = "synthetic-unknown-opcode.obj";
    unsupported->codeWords = 3;
    unsupported->data = {
        0, 0, 0, 0xfe, // unknown opcode FE
        0, 0, 0, 0xfe, // reported once, not every instruction
        0, 0, 0, 0     // halt
    };
    vm.registerObject(unsupported);
    vm.start(unsupported);
    vm.run(5);
    require(host.diagnostics.size() == 1 &&
                host.diagnostics.front().find("Unsupported PMAIL bytecode") !=
                    std::string::npos,
            "Unknown VM bytecode must emit one diagnostic per opcode");
    vm.reset();
    vm.registerObject(unsupported);
    vm.start(unsupported);
    vm.run(5);
    require(host.diagnostics.size() == 2,
            "VM reset must also reset missing-opcode diagnostics");
    std::cout << "Pure Mail deterministic 32-bit VM expression tests passed\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
