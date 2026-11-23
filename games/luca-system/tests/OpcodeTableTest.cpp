#include <LucaSystem/OpcodeTable.hpp>

#include <iostream>
#include <stdexcept>
#include <string>

namespace {
void expect(bool ok, const char *test) {
  if (!ok)
    throw std::runtime_error(test);
}
template <class F> void expectInvalid(F fn) {
  bool rejected = false;
  try {
    fn();
  } catch (const std::invalid_argument &) {
    rejected = true;
  }
  expect(rejected, "invalid opcode table must be rejected");
}
} // namespace

int main() {
  try {
    const auto air =
        LucaSystem::OpcodeTable::parse("EQU\nEQUN\nMESSAGE\nNULL\n");
    expect(air.size() == 4 && air.name(0) == "EQU" &&
               air.name(2) == "MESSAGE" && air.name(3) == "NULL" &&
               air.name(4) == "OP_4",
           "AIR name and opcode indices");
    const auto sp = LucaSystem::OpcodeTable::parse("\xef\xbb\xbf"
                                                   "EQU\r\nEQUN\r\nSELECT\r\n");
    expect(sp.size() == 3 && sp.name(0) == "EQU" && sp.name(2) == "SELECT",
           "SP BOM and CRLF");
    expectInvalid([] { LucaSystem::OpcodeTable::parse(""); });
    expectInvalid([] { LucaSystem::OpcodeTable::parse("EQU\n\nMESSAGE"); });
    expectInvalid([] { LucaSystem::OpcodeTable::parse("NAME WITH SPACES"); });
    std::string tooMany;
    for (int i = 0; i < 257; ++i)
      tooMany += "OP\n";
    expectInvalid([&] { LucaSystem::OpcodeTable::parse(tooMany); });
    std::cout << "Luca opcode table tests passed\n";
  } catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
