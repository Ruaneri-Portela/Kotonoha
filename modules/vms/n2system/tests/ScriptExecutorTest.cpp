// Copyright 2026 Kotonoha contributors. SPDX-License-Identifier: MIT
#include <N2System/ScriptExecutor.hpp>
#include <iostream>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <vector>

static void check(bool value, const char *name) {
  if (!value)
    throw std::runtime_error(name);
}
template <typename F> static void rejected(F fn) {
  bool caught = false;
  try {
    fn();
  } catch (const std::exception &) {
    caught = true;
  }
  check(caught, "unsafe NsScript accepted");
}
static_assert(!std::is_copy_constructible<N2System::ScriptExecutor>::value,
              "ScriptExecutor owns an index borrowed by its cursor");
static_assert(!std::is_move_constructible<N2System::ScriptExecutor>::value,
              "Moving ScriptExecutor would invalidate cursor references");
int main() {
  try {
    const std::string src = "chapter main {\n"
                            "start:\n"
                            "Show(\"Oi\\n\", 12);\n"
                            "WaitKey();\n"
                            "Goto(\"final\");\n"
                            "Show(\"SKIPPED\");\n"
                            "final:\n"
                            "Show('fim');\n"
                            "Chapter(\"other\");\n"
                            "}\n"
                            "chapter other { Show(\"next\"); End(); }\n";
    std::vector<std::string> trace;
    N2System::ScriptExecutor vm(
        src, "main",
        [&](const N2System::Invocation &call,
            const std::vector<std::string> &args) {
          const std::string name = call.function;
          if (name == "Show") {
            trace.push_back(args.at(0));
            if (trace.size() == 1)
              check(args.size() == 2 && args.at(1) == "12",
                    "literal arguments");
            return N2System::VmAction{};
          }
          if (name == "WaitKey")
            return N2System::VmAction{N2System::VmActionKind::Pause, {}};
          if (name == "Goto")
            return N2System::VmAction{N2System::VmActionKind::JumpLabel,
                                      args.at(0)};
          if (name == "Chapter")
            return N2System::VmAction{N2System::VmActionKind::EnterChapter,
                                      args.at(0)};
          if (name == "End")
            return N2System::VmAction{N2System::VmActionKind::Finish, {}};
          throw std::runtime_error("unknown NsScript call");
        });
    check(vm.run(1) == 1 && trace.size() == 1 && trace[0] == "Oi\n",
          "first call dispatched exactly once");
    check(vm.run(16) == 1 && vm.paused() && vm.run(16) == 0,
          "WaitKey callback blocks execution until resume");
    vm.resume();
    check(vm.run(16) == 5 && vm.finished() && vm.chapterName() == "other" &&
              trace == std::vector<std::string>({"Oi\n", "fim", "next"}) &&
              vm.run(16) == 0,
          "VM jumps chapters and skips bypassed calls without replay");
    check(vm.enterChapter("main") && vm.jumpToLabel("final") &&
              vm.remaining() == 2 && !vm.finished(),
          "explicit chapter and label navigation resets finished");
    check(!vm.jumpToLabel("missing") && vm.remaining() == 2 &&
              !vm.enterChapter("missing") && vm.chapterName() == "main",
          "invalid navigation leaves cursor unchanged");

    const auto noop = [](const N2System::Invocation &,
                         const std::vector<std::string> &) {
      return N2System::VmAction{};
    };
    for (const std::string invalid :
         {"chapter bad { if (1) { Show(\"wrong\"); } }",
          "chapter bad { Show(Other()); }",
          "chapter bad { Show(\"missing semicolon\") }",
          "chapter bad { var x = 1; Show(\"wrong\"); }",
          "chapter bad { Show(\"valid\"); while (1) Show(\"bad\"); }"}) {
      rejected([&] { N2System::ScriptExecutor bad(invalid, "bad", noop); });
    }
    rejected([&] { N2System::ScriptExecutor bad(src, "missing", noop); });
    rejected([&] {
      N2System::ScriptExecutor bad(src, "main", N2System::VmCallback{});
    });
    N2System::ScriptExecutor loop(
        "chapter main {\nstart:\nGoto(\"start\");\n}", "main",
        [](const N2System::Invocation &, const std::vector<std::string> &) {
          return N2System::VmAction{N2System::VmActionKind::JumpLabel, "start"};
        });
    check(loop.run(5) == 5 && !loop.finished() && !loop.paused(),
          "instruction budget prevents infinite callback loops");
    N2System::ScriptExecutor badTarget(
        "chapter main { Goto(\"absent\"); }", "main",
        [](const N2System::Invocation &, const std::vector<std::string> &) {
          return N2System::VmAction{N2System::VmActionKind::JumpLabel,
                                    "absent"};
        });
    const auto next = badTarget.next();
    rejected([&] { (void)badTarget.run(1); });
    check(badTarget.next() == next && !badTarget.finished(),
          "failed branch does not advance the VM program counter");
    // Subroutine calls preserve the next invocation, even across chapters.
    std::vector<std::string> calls;
    N2System::ScriptExecutor subroutines(
        "chapter main {\n"
        "  Call(\"worker\");\n"
        "  Show(\"returned\");\n"
        "  End();\n"
        "}\n"
        "chapter worker {\n"
        "  Show(\"worker\");\n"
        "  CallLocal(\"inner\");\n"
        "  Return();\n"
        "  inner:\n"
        "  Show(\"inner\");\n"
        "  Return();\n"
        "}\n",
        "main",
        [&](const N2System::Invocation &inv,
            const std::vector<std::string> &args) -> N2System::VmAction {
          if (inv.function == "Call")
            return {N2System::VmActionKind::CallChapter, args.at(0)};
          if (inv.function == "CallLocal")
            return {N2System::VmActionKind::CallLabel, args.at(0)};
          if (inv.function == "Return")
            return {N2System::VmActionKind::Return, {}};
          if (inv.function == "End")
            return {N2System::VmActionKind::Finish, {}};
          calls.push_back(args.at(0));
          return {};
        });
    check(subroutines.run(2) == 2 && subroutines.callDepth() == 1 &&
              calls == std::vector<std::string>{"worker"},
          "chapter call saves exact return frame");
    check(subroutines.run(16) == 6 && subroutines.finished() &&
              subroutines.callDepth() == 0 &&
              calls ==
                  std::vector<std::string>({"worker", "inner", "returned"}),
          "nested label call and returns restore instructions in order");

    N2System::ScriptExecutor missingCall(
        "chapter main { Call(\"missing\"); }", "main",
        [](const N2System::Invocation &,
           const std::vector<std::string> &) -> N2System::VmAction {
          return {N2System::VmActionKind::CallChapter, "missing"};
        });
    const auto *beforeMissing = missingCall.next();
    rejected([&] { missingCall.run(1); });
    check(missingCall.next() == beforeMissing && missingCall.callDepth() == 0,
          "invalid call leaves both PC and stack intact");

    N2System::ScriptExecutor strayReturn(
        "chapter main { Return(); }", "main",
        [](const N2System::Invocation &,
           const std::vector<std::string> &) -> N2System::VmAction {
          return {N2System::VmActionKind::Return, {}};
        });
    rejected([&] { strayReturn.run(1); });
    check(strayReturn.callDepth() == 0 && strayReturn.next(),
          "return without call cannot consume an instruction");

    N2System::ScriptExecutor recursion(
        "chapter main {\nloop:\nCall(\"loop\");\n}", "main",
        [](const N2System::Invocation &,
           const std::vector<std::string> &) -> N2System::VmAction {
          return {N2System::VmActionKind::CallLabel, "loop"};
        });
    check(recursion.run(1024) == 1024 && recursion.callDepth() == 1024,
          "bounded recursive calls may yield to a subsequent frame");
    rejected([&] { recursion.run(1); });
    check(recursion.callDepth() == 1024 && !recursion.finished(),
          "recursion guard rejects extra call before mutating stack");

    std::cout << "N2System constrained execution tests passed\n";
  } catch (const std::exception &e) {
    std::cerr << "N2System execution test: " << e.what() << '\n';
    return 1;
  }
}
