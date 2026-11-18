// Copyright 2026 Kotonoha contributors. SPDX-License-Identifier: MIT
#include <N2System/ScriptIndex.hpp>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>

namespace {
void check(bool value, const char *message) {
  if (!value)
    throw std::runtime_error(message);
}
void rejects(std::string_view value) {
  bool failed = false;
  try {
    (void)N2System::ScriptIndex::Parse(value);
  } catch (const std::runtime_error &) {
    failed = true;
  }
  check(failed, "malformed NsScript accepted");
}
} // namespace
int main() {
  try {
    const std::string src = "\xef\xbb\xbf"
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
    check(main->invocations[0].line == 4, "NsScript call source location");
    check(src.substr(main->findLabel("begin")->offset, 5) == "begin",
          "label source offset");
    check(main->start < main->end, "chapter source range");
    const auto *ending = parsed.findChapter("ending");
    check(ending && !ending->findLabel("finish"),
          "inline label should not be a line label");
    check(!parsed.findChapter("missing"), "missing chapter");
    N2System::ScriptCursor cursor(parsed, "main");
    check(cursor.chapterName() == "main" && cursor.remaining() == 3 &&
              cursor.peek() && cursor.peek()->function == "WaitKey",
          "cursor starts at chapter first function without executing it");
    check(cursor.advance() && cursor.remaining() == 2 && cursor.peek() &&
              cursor.peek()->function == "Show",
          "cursor advances to next indexed call");
    check(cursor.position() == 1 && cursor.seekPosition(3) &&
              cursor.remaining() == 0 && cursor.seekPosition(1) &&
              cursor.peek() && cursor.peek()->function == "Show" &&
              !cursor.seekPosition(4) && cursor.position() == 1,
          "cursor validates exact return PC without consuming invocation");
    check(!cursor.jumpToLabel("missing") && cursor.remaining() == 2,
          "missing NsScript label leaves cursor unchanged");
    check(cursor.jumpToLabel("done") && !cursor.peek() &&
              cursor.remaining() == 0 && !cursor.advance(),
          "cursor jumps to terminal label without running prior calls");
    check(cursor.jumpToLabel("begin") && cursor.remaining() == 3,
          "cursor can seek backwards by line-leading label");
    check(!cursor.enterChapter("missing") && cursor.chapterName() == "main" &&
              cursor.remaining() == 3,
          "missing chapter leaves cursor unchanged");
    check(cursor.enterChapter("ending") && cursor.chapterName() == "ending" &&
              cursor.remaining() == 1 &&
              cursor.advance()->function == "WaitKey" &&
              cursor.remaining() == 0,
          "cursor changes chapters without executing call sites");
    bool rejectedCursor = false;
    try {
      N2System::ScriptCursor invalid(parsed, "missing");
    } catch (const std::out_of_range &) {
      rejectedCursor = true;
    }
    check(rejectedCursor, "cursor rejects unknown startup chapter");
    const std::string oldMac = "chapter legacy {\r"
                               "  label_one:\r"
                               "  Show(\"line\");\r"
                               "  /* ignore\rbraces { } */\r"
                               "  label_two:\r"
                               "  WaitKey();\r"
                               "}\r";
    const auto cr = N2System::ScriptIndex::Parse(oldMac);
    const auto *legacy = cr.findChapter("legacy");
    check(legacy && legacy->findLabel("label_one") &&
              legacy->findLabel("label_two") &&
              legacy->findLabel("label_two")->line == 6,
          "CR-only NsScript source lines and labels");
    check(legacy && legacy->invocations.size() == 2 &&
              legacy->invocations[1].line == 7,
          "CR-only NsScript invocation line numbers");
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
