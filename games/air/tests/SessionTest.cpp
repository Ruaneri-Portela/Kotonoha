// Copyright 2026 Kotonoha contributors. SPDX-License-Identifier: MIT
#include "Air/Session.hpp"
#include <cassert>
#include <iostream>
#include <memory>
int main(int argc, char **argv) {
  if (argc < 2)
    return 77;
  Air::ScriptArchive archive(argv[1]);
  Air::Session session(archive);
  assert(session.scenario() == 9030 && !session.blocked() &&
         !session.finished());
  session.run(10000);
  assert(session.blocked());
  const int scene = session.scenario();
  const auto pc = session.index();
  assert(!session.step() && session.scenario() == scene &&
         session.index() == pc);
  std::size_t external = 0;
  session.command = [&](const libreallive::CommandElement &) {
    ++external;
    return true;
  };
  session.resume();
  auto n = session.run(20000);
  assert(n > 0 && external > 0);
  session.jump(9032);
  assert(session.scenario() == 9032 && !session.blocked());
  session.run(20000);
  session.seek(9032, 0);
  assert(session.index() == 0);
  auto call = [&](std::initializer_list<int> args) {
    std::string bytes = "#";
    bytes += char(2);
    bytes += char(1);
    bytes += char(12);
    bytes += char(0);
    bytes += char(args.size());
    bytes += char(0);
    bytes += char(args.size() - 1);
    bytes += '(';
    bool first = true;
    for (int value : args) {
      if (!first)
        bytes += ',';
      first = false;
      bytes += '$';
      bytes += char(255);
      for (int i = 0; i < 4; ++i)
        bytes += char(static_cast<unsigned>(value) >> (i * 8));
    }
    bytes += ')';
    auto c = std::unique_ptr<libreallive::CommandElement>(
        libreallive::BuildFunctionElement(bytes.c_str()));
    session.machine().ExecuteCommand(*c);
    return session.machine().store_register();
  };
  auto &m = session.machine();
  constexpr int src = (18 << 16) | 3, out = (18 << 16) | 4,
                xmod = (0 << 16) | 9;
  m.SetStringValue(18, 3, std::string("A") + char(3) + "B");
  assert(call({0, 10, src}) == 1);
  assert(m.GetStringValue(18, 3).front() == ' ');
  session.glyphPosition = [](const std::string &) { return 17; };
  assert(call({0, 12, out, xmod}) == 2 && m.GetStringValue(18, 4) == " ");
  assert(m.GetIntValue(libreallive::IntMemRef(0, 9)) == 17);
  assert(call({0, 12, out, xmod}) == 2 && m.GetStringValue(18, 4) == "A");
  assert(call({0, 12, out, xmod}) == 3);
  assert(call({0, 12, out, xmod}) == 2 && m.GetStringValue(18, 4) == "B");
  assert(call({0, 12, out, xmod}) == 1);
  m.SetStringValue(18, 3, std::string(1, char(1)) + "Misuzu" + char(2) + "Oi");
  std::string who;
  session.speaker = [&](std::string s) { who = s; };
  call({0, 100, -1, 1});
  call({0, 10, src});
  assert(call({0, 12, out, xmod}) == 2 && who == "Misuzu" &&
         m.GetStringValue(18, 4) == "O");
  // Preserve an unread rlBabel glyph plus binary strings and all interpreter
  // maps.
  m.SetIntValue(libreallive::IntMemRef(0, 71), -12345);
  m.SetStringValue(12, 7, std::string("x\0y", 3));
  m.set_store_register(-99);
  session.currentWindow = 63;
  const auto saved = session.snapshot();
  const auto savedPc = session.index();
  assert(call({0, 12, out, xmod}) == 2 && m.GetStringValue(18, 4) == "i");
  m.SetIntValue(libreallive::IntMemRef(0, 71), 42);
  session.jump(9030);
  session.restore(saved);
  assert(session.index() == savedPc && session.currentWindow == 63 &&
         m.store_register() == -99);
  assert(m.GetIntValue(libreallive::IntMemRef(0, 71)) == -12345 &&
         m.GetStringValue(12, 7) == std::string("x\0y", 3));
  assert(session.snapshot() == saved);
  assert(call({0, 12, out, xmod}) == 2 && m.GetStringValue(18, 4) == "i");
  // Invalid snapshots must leave every serialized field unchanged.
  const auto before = session.snapshot();
  for (auto invalid :
       {std::string(), before.substr(0, before.size() - 1), before + "junk"}) {
    bool threw = false;
    try {
      session.restore(invalid);
    } catch (const std::exception &) {
      threw = true;
    }
    assert(threw && session.snapshot() == before);
  }
  auto invalidPc = before;
  for (int i = 0; i < 4; ++i)
    invalidPc[12 + i] = char(255);
  bool rejected = false;
  try {
    session.restore(invalidPc);
  } catch (const std::exception &) {
    rejected = true;
  }
  assert(rejected && session.snapshot() == before);
  // Locate a real subroutine call and verify the pending return frame survives.
  bool testedStack = false;
  for (int sc : archive.ScenarioNumbers()) {
    std::size_t index = 0;
    for (const auto &e : archive.Scenario(sc)) {
      auto *c = dynamic_cast<libreallive::CommandElement *>(e.get());
      if (c && c->modtype() == 0 && c->module() == 1 && c->opcode() == 5) {
        session.seek(sc, index);
        const auto depth = session.stackDepth();
        assert(session.step());
        assert(session.stackDepth() == depth + 1);
        const auto withStack = session.snapshot();
        session.jump(9032);
        session.restore(withStack);
        assert(session.stackDepth() == depth + 1 &&
               session.snapshot() == withStack);
        testedStack = true;
        break;
      }
      ++index;
    }
    if (testedStack)
      break;
  }
  assert(testedStack);
  session.seek(9032, 0);
  std::cout << "startup instructions=" << n << " external=" << external
            << " scene=" << session.scenario() << '\n';
}
