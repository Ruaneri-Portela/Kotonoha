#include <Amagami/VitaVmBridge.hpp>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
void check(bool passed, const char *message) {
  if (!passed)
    throw std::runtime_error(message);
}
Amagami::VmValue number(double value) {
  return Amagami::VmValue::num(value);
}
void rejects(Amagami::Vita::VitaVmBridge &bridge,
             const std::string &owner, unsigned opcode,
             const Amagami::VmValue &self,
             const std::vector<Amagami::VmValue> &args) {
  bool rejected = false;
  try {
    auto result = bridge.primitive(owner, "test", false, self, opcode, args);
    rejected = !result;
  } catch (const std::exception &) {
    rejected = true;
  }
  check(rejected, "Unsupported save/write or invalid argument was accepted");
}
} // namespace

int main() {
  try {
    using namespace Amagami;
    Vita::VitaVmBridge bridge;
    const auto card = VmValue::ref(std::make_shared<VmObject>());
    auto changed = bridge.primitive("MemoryCard", "isChanged", false,
                                    card, 0, {});
    check(changed && changed->kind == VmValue::Boolean &&
              changed->number == 0,
          "No card changes before save backend is available");
    auto status = bridge.primitive("MemoryCard", "status", false, card, 1, {});
    check(status && status->kind == VmValue::Number && status->number == 6,
          "Empty but usable Vita card status");
    auto checkData = bridge.primitive("MemoryCard", "checkData", false,
                                      card, 2, {number(1)});
    check(checkData && checkData->object == card.object,
          "Vita checkData does not fabricate saved records");
    auto deleteBroken = bridge.primitive("MemoryCard", "deleteAll", false,
                                         card, 9, {});
    check(deleteBroken && deleteBroken->number == 0,
          "No corrupted Vita save data");
    auto showMessage = bridge.primitive("MemoryCard", "message", false,
                                        card, 10, {});
    check(showMessage && showMessage->object == card.object,
          "No native card error to display");
    auto pending = bridge.primitive("MemoryCard", "messageBusy", false,
                                    card, 11, {});
    check(pending && pending->kind == VmValue::Boolean &&
              pending->number == 0,
          "No pending system message in the experimental Vita adapter");

    // Fail explicitly rather than acknowledging saves that were never made.
    for (unsigned opcode = 3; opcode <= 8; ++opcode)
      rejects(bridge, "MemoryCard", opcode, card,
              {number(0), number(0), number(0)});
    rejects(bridge, "MemoryCard", 0, card, {number(0)});
    rejects(bridge, "MemoryCard", 2, card, {});

    const auto voice = VmValue::ref(std::make_shared<VmObject>());
    auto length = bridge.primitive("Voice", "length", false,
                                   voice, 5, {number(0)});
    check(length && length->number == 0,
          "Silent Voice.length is zero without a decoder");
    rejects(bridge, "Voice", 5, voice, {});
    rejects(bridge, "Voice", 1, voice, {number(0)});
    std::cout << "Vita bootstrap card and voice fallback tests passed\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
