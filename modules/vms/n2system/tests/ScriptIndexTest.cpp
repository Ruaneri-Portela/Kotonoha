// Copyright 2026 Kotonoha contributors. SPDX-License-Identifier: MIT
#include <N2System/ScriptIndex.hpp>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>

namespace {
void check(bool value, const char *message) {
  if (!value) throw std::runtime_error(message);
}
void rejects(std::string_view value) {
  bool failed = false;
  try { (void)N2System::ScriptIndex::Parse(value); }
  catch (const std::runtime_error &) { failed = true; }
  check(failed, "malformed NsScript accepted");
}
} // namespace
int main() {
  try {
    const std::string src =
        "\xef\xbb\xbf"
        "// chapter fake { ignored\n"
        "chapter main {\n"
        "  begin:\n"
        "  WaitKey();\n"
        "  /* { ignored } */\n"
        "  Show(\"} // inside string\");\n"
        "  if (true) { DoThing(); }\n"
        "  done:\n"
        "}\n"
        "chapter ending { finish: WaitKey(); }\n";
    const auto parsed = N2System::ScriptIndex::Parse(src);
    check(parsed.chapters().size() == 2, "chapter count");
    const auto *main = parsed.findChapter("main");
    check(main && main->labels.size() == 2, "main chapter labels");
    check(main->findLabel("begin") && main->findLabel("done"), "label lookup");
    check(main->findLabel("begin")->line == 3, "source line tracking");
    check(main->invocations.size() == 3,
          "NsScript call sites exclude comments and if conditions");
    check(main->invocations[0].function == "WaitKey" &&
              main->invocations[1].function == "Show" &&
              main->invocations[2].function == "DoThing",
          "NsScript call order");
    check(main->invocations[0].line == 4,
          "NsScript call source location");
    check(src.substr(main->findLabel("begin")->offset, 5) == "begin",
          "label source offset");
    check(main->start < main->end, "chapter source range");
    const auto *ending = parsed.findChapter("ending");
    check(ending && !ending->findLabel("finish"),
          "inline label should not be a line label");
    check(!parsed.findChapter("missing"), "missing chapter");
    rejects("chapter main {");
    rejects("chapter { }");
    rejects("chapter main ( )");
    rejects("chapter main {} chapter main {}");
    rejects("chapter main {\nstart:\nstart:\n}");
    rejects("chapter main { /* unterminated");
    rejects("chapter main { Print(\"unterminated); }");
    std::cout << "N2System script index tests passed\n";
    return 0;
  } catch (const std::exception &e) {
    std::cerr << "N2System test failed: " << e.what() << '\n';
    return 1;
  }
}
