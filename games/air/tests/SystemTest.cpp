// Copyright 2026 Kotonoha contributors. SPDX-License-Identifier: MIT
#include "Air/System.hpp"
#include <cassert>
#include <memory>
namespace {
std::string integer(int value) {
  std::string s = "$";
  s += char(255);
  for (int i = 0; i < 4; ++i)
    s += char(static_cast<unsigned>(value) >> (8 * i));
  return s;
}
std::string stringRef(int index) {
  return std::string("$", 1) + char(18) + '[' + integer(index) + ']';
}
std::unique_ptr<libreallive::CommandElement>
command(int op, std::initializer_list<std::string> args) {
  std::string s = "#";
  s += char(1);
  s += char(4);
  s += char(op);
  s += char(op >> 8);
  s += char(args.size());
  s += char(0);
  s += char(0);
  s += '(';
  bool first = true;
  for (const auto &p : args) {
    if (!first)
      s += ',';
    first = false;
    s += p;
  }
  s += ')';
  return std::unique_ptr<libreallive::CommandElement>(
      libreallive::BuildFunctionElement(s.c_str()));
}
} // namespace
int main() {
  RLMachine m;
  auto config = [](const std::string &key, int fallback) {
    return key == "WINDOW.000.OPEN_ANM_TIME" ? 450 : fallback;
  };
  auto call = [&](int op, std::initializer_list<std::string> args) {
    return Air::systemCommand(m, *command(op, args), config);
  };
  assert(call(464, {integer(0)}) && m.store_register() == 450);
  assert(call(465, {integer(0), integer(1200)}));
  assert(call(464, {integer(0)}) && m.store_register() == 1200);
  assert(call(1301, {integer(0), "\"Yukito\""}));
  assert(call(1300, {integer(0), stringRef(1)}));
  assert(m.GetStringValue(18, 1) == "Yukito" &&
         Air::systemName(m, true, 0) == "Yukito");
  assert(call(1710, {integer(1), "\"Misuzu\""}));
  assert(call(1711, {integer(1), stringRef(2)}));
  assert(m.GetStringValue(18, 2) == "Misuzu");
  assert(call(2223, {integer(42)}));
  assert(call(2323, {}) && m.store_register() == 42);
  std::string interval = "a";
  interval += char(2);
  interval += '(';
  interval +=
      integer(0) + ',' + integer(10000) + ',' + integer(11) + ',' + integer(0);
  interval += ')';
  assert(call(800, {integer(5000), integer(0), integer(0), interval}) &&
         m.store_register() == 5);
  assert(call(800, {integer(10000), integer(0), integer(0), interval}) &&
         m.store_register() == 11);
  assert(!call(1700, {integer(0)}) && !call(1701, {integer(0)}) &&
         !call(1703, {integer(0)}) && !call(9876, {}));
}
