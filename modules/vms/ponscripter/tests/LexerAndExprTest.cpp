// Copyright 2026 Kotonoha contributors. SPDX-License-Identifier: MIT
#include <Ponscripter/Lexer.hpp>
#include <Ponscripter/ScriptBuffer.hpp>
#include <Ponscripter/TextFormatter.hpp>
#include <Ponscripter/VariableStore.hpp>

#include <iostream>
#include <stdexcept>
#include <string>

namespace {

void check(bool cond, const char *msg) {
  if (!cond)
    throw std::runtime_error(msg);
}

} // namespace

int main() {
  try {
    // 1. UTF-8 BOM stripping and ;mode800 directive
    Ponscripter::ScriptBuffer buf;
    const std::string bomScript =
        "\xEF\xBB\xBF;mode800,value500\n"
        "*define\n"
        "globalon\n"
        "numalias gro,500\n"
        "stralias s01,\"sound\\s01.ogg\"\n"
        "game\n"
        "*start\n"
        "lsp 85,\":a;system\\ffffff.png\",0,0:print 1\n"
        "^Olá, ~i~mundo~i~! ~~ Salvar ~~ ... --@\n"
        "br\n"
        "end\n";
    buf.appendFile("00.utf", bomScript);
    buf.finalize();

    check(buf.directives().resolution.width == 800 &&
              buf.directives().resolution.height == 600,
          "UTF-8 BOM must be stripped before parsing ;mode800");
    check(buf.directives().globalVariableBorder == 500,
          "value500 directive must set globalVariableBorder=500");
    check(buf.hasLabel("define") && buf.hasLabel("start"),
          "Labels *define and *start must be indexed");

    // 2. VariableStore and LineLexer expressions
    Ponscripter::VariableStore vars;
    vars.setGlobalOn(true);
    vars.setGlobalBorder(200);
    vars.setNumAlias("gro", 500);
    vars.setNumAlias("volmp3", 516);
    vars.setStrAlias("s02", "sound\\s02.ogg");

    check(vars.setNum("gro", 1),
          "Setting %gro (500 >= 200) must mark global dirty");
    check(!vars.setNum(10, 42), "Setting %10 (< 200) is local");
    check(vars.getNum("gro") == 1 && vars.getNum(500) == 1,
          "numalias lookup must match numeric ID");

    vars.setNum(3, 645);
    {
      Ponscripter::LineLexer lx("%3-600", &vars);
      check(lx.parseIntExpr() == 45,
            "Slider arithmetic %3-600 must evaluate to 45");
    }

    {
      vars.setNum("volmp3", 60);
      Ponscripter::LineLexer lx("%gro=1 && %volmp3<>-1", &vars);
      check(lx.parseCondition(), "Chained && and <> condition must be true");
    }

    {
      Ponscripter::LineLexer lx("s02", &vars);
      check(lx.parseStringExpr() == "sound\\s02.ogg",
            "Bareword stralias must resolve in parseStringExpr");
    }

    {
      Ponscripter::LineLexer lx("%123abc", &vars);
      check(lx.parseVarRef() == -1,
            "Numeric variables must reject trailing identifier text");
    }
    {
      Ponscripter::LineLexer lx("$123abc", &vars);
      check(lx.parseStrVarRef() == -1,
            "String variables must reject trailing identifier text");
    }

    // 3. TextFormatter inline tags, ~~ escape, #RRGGBB, and ligatures
    {
      Ponscripter::TextStyle style;
      auto formatted = Ponscripter::TextFormatter::formatLine(
          "^#00ff00Olá ~i~Saya~i~... --@\n", style, true);
      check(formatted.terminator == Ponscripter::TextTerminator::ClickInline,
            "Line ending with @ must set ClickInline terminator");
      check(formatted.spans.size() >= 3, "Inline ~i~ must split styled spans");
      check(formatted.spans[0].style.colorRgb == 0x00FF00u,
            "#00ff00 must set span colorRgb");
      check(formatted.spans[1].style.italic && !formatted.spans[2].style.italic,
            "~i~ must toggle italic on and off");
      check(formatted.plainText.find("\xE2\x80\xA6") != std::string::npos,
            "... must convert to U+2026 ellipsis");
      check(formatted.plainText.find("\xE2\x80\x93") != std::string::npos,
            "-- must convert to U+2013 en-dash");

      const std::string uiLabel =
          Ponscripter::TextFormatter::formatUiLabel("^~~ Salvar ~~", true);
      check(uiLabel == "~ Salvar ~",
            "~~ escape in UI label must emit single literal ~");
    }

    std::cout << "Ponscripter.LexerAndExpr passed\n";
    return 0;
  } catch (const std::exception &e) {
    std::cerr << "Ponscripter.LexerAndExpr failed: " << e.what() << '\n';
    return 1;
  }
}
