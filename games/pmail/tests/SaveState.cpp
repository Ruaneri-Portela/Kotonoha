#include <Pmail/Port.hpp>
#include <Pmail/Vm.hpp>
#include <SDL3/SDL.h>
#include <fstream>
#include <iostream>
#include <stdexcept>

namespace {
void require(bool value, const char *message) {
  if (!value) throw std::runtime_error(message);
}
struct Host : Pmail::Host {
  Pmail::ObjectPtr object;
  void native(Pmail::Vm &, int, const std::vector<Pmail::Arg> &) override {}
  Pmail::ObjectPtr loadByName(const std::string &name) override { return object && name == object->name ? object : nullptr; }
  Pmail::ObjectPtr loadById(uint32_t id) override { return object && id == object->id ? object : nullptr; }
};
void vmRoundTrip() {
  Host host;
  host.object = std::make_shared<Pmail::ScriptObject>();
  auto &obj = *host.object;
  obj.name = "synthetic.obj"; obj.id = 1; obj.codeOffset = 16; obj.codeWords = 8;
  obj.data.assign(16 + 8 * 4, 0); obj.strings = {"@"};
  auto put = [&](size_t at, uint32_t value) {
    for (int i = 0; i < 4; ++i) obj.data[at + i] = uint8_t(value >> (i * 8));
  };
  put(0, 4); put(4, 12); obj.data[8] = 3; obj.data[12] = 5;
  const uint32_t code[] = {0x1103002a, 0x20000000, 0x06020300, 0x21010000,
                           0, 0x11050000, 0x10203001, 0x04000000};
  for (size_t i = 0; i < 8; ++i) put(16 + i * 4, code[i]);
  Pmail::Vm vm(host); vm.registerObject(host.object); vm.start(host.object);
  vm.setVar(0x4000, 1234); vm.run(3);
  require(vm.stackDepth() == 2 && vm.pc() == 5, "Did not enter suspended user call");
  const auto saved = vm.serialize();
  vm.run(100); const auto finished = vm.serialize();
  require(vm.ended() && vm.var(0x3001) != 0, "Synthetic function failed");
  vm.deserialize(saved); vm.run(100);
  require(vm.serialize() == finished, "VM stack/user table/RNG continuation differs");
  auto corrupt = saved; corrupt[40] ^= 1;
  bool rejected = false;
  try { vm.deserialize(corrupt); } catch (...) { rejected = true; }
  require(rejected && vm.serialize() == finished, "Corrupt VM state mutated live VM");
}
std::string nextMessage(Kotonoha::PmailPort::PmailGame &game) {
  const auto deadline = SDL_GetTicks() + 10000;
  game.click();
  while (!game.waitingForClick() && !game.choosing() && !game.ended() && SDL_GetTicks() < deadline) {
    game.pump(); SDL_Delay(1);
  }
  require(!game.ended(), "Script ended before next dialogue");
  require(game.waitingForClick() || game.choosing(), "Timed out reaching next dialogue");
  return game.currentScript() + "|" + std::to_string(game.vm().pc()) + "|" + game.message();
}
}
int main(int argc, char **argv) {
  try {
    vmRoundTrip();
    if (argc != 2) throw std::runtime_error("Usage: PmailSaveStateCheck script.dat");
    std::ifstream file(argv[1], std::ios::binary);
    std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(file)), {});
    require(!bytes.empty(), "Cannot read script pack");
    Kotonoha::PmailPort::PmailGame game(bytes, argv[1]);
    require(game.init(), "Game init failed");
    game.returnToTitle(); require(game.atTitle(), "Return-to-title did not reach menu");
    require(game.titleChoose(0), "Cannot select new game");
    game.pump();
    require(game.waitingForClick() && !game.message().empty(), "Did not reach first dialogue");
    game.vm().setVar(0x4001, 137);
    const auto save = game.serialize();
    const auto expectedNext = nextMessage(game);
    Kotonoha::PmailPort::PmailGame restored(bytes, argv[1]);
    restored.deserialize(save);
    require(restored.serialize() == save, "Game round trip differs");
    require(restored.vm().var(0x4001) == 137, "Variable bank lost");
    require(nextMessage(restored) == expectedNext, "Restored story continuation differs");
    const auto before = restored.serialize();
    bool rejected = false;
    try { restored.deserialize(save.substr(0, save.size() - 1)); } catch (...) { rejected = true; }
    require(rejected && restored.serialize() == before, "Truncated save changed game");
    restored.returnToTitle(); require(restored.atTitle(), "Story-to-title failed");
    require(restored.vm().var(0x4001) == 137, "Return-to-title lost persistent variables");
    std::cout << "PMAIL save round-trip, VM stack/RNG, story continuation, invalid saves and return-to-title passed.\n";
    return 0;
  } catch (const std::exception &error) { std::cerr << error.what() << '\n'; return 1; }
}
