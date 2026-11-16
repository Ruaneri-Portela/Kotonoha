#include "PmailWindowTint.hpp"
#include <iostream>
#include <stdexcept>

int main() {
  using namespace Kotonoha::PmailPort;
  const auto defaults = ParsePmailMessageTint("");
  if (defaults.red != 0 || defaults.green != 0 || defaults.blue != 150)
    throw std::runtime_error("Original message window default tint changed");
  const auto parsed = ParsePmailMessageTint(
      "[Config]\r\n MesWin_R = 60\r\nMesWin_G=22\nMesWin_B=255\n");
  if (parsed.red != 60 || parsed.green != 22 || parsed.blue != 255)
    throw std::runtime_error("Original OF_ADV2.INI MesWin settings ignored");
  const auto invalid = ParsePmailMessageTint(
      "MesWin_R=-2\nMesWin_G=300\nMesWin_B=unsupported\n", defaults);
  if (invalid.red != 0 || invalid.green != 0 || invalid.blue != 150)
    throw std::runtime_error("Invalid tint was not ignored");
  if (PmailAddRgbChr(0, 150) != 149 || PmailAddRgbChr(255, 150) != 255 ||
      PmailAddRgbChr(10, 0) != 10)
    throw std::runtime_error("locpu.dll AddRgbChr math mismatch");
  std::cout << "PMAIL original MesWin defaults and INI color parsing passed\n";
}
