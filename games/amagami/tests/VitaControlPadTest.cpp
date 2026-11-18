#include <Amagami/VitaVmBridge.hpp>
#include <array>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace {
void check(bool pass, const char *reason) {
  if (!pass)
    throw std::runtime_error(reason);
}
void add(Amagami::Script &s, const std::string &name, uint8_t opcode,
         uint16_t argCount) {
  Amagami::Method method;
  method.name = name;
  method.arguments = argCount;
  method.code = {0x34, opcode, 2}; // SCF native call and return
  s.methods.push_back(std::move(method));
}
Amagami::VmValue run(Amagami::Vm &vm, const Amagami::VmValue &self,
                     const char *method,
                     std::vector<Amagami::VmValue> args = {}) {
  vm.call(self, method, std::move(args));
  vm.run(1000);
  check(vm.finished(), "SCF ControlPad call did not terminate");
  return vm.result();
}
} // namespace

int main() {
  try {
    using namespace Amagami;
    Vita::VitaVmBridge bridge;
    Vm vm;
    Script pad;
    pad.name = "ControlPad";
    add(pad, "held", 0, 0);
    add(pad, "trig", 1, 0);
    add(pad, "rapid", 2, 0);
    add(pad, "resetRapid", 3, 0);
    add(pad, "pressure", 4, 0);
    add(pad, "analog", 5, 1);
    add(pad, "rumble", 6, 2);
    vm.add(pad);
    vm.scopedPrimitive = [&bridge](const std::string &owner,
                                   const std::string &method, bool cls,
                                   const VmValue &receiver, unsigned code,
                                   const std::vector<VmValue> &args) {
      return bridge.primitive(owner, method, cls, receiver, code, args);
    };
    const auto self = vm.instance("ControlPad");
    const uint32_t buttons = 4096 | 32 | 4; // UP, CIRCLE and L
    const uint32_t expected = 0x10 | 0x2000 | 0x400 | 0x100;
    bridge.inputFrame(buttons, {{30, 40, -12, 0}});
    check(run(vm, self, "held").number == expected,
          "Vita byte-swapped SceCtrl physical button mask");
    check(run(vm, self, "trig").number == expected,
          "Newly pressed buttons available to SCF trigger");
    check(run(vm, self, "rapid").number == expected,
          "Newly pressed buttons are rapid this frame");
    check(run(vm, self, "pressure").number == 0,
          "Analog button pressure native returns zero");
    check(run(vm, self, "analog", {VmValue::num(0)}).number == 30 &&
              run(vm, self, "analog", {VmValue::num(2)}).number == -12,
          "Per-axis signed analog read");
    check(run(vm, self, "analog", {VmValue::num(4)}).number == 50 &&
              run(vm, self, "analog", {VmValue::num(9)}).number == 0,
          "Left stick magnitude and unsupported axis");

    bridge.inputFrame(buttons);
    check(run(vm, self, "trig").number == 0 &&
              run(vm, self, "rapid").number == 0,
          "Holding a button is not a new trigger");
    for (int i = 0; i < 19; ++i)
      bridge.inputFrame(buttons);
    check(run(vm, self, "rapid").number == expected,
          "Native auto-repeat starts after 20 held frames");
    bridge.inputFrame(buttons);
    check(run(vm, self, "rapid").number == 0,
          "Auto-repeat is not active on every frame");
    run(vm, self, "resetRapid");
    check(run(vm, self, "rapid").number == 0, "resetRapid clears repeat phase");
    check(
        run(vm, self, "rumble", {VmValue::num(255), VmValue::num(1)}).object ==
            self.object,
        "Rumble primitive gracefully returns receiver without haptics");

    bridge.inputFrame(0);
    check(run(vm, self, "held").number == 0 &&
              run(vm, self, "trig").number == 0,
          "Release all buttons");
    bridge.inputFrame(32);
    check(run(vm, self, "trig").number == 0x2000,
          "New press after release is recognized");
    std::cout << "Vita SCF ControlPad synthetic tests passed\n";
    return 0;
  } catch (const std::exception &e) {
    std::cerr << e.what() << '\n';
    return 1;
  }
}
