// Copyright 2026 Kotonoha contributors. SPDX-License-Identifier: MIT
#include <Ponscripter/PonscripterVm.hpp>

#include <algorithm>
#include <charconv>
#include <cctype>
#include <climits>
#include <cmath>
#include <cstdlib>
#include <ctime>

namespace Ponscripter {
namespace {

// Index 'mid' and 'len' in Unicode code points instead of splitting a UTF-8
// sequence. Malformed bytes are consumed as individual code points.
std::size_t utf8Offset(std::string_view value, std::size_t count) {
  std::size_t offset = 0;
  while (offset < value.size() && count-- > 0) {
    ++offset;
    while (offset < value.size() &&
           (static_cast<unsigned char>(value[offset]) & 0xC0u) == 0x80u)
      ++offset;
  }
  return offset;
}

std::size_t utf8Length(std::string_view value) {
  std::size_t count = 0;
  for (unsigned char c : value) {
    if ((c & 0xC0u) != 0x80u)
      ++count;
  }
  return count;
}

} // namespace

void PonscripterVm::registerCommand(std::string_view name,
                                    CommandHandler handler) {
  std::string key(name);
  std::transform(key.begin(), key.end(), key.begin(), [](unsigned char c) {
    return static_cast<char>(std::tolower(c));
  });
  funcLut_[std::move(key)] = handler;
}

void PonscripterVm::registerCommands() {
  // 1. Structure & Lifecycle
  for (const char *c :
       {"game",       "end",       "reset",         "definereset", "globalon",
        "nsa",        "ns2",       "ns3",           "arc",         "nsaoffset",
        "caption",    "apptitle",  "versionstr",    "humanz",      "underline",
        "windowback", "filelog",   "labellog",      "kidokuskip",  "kidokumode",
        "mode_ext",   "mode_saya", "mode_wave_demo",
        "textgosub", "pretextgosub"}) {
    registerCommand(c, &PonscripterVm::cmdLifecycle);
  }

  // 2. Aliases & Subroutines
  for (const char *c : {"numalias", "stralias", "defsub", "dim", "getparam"}) {
    registerCommand(c, &PonscripterVm::cmdAlias);
  }

  // 3. Flow Control
  for (const char *c :
       {"if", "notif", "goto", "gosub", "return", "for", "next", "break",
        "select", "selgosub", "selnum", "csel", "cselgoto", "cselbtn",
        "getcselnum", "getcselstr", "skip", "tablegoto",
        "tablegoto1", "debugtablegoto"}) {
    registerCommand(c, &PonscripterVm::cmdFlow);
  }

  // 4. Variables & Math
  for (const char *c :
       {"mov",      "mov3",  "mov4", "mov5",     "mov6",     "mov7", "mov8",
        "mov9",     "mov10", "movl", "movz", "inc",      "dec",      "add",  "sub",
        "mul",      "div",   "mod",  "rnd",      "rnd2",     "cmp",  "atoi",
        "itoa",     "itoa2", "len",  "mid",      "sin",      "cos",  "tan",
        "intlimit", "date",  "time", "savetime", "fileexist"}) {
    registerCommand(c, &PonscripterVm::cmdVars);
  }

  // 5. Backgrounds, Standing & Extended Sprites
  for (const char *c :
       {"bg",          "bgcopy",     "bgcpy",        "ld",        "cl",
        "lsp",         "lsph",       "lsp2",         "lsph2",     "lsp2add",
        "lsp2sub",     "lsph2add",   "lsph2sub",     "csp",       "csp2",
        "vsp",         "vsp2",       "msp",          "msp2",      "amsp",
        "amsp2",       "cell",       "getspsize",    "getspmode", "allsphide",
        "allspresume", "allsp2hide", "allsp2resume", "spstr",     "strsp",
        "strsph",      "bar",        "barclear",     "prnum",     "prnumclear",
        "blt",         "draw",       "drawbg",       "drawbg2",   "drawsp",
        "drawsp2",     "drawsp3",    "drawfill",     "drawclear", "drawtext",
        "monocro",     "nega",       "quake",        "quakex",    "quakey",
        "tal"}) {
    registerCommand(c, &PonscripterVm::cmdGraphics);
  }

  // 6. Effects & Transitions
  for (const char *c :
       {"effect", "effectcut", "effectblank", "windoweffect", "print"}) {
    registerCommand(c, &PonscripterVm::cmdEffects);
  }

  // 7. Text, Fonts, Cursors & Windowing
  for (const char *c : {"h_mapfont",       "pmapfont",     "h_rendering",
                        "prendering",      "h_fontstyle",  "pfontstyle",
                        "h_defwindow",     "pdefwindow",   "h_usewindow",
                        "pusewindow",      "h_usewindow3", "pusewindow3",
                        "h_locate",        "plocate",      "pligate",
                        "pbreakstr",       "pindentstr",   "pcenterline",
                        "pspeedpercent",   "bidirect",     "gettextextent",
                        "gettextheight",   "localestring", "setwindow",
                        "setwindow2",      "setwindow3",   "setcursor",
                        "abssetcursor",    "br",           "br2",
                        "textoff",         "texton",       "textclear",
                        "erasetextwindow", "textspeed",    "defaultspeed",
                        "defaultfont",     "automode",     "automode_time",
                        "autoclick",       "skipoff",      "locate",
                        "puttext",         "texec",        "clickstr",     "linepage",
                        "linepage2",       "shadedistance"}) {
    registerCommand(c, &PonscripterVm::cmdTextAndWindow);
  }

  // 8. Interactive Buttons, Timers & Input
  for (const char *c : {"btndef",         "btn",         "spbtn",
                        "exbtn",          "exbtn_d",     "btnwait",
                        "btnwait2",       "btntime",     "btntime2",
                        "btndown",        "transbtn",    "getmousepos",
                        "click",          "lrclick",     "wait",
                        "delay",          "waittimer",   "resettimer",
                        "gettimer",       "getbtntimer", "cellcheckspbtn",
                        "cellcheckexbtn", "existspbtn"}) {
    registerCommand(c, &PonscripterVm::cmdButtonsAndInput);
  }

  // 9. Audio & Video
  for (const char *c : {"mp3",
                        "mp3loop",
                        "mp3save",
                        "mp3fadein",
                        "mp3fadeout",
                        "mp3stop",
                        "bgm",
                        "bgmonce",
                        "bgmstop",
                        "play",
                        "playonce",
                        "playstop",
                        "wave",
                        "waveloop",
                        "wavestop",
                        "dwave",
                        "dwaveload",
                        "dwaveloop",
                        "dwaveplay",
                        "dwaveplayloop",
                        "dwavestop",
                        "stop",
                        "bgmvol",
                        "mp3vol",
                        "sevol",
                        "voicevol",
                        "chvol",
                        "defmp3vol",
                        "defsevol",
                        "defvoicevol",
                        "getbgmvol",
                        "getmp3vol",
                        "getsevol",
                        "getvoicevol",
                        "clickvoice",
                        "selectvoice",
                        "menuselectvoice",
                        "mpegplay",
                        "avi",
                        "movie"}) {
    registerCommand(c, &PonscripterVm::cmdAudioAndVideo);
  }

  // 10. Menus, Save/Load & System Calls
  for (const char *c : {"savename",        "savenumber",     "rmenu",
                        "rmode",           "menusetwindow",  "menuselectcolor",
                        "selectcolor",     "lookbackbutton", "lookbackcolor",
                        "lookbackflush",   "lookbacksp",     "systemcall",
                        "isfull",          "menu_window",    "menu_full",
                        "menu_automode",   "menu_click_def", "menu_click_page",
                        "menu_waveon",     "menu_waveoff",   "savegame",
                        "savegame2",       "loadgame",       "saveon",
                        "saveoff",         "savefileexist",  "savescreenshot",
                        "savescreenshot2", "getscreenshot",  "deletescreenshot",
                        "lsp_savethumb"}) {
    registerCommand(c, &PonscripterVm::cmdMenusAndSystem);
  }
}

void PonscripterVm::cmdLifecycle(LineLexer &lexer, const std::string &cmd) {
  if (cmd == "game") {
    inDefineBlock_ = false;
    jumpToLabel("start");
    return;
  }
  if (cmd == "end") {
    waitState_ = WaitState::Halted;
    if (callbacks_) {
      callbacks_->onStopBgm(0);
      callbacks_->onStopWave(-1);
      callbacks_->onPersistGlobals();
    }
    return;
  }
  if (cmd == "reset" || cmd == "definereset") {
    resetToStart();
    return;
  }
  if (cmd == "globalon") {
    vars_.setGlobalOn(true);
    return;
  }
  if (cmd == "nsa" || cmd == "ns2" || cmd == "ns3" || cmd == "arc") {
    useNsa_ = true;
    return;
  }
  if (cmd == "caption" || cmd == "apptitle") {
    caption_ = lexer.parseStringExpr();
    if (callbacks_)
      callbacks_->onCaptionChanged(caption_);
    return;
  }
  if (cmd == "versionstr") {
    versionStr1_ = lexer.parseStringExpr();
    if (lexer.consumeComma())
      versionStr2_ = lexer.parseStringExpr();
    return;
  }
  if (cmd == "underline") {
    underlineValue_ = lexer.parseIntExpr();
    return;
  }
  if (cmd == "windowback") {
    windowback_ = true;
    return;
  }
  if (cmd == "pretextgosub") {
    pretextGosubLabel_ = lexer.parseLabelTarget();
    return;
  }
  if (cmd == "textgosub") {
    textGosubLabel_ = lexer.parseLabelTarget();
    return;
  }
}

void PonscripterVm::cmdAlias(LineLexer &lexer, const std::string &cmd) {
  if (cmd == "getparam") {
    if (callStack_.empty())
      return;
    CallStackEntry &frame = callStack_.back();
    while (true) {
      lexer.skipWhitespace();
      // i%dest and s%dest return the caller's numeric/string variable ID,
      // respectively, instead of the evaluated argument value.
      char referenceMode = '\0';
      if (lexer.peek() == 'i' || lexer.peek() == 's')
        referenceMode = lexer.get();
      const char kind = lexer.peek();
      if (kind != '%' && kind != '$')
        break;
      CallArgument arg;
      if (frame.nextArgument < frame.arguments.size())
        arg = frame.arguments[frame.nextArgument];
      ++frame.nextArgument;
      if (kind == '%') {
        const int id = lexer.parseVarRef();
        int value = arg.number;
        if (referenceMode == 'i' || referenceMode == 's') {
          value = (arg.sourceVariable >= 0 &&
                   arg.sourceIsString == (referenceMode == 's'))
                      ? arg.sourceVariable : -1;
        } else if (arg.isString) {
          value = 0;
          const char *start = arg.text.data();
          const char *finish = start + arg.text.size();
          const auto parsed = std::from_chars(start, finish, value);
          if (parsed.ec != std::errc{} || parsed.ptr != finish)
            value = 0;
        }
        if (id >= 0 && vars_.setNum(id, value) && callbacks_)
          callbacks_->onPersistGlobals();
      } else {
        const int id = lexer.parseStrVarRef();
        if (id >= 0)
          vars_.setStr(
              id, referenceMode == 'i' || referenceMode == 's'
                      ? std::to_string(
                            (arg.sourceVariable >= 0 &&
                             arg.sourceIsString == (referenceMode == 's'))
                                ? arg.sourceVariable : -1)
                      : (arg.isString ? arg.text : std::to_string(arg.number)));
      }
      if (!lexer.consumeComma())
        break;
    }
    return;
  }
  if (cmd == "numalias") {
    const std::string name = lexer.parseIdentifier();
    lexer.consumeComma();
    const int val = lexer.parseIntExpr();
    vars_.setNumAlias(name, val);
    return;
  }
  if (cmd == "stralias") {
    const std::string name = lexer.parseIdentifier();
    lexer.consumeComma();
    const std::string val = lexer.parseStringExpr();
    vars_.setStrAlias(name, val);
    return;
  }
  if (cmd == "defsub") {
    const std::string name = lexer.parseIdentifier();
    if (!name.empty())
      userDefsubs_[name] = name;
    return;
  }
  if (cmd == "dim") {
    lexer.skipWhitespace();
    if (lexer.peek() == '?') {
      lexer.get();
      int arrId = 0;
      const std::string tok = lexer.parseIdentifier();
      if (!tok.empty() && std::isdigit(static_cast<unsigned char>(tok[0])))
        arrId = std::stoi(tok);
      else
        vars_.findNumAlias(tok, arrId);
      std::vector<int> dims;
      while (lexer.peek() == '[') {
        lexer.get();
        dims.push_back(lexer.parseIntExpr());
        lexer.skipWhitespace();
        if (lexer.peek() == ']')
          lexer.get();
      }
      vars_.dimArray(arrId, dims);
    }
    return;
  }
}

void PonscripterVm::cmdFlow(LineLexer &lexer, const std::string &cmd) {
  if (cmd == "if" || cmd == "notif") {
    auto fchkFn = [this](std::string_view p) {
      std::string norm(p);
      std::replace(norm.begin(), norm.end(), '\\', '/');
      std::transform(
          norm.begin(), norm.end(), norm.begin(),
          [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
      return seenFiles_.count(norm) > 0;
    };
    auto lchkFn = [this](std::string_view l) {
      return seenLabels_.count(ScriptBuffer::normalizeLabel(l)) > 0;
    };

    bool cond = lexer.parseCondition(fchkFn, lchkFn);
    if (cmd == "notif")
      cond = !cond;

    if (!cond) {
      // Condition is false! Skip any remaining colon-separated statements on
      // the SAME source line (standard ONScripter/PONscripter `if` semantics!).
      const auto &stmts = script_.statements();
      if (pc_ > 0 && pc_ <= stmts.size()) {
        const std::size_t currentLine = stmts[pc_ - 1].lineIndex;
        while (pc_ < stmts.size() && stmts[pc_].lineIndex == currentLine) {
          ++pc_;
        }
      }
      return;
    }

    // Condition is true! Execute the remainder of the current statement slice,
    // and let subsequent statements on the same line run normally.
    lexer.skipWhitespace();
    if (!lexer.eof()) {
      StatementSlice tail;
      tail.lineIndex = (pc_ > 0 && pc_ <= script_.statements().size())
                           ? script_.statements()[pc_ - 1].lineIndex
                           : 0;
      tail.statementIndex = (pc_ > 0) ? (pc_ - 1) : 0;
      tail.text = std::string(lexer.remaining());
      tail.isText = (!tail.text.empty() &&
                     (tail.text.front() == '^' || tail.text.front() == '`'));
      executeStatement(tail);
    }
    return;
  }

  if (cmd == "goto") {
    const std::string target = lexer.parseLabelTarget();
    jumpToLabel(target);
    return;
  }

  if (cmd == "gosub") {
    const std::string target = lexer.parseLabelTarget();
    if (!script_.hasLabel(target))
      return;
    CallStackEntry frame;
    frame.returnStatementIndex = pc_;
    frame.targetLabel = target;
    frame.callerLabel = currentLabel_;
    captureCallArguments(lexer, frame, true);
    callStack_.push_back(std::move(frame));
    jumpToLabel(target);
    return;
  }

  if (cmd == "return") {
    if (!callStack_.empty()) {
      const CallStackEntry frame = callStack_.back();
      callStack_.pop_back();
      if (!lexer.eof()) {
        const std::string overrideTarget = lexer.parseLabelTarget();
        if (!overrideTarget.empty()) {
          jumpToLabel(overrideTarget);
          return;
        }
      }
      pc_ = frame.returnStatementIndex;
      if (!frame.callerLabel.empty()) {
        currentLabel_ = frame.callerLabel;
        const auto section = script_.findLabel(currentLabel_);
        if (section != ScriptBuffer::npos)
          currentSectionId_ = section;
      }
    }
    return;
  }

  if (cmd == "for") {
    const int varId = lexer.parseVarRef();
    lexer.skipWhitespace();
    if (lexer.peek() == '=')
      lexer.get();
    const int startVal = lexer.parseIntExpr();
    const std::string toKw = lexer.parseIdentifier();
    (void)toKw; // "to"
    const int endVal = lexer.parseIntExpr();
    int stepVal = 1;
    lexer.skipWhitespace();
    if (!lexer.eof()) {
      const std::string stepKw = lexer.parseIdentifier();
      if (stepKw == "step")
        stepVal = lexer.parseIntExpr();
    }
    if (stepVal == 0)
      stepVal = 1;

    if (varId >= 0) {
      const bool gChanged = vars_.setNum(varId, startVal);
      if (gChanged && callbacks_)
        callbacks_->onPersistGlobals();
    }

    // A for-loop whose initial value lies outside the range must not
    // enter its body, even when its body contains nested for-loops.
    if ((stepVal > 0 && startVal > endVal) ||
        (stepVal < 0 && startVal < endVal)) {
      const auto &stmts = script_.statements();
      int depth = 1;
      while (pc_ < stmts.size() && depth > 0) {
        if (!stmts[pc_].isLabel && !stmts[pc_].isText) {
          LineLexer scan(stmts[pc_].text, &vars_);
          const std::string nested = scan.parseIdentifier();
          if (nested == "for")
            ++depth;
          else if (nested == "next")
            --depth;
        }
        ++pc_;
      }
      return;
    }
    ForLoopFrame frame;
    frame.varId = varId;
    frame.endValue = endVal;
    frame.step = stepVal;
    frame.bodyStatementIndex = pc_;
    forStack_.push_back(frame);
    return;
  }

  if (cmd == "next") {
    if (!forStack_.empty()) {
      auto &frame = forStack_.back();
      // Notice in `00.utf:165-173` (`*flash1`), the script has BOTH
      // `for %0=1 to 3` AND `add %0,1` before `next`, whereas `*mute` (`197`)
      // and `*staff` (`324`) rely on `next` adding `step`!
      // In PONscripter (`ScriptParser.cpp`), `nextCommand` ALWAYS adds `step`
      // to the loop variable and loops while:
      // `(step > 0 && val <= endValue) || (step < 0 && val >= endValue)`.
      const int nextVal = vars_.getNum(frame.varId) + frame.step;
      const bool gChanged = vars_.setNum(frame.varId, nextVal);
      if (gChanged && callbacks_)
        callbacks_->onPersistGlobals();

      const bool keepGoing = (frame.step >= 0) ? (nextVal <= frame.endValue)
                                               : (nextVal >= frame.endValue);
      if (keepGoing) {
        pc_ = frame.bodyStatementIndex;
      } else {
        forStack_.pop_back();
      }
    }
    return;
  }

  if (cmd == "break") {
    if (!forStack_.empty()) {
      forStack_.pop_back();
      // Advance pc_ past matching `next`
      const auto &stmts = script_.statements();
      int depth = 1;
      while (pc_ < stmts.size() && depth > 0) {
        LineLexer scan(stmts[pc_].text, &vars_);
        const std::string c = scan.parseIdentifier();
        if (c == "for")
          ++depth;
        else if (c == "next")
          --depth;
        ++pc_;
      }
    }
    return;
  }

  if (cmd == "select" || cmd == "selgosub" || cmd == "selnum") {
    selectOptions_.clear();
    selectionMode_ = cmd == "selgosub" ? SelectionMode::Subroutine :
                     cmd == "selnum" ? SelectionMode::Number :
                                       SelectionMode::Jump;
    selectionNumberVariable_ = -1;
    if (selectionMode_ == SelectionMode::Number) {
      selectionNumberVariable_ = lexer.parseVarRef();
      if (!lexer.consumeComma())
        return;
    }

    // selnum contains text-only options; select/selgosub use text/label pairs.
    while (true) {
      lexer.skipWhitespace();
      if (lexer.eof())
        break;
      SelectOption opt;
      const std::size_t before = lexer.position();
      opt.text = TextFormatter::formatUiLabel(lexer.parseStringExpr(), true);
      if (lexer.position() == before)
        break;
      if (selectionMode_ != SelectionMode::Number) {
        if (!lexer.consumeComma())
          break;
        opt.targetLabel = lexer.parseLabelTarget();
      }
      if (!opt.text.empty() ||
          (selectionMode_ != SelectionMode::Number && !opt.targetLabel.empty()))
        selectOptions_.push_back(std::move(opt));
      if (!lexer.consumeComma())
        break;
    }

    if (selectOptions_.empty())
      return;
    enterTextDisplayMode();
    typewriterChars_ = totalPageChars_;
    waitState_ = WaitState::WaitSelect;
    if (callbacks_)
      callbacks_->onSceneDirty();
    return;
  }

  if (cmd == "csel") {
    selectionMode_ = SelectionMode::Jump;
    selectionNumberVariable_ = -1;
    cselOptions_.clear();
    selectOptions_.clear();
    while (!lexer.eof()) {
      lexer.skipWhitespace();
      if (lexer.eof())
        break;
      SelectOption opt;
      const auto before = lexer.position();
      opt.text = TextFormatter::formatUiLabel(lexer.parseStringExpr(), true);
      if (lexer.position() == before || !lexer.consumeComma())
        break;
      opt.targetLabel = lexer.parseLabelTarget();
      if (!opt.targetLabel.empty())
        cselOptions_.push_back(std::move(opt));
      if (!lexer.consumeComma())
        break;
    }
    // A custom selection is controlled by the script's own UI. It does not
    // block in WaitSelect or display the built-in choice list.
    if (!cselOptions_.empty() && script_.hasLabel("customsel")) {
      jumpToLabel("customsel");
    } else if (!cselOptions_.empty()) {
      // Graceful fallback for scripts lacking the customsel routine.
      selectOptions_ = cselOptions_;
      enterTextDisplayMode();
      waitState_ = WaitState::WaitSelect;
    }
    return;
  }

  if (cmd == "cselbtn") {
    const int index = lexer.parseIntExpr();
    if (!lexer.consumeComma())
      return;
    const int buttonId = lexer.parseIntExpr();
    if (!lexer.consumeComma())
      return;
    const int x = lexer.parseIntExpr();
    if (!lexer.consumeComma())
      return;
    const int y = lexer.parseIntExpr();
    if (index < 0 || index >= static_cast<int>(cselOptions_.size()) ||
        buttonId <= 0)
      return;

    ButtonDef button;
    button.kind = ButtonDef::Kind::TextBtn;
    button.spriteNo = index;
    button.id = buttonId;
    button.x = x;
    button.y = y;
    button.text = cselOptions_[index].text;
    button.w = std::max(1, static_cast<int>(utf8Length(button.text))) *
               std::max(1, window_.fontW + window_.pitchX);
    button.h = std::max(1, window_.fontH + window_.pitchY);
    const auto existing = std::find_if(
        buttons_.begin(), buttons_.end(),
        [&](const ButtonDef &item) { return item.id == buttonId; });
    if (existing != buttons_.end())
      *existing = std::move(button);
    else
      buttons_.push_back(std::move(button));
    if (callbacks_)
      callbacks_->onSceneDirty();
    return;
  }

  if (cmd == "getcselnum") {
    const int varId = lexer.parseVarRef();
    if (varId >= 0)
      vars_.setNum(varId, static_cast<int>(cselOptions_.size()));
    return;
  }

  if (cmd == "getcselstr") {
    const int strId = lexer.parseStrVarRef();
    lexer.consumeComma();
    const int idx = lexer.parseIntExpr();
    if (strId >= 0 && idx >= 0 && idx < static_cast<int>(cselOptions_.size())) {
      vars_.setStr(strId, cselOptions_[idx].text);
    }
    return;
  }

  if (cmd == "cselgoto") {
    const int idx = lexer.parseIntExpr();
    if (idx >= 0 && idx < static_cast<int>(cselOptions_.size())) {
      const std::string target = cselOptions_[idx].targetLabel;
      if (script_.hasLabel(target)) {
        cselOptions_.clear();
        selectOptions_.clear();
        buttons_.clear();
        hoveredButtonId_ = 0;
        clearCurrentPage(true);
        jumpToLabel(target);
        if (callbacks_)
          callbacks_->onSceneDirty();
      }
    }
    return;
  }

  if (cmd == "tablegoto" || cmd == "tablegoto1" ||
      cmd == "debugtablegoto") {
    const int index = lexer.parseIntExpr();
    int current = cmd == "tablegoto1" ? 1 : 0;
    while (lexer.consumeComma()) {
      const std::string target = lexer.parseLabelTarget();
      if (current == index && !target.empty()) {
        jumpToLabel(target);
        return;
      }
      ++current;
    }
    return;
  }

  if (cmd == "skip") {
    const int delta = lexer.parseIntExpr();
    const auto &stmts = script_.statements();
    if (pc_ > 0 && pc_ <= stmts.size()) {
      const std::int64_t targetLine =
          static_cast<std::int64_t>(stmts[pc_ - 1].lineIndex) + delta;
      for (std::size_t i = 0; i < stmts.size(); ++i) {
        if (static_cast<std::int64_t>(stmts[i].lineIndex) >= targetLine) {
          pc_ = i;
          break;
        }
      }
    }
    return;
  }
}

void PonscripterVm::cmdVars(LineLexer &lexer, const std::string &cmd) {
  auto notifyIfGlobal = [this](bool globalChanged) {
    if (globalChanged && callbacks_)
      callbacks_->onPersistGlobals();
  };

  if (cmd == "len") {
    const int dest = lexer.parseVarRef();
    if (!lexer.consumeComma())
      return;
    const std::string source = lexer.parseStringExpr();
    if (dest >= 0)
      notifyIfGlobal(vars_.setNum(dest, static_cast<int>(
          std::min<std::size_t>(utf8Length(source),
                                static_cast<std::size_t>(INT_MAX)))));
    return;
  }

  if (cmd == "mid") {
    const int dest = lexer.parseStrVarRef();
    if (!lexer.consumeComma())
      return;
    const std::string source = lexer.parseStringExpr();
    if (!lexer.consumeComma())
      return;
    const auto start = static_cast<std::size_t>(
        std::max(0, lexer.parseIntExpr()));
    if (!lexer.consumeComma())
      return;
    const auto count = static_cast<std::size_t>(
        std::max(0, lexer.parseIntExpr()));
    const std::size_t begin = utf8Offset(source, start);
    const std::size_t length = utf8Offset(
        std::string_view(source).substr(begin), count);
    if (dest >= 0)
      vars_.setStr(dest, source.substr(begin, length));
    return;
  }

  if (cmd.rfind("mov", 0) == 0) {
    lexer.skipWhitespace();
    if (lexer.peek() == '$') {
      const int strId = lexer.parseStrVarRef();
      lexer.consumeComma();
      vars_.setStr(strId, lexer.parseStringExpr());
      return;
    }
    if (lexer.peek() == '?') {
      lexer.get();
      int arrId = 0;
      const std::string tok = lexer.parseIdentifier();
      if (!tok.empty() && std::isdigit(static_cast<unsigned char>(tok[0])))
        arrId = std::stoi(tok);
      else
        vars_.findNumAlias(tok, arrId);
      std::vector<int> indices;
      while (lexer.peek() == '[') {
        lexer.get();
        indices.push_back(lexer.parseIntExpr());
        lexer.skipWhitespace();
        if (lexer.peek() == ']')
          lexer.get();
      }
      const bool firstArgument = lexer.consumeComma();
      if (cmd == "movl" || cmd == "movz") {
        const int count = vars_.remainingArrayElements(arrId, indices);
        if (count <= 0)
          return;
        const int start = indices.back();
        bool argumentAvailable = firstArgument;
        for (int i = 0; i < count; ++i) {
          if (!argumentAvailable && cmd == "movl")
            break;
          indices.back() = start + i;
          vars_.setArray(arrId, indices,
                         argumentAvailable ? lexer.parseIntExpr() : 0);
          if (argumentAvailable)
            argumentAvailable = lexer.consumeComma();
        }
      } else if (firstArgument) {
        vars_.setArray(arrId, indices, lexer.parseIntExpr());
      }
      return;
    }

    const int startVar = lexer.parseVarRef();
    int offset = 0;
    bool anyGlobal = false;
    while (lexer.consumeComma()) {
      const int val = lexer.parseIntExpr();
      if (startVar >= 0) {
        anyGlobal = vars_.setNum(startVar + offset, val) || anyGlobal;
      }
      ++offset;
    }
    notifyIfGlobal(anyGlobal);
    return;
  }

  if (cmd == "inc" || cmd == "dec") {
    const int varId = lexer.parseVarRef();
    if (varId >= 0) {
      const int delta = (cmd == "inc") ? 1 : -1;
      notifyIfGlobal(vars_.setNum(varId, vars_.getNum(varId) + delta));
    }
    return;
  }

  if (cmd == "add") {
    lexer.skipWhitespace();
    if (lexer.peek() == '$') {
      const int strId = lexer.parseStrVarRef();
      lexer.consumeComma();
      vars_.setStr(strId, vars_.getStr(strId) + lexer.parseStringExpr());
      return;
    }
    const int varId = lexer.parseVarRef();
    lexer.consumeComma();
    const int rhs = lexer.parseIntExpr();
    if (varId >= 0)
      notifyIfGlobal(vars_.setNum(varId, vars_.getNum(varId) + rhs));
    return;
  }

  if (cmd == "sub") {
    const int varId = lexer.parseVarRef();
    lexer.consumeComma();
    const int rhs = lexer.parseIntExpr();
    if (varId >= 0)
      notifyIfGlobal(vars_.setNum(varId, vars_.getNum(varId) - rhs));
    return;
  }

  if (cmd == "mul") {
    const int varId = lexer.parseVarRef();
    lexer.consumeComma();
    const int rhs = lexer.parseIntExpr();
    if (varId >= 0)
      notifyIfGlobal(vars_.setNum(varId, vars_.getNum(varId) * rhs));
    return;
  }

  if (cmd == "div" || cmd == "mod") {
    const int varId = lexer.parseVarRef();
    lexer.consumeComma();
    const int rhs = lexer.parseIntExpr();
    if (varId >= 0 && rhs != 0) {
      const int cur = vars_.getNum(varId);
      const int next = (cmd == "div") ? (cur / rhs) : (cur % rhs);
      notifyIfGlobal(vars_.setNum(varId, next));
    }
    return;
  }

  if (cmd == "rnd") {
    const int varId = lexer.parseVarRef();
    lexer.consumeComma();
    const int maxVal = std::max(1, lexer.parseIntExpr());
    if (varId >= 0)
      notifyIfGlobal(vars_.setNum(varId, std::rand() % maxVal));
    return;
  }

  if (cmd == "rnd2") {
    const int varId = lexer.parseVarRef();
    lexer.consumeComma();
    const int lo = lexer.parseIntExpr();
    lexer.consumeComma();
    const int hi = lexer.parseIntExpr();
    if (varId >= 0) {
      const int span = std::max(1, hi - lo + 1);
      notifyIfGlobal(vars_.setNum(varId, lo + (std::rand() % span)));
    }
    return;
  }

  if (cmd == "cmp") {
    const int varId = lexer.parseVarRef();
    lexer.consumeComma();
    const std::string a = lexer.parseStringExpr();
    lexer.consumeComma();
    const std::string b = lexer.parseStringExpr();
    if (varId >= 0) {
      const int res = (a == b) ? 0 : (a > b ? 1 : -1);
      notifyIfGlobal(vars_.setNum(varId, res));
    }
    return;
  }

  if (cmd == "atoi") {
    const int varId = lexer.parseVarRef();
    lexer.consumeComma();
    const std::string s = lexer.parseStringExpr();
    if (varId >= 0)
      notifyIfGlobal(vars_.setNum(varId, std::atoi(s.c_str())));
    return;
  }

  if (cmd == "itoa" || cmd == "itoa2") {
    const int strId = lexer.parseStrVarRef();
    lexer.consumeComma();
    const int val = lexer.parseIntExpr();
    vars_.setStr(strId, std::to_string(val));
    return;
  }

  if (cmd == "intlimit") {
    const int varId = lexer.parseIntExpr();
    lexer.consumeComma();
    const int lo = lexer.parseIntExpr();
    lexer.consumeComma();
    const int hi = lexer.parseIntExpr();
    if (varId >= 0)
      notifyIfGlobal(
          vars_.setNum(varId, std::clamp(vars_.getNum(varId), lo, hi)));
    return;
  }

  if (cmd == "date") {
    const std::time_t now = std::time(nullptr);
    const std::tm *lt = std::localtime(&now);
    const int yVar = lexer.parseVarRef();
    lexer.consumeComma();
    const int mVar = lexer.parseVarRef();
    lexer.consumeComma();
    const int dVar = lexer.parseVarRef();
    if (lt) {
      if (yVar >= 0)
        vars_.setNum(yVar, (lt->tm_year + 1900) % 100);
      if (mVar >= 0)
        vars_.setNum(mVar, lt->tm_mon + 1);
      if (dVar >= 0)
        vars_.setNum(dVar, lt->tm_mday);
    }
    return;
  }

  if (cmd == "time") {
    const std::time_t now = std::time(nullptr);
    const std::tm *lt = std::localtime(&now);
    const int hVar = lexer.parseVarRef();
    lexer.consumeComma();
    const int mVar = lexer.parseVarRef();
    lexer.consumeComma();
    const int sVar = lexer.parseVarRef();
    if (lt) {
      if (hVar >= 0)
        vars_.setNum(hVar, lt->tm_hour);
      if (mVar >= 0)
        vars_.setNum(mVar, lt->tm_min);
      if (sVar >= 0)
        vars_.setNum(sVar, lt->tm_sec);
    }
    return;
  }

  if (cmd == "savetime") {
    const int slot = lexer.parseIntExpr();
    lexer.consumeComma();
    const int exVar = lexer.parseVarRef();
    int mVar = -1, dVar = -1, hVar = -1, minVar = -1;
    if (lexer.consumeComma())
      mVar = lexer.parseVarRef();
    if (lexer.consumeComma())
      dVar = lexer.parseVarRef();
    if (lexer.consumeComma())
      hVar = lexer.parseVarRef();
    if (lexer.consumeComma())
      minVar = lexer.parseVarRef();
    SaveTimestamp ts;
    if (callbacks_)
      ts = callbacks_->querySaveSlot(slot);
    if (exVar >= 0)
      vars_.setNum(exVar, ts.exists ? 1 : 0);
    if (mVar >= 0)
      vars_.setNum(mVar, ts.month);
    if (dVar >= 0)
      vars_.setNum(dVar, ts.day);
    if (hVar >= 0)
      vars_.setNum(hVar, ts.hour);
    if (minVar >= 0)
      vars_.setNum(minVar, ts.minute);
    return;
  }
}

void PonscripterVm::cmdGraphics(LineLexer &lexer, const std::string &cmd) {
  auto queueManual = [this](ManualDrawOp op) {
    constexpr std::size_t kMaxDrawOps = 4096;
    if (manualDraftOps_.size() < kMaxDrawOps)
      manualDraftOps_.push_back(std::move(op));
  };
  if (cmd == "spstr") {
    const std::string controls = lexer.parseStringExpr();
    std::size_t pos = 0;
    bool dirty = false;
    auto readNumber = [&](int &value) {
      const std::size_t start = pos;
      if (pos < controls.size() && controls[pos] == '-')
        ++pos;
      const std::size_t firstDigit = pos;
      while (pos < controls.size() &&
             std::isdigit(static_cast<unsigned char>(controls[pos])))
        ++pos;
      if (pos == firstDigit) {
        pos = start;
        return false;
      }
      const auto result = std::from_chars(
          controls.data() + start, controls.data() + pos, value);
      return result.ec == std::errc{} &&
             result.ptr == controls.data() + pos;
    };
    auto consume = [&](char delimiter) {
      if (pos < controls.size() && controls[pos] == delimiter) {
        ++pos;
        return true;
      }
      return false;
    };

    // Cn[-m] hides a range, Pn[,cell] shows a cell, Mn,x,y moves a
    // sprite, and Sn,(file) plays a sample through channel n.
    while (pos < controls.size()) {
      const char command = static_cast<char>(std::tolower(
          static_cast<unsigned char>(controls[pos++])));
      int slot = 0;
      if (command == 'c') {
        if (!readNumber(slot))
          continue;
        int end = slot;
        if (consume('-') && !readNumber(end))
          continue;
        const int begin = std::max(0, slot);
        end = std::min(end, kMaxExtendedSprites - 1);
        if (begin > end)
          continue;
        for (int index = begin; index <= end; ++index) {
          sprites_[index].visible = false;
          dirty = true;
        }
      } else if (command == 'p') {
        if (!readNumber(slot))
          continue;
        int cell = 0;
        if (consume(',') && !readNumber(cell))
          continue;
        if (slot >= 0 && slot < kMaxExtendedSprites &&
            sprites_[slot].loaded) {
          auto &sprite = sprites_[slot];
          sprite.visible = true;
          sprite.currentCell = std::clamp(
              cell, 0, std::max(0, sprite.spec.numCells - 1));
          dirty = true;
        }
      } else if (command == 'm') {
        int x = 0, y = 0;
        if (!readNumber(slot) || !consume(',') || !readNumber(x) ||
            !consume(',') || !readNumber(y))
          continue;
        if (slot >= 0 && slot < kMaxExtendedSprites &&
            sprites_[slot].loaded) {
          sprites_[slot].x = x;
          sprites_[slot].y = y;
          dirty = true;
        }
      } else if (command == 's') {
        if (!readNumber(slot) || !consume(',') || !consume('('))
          continue;
        const std::size_t end = controls.find(')', pos);
        if (end == std::string::npos)
          break;
        std::string path = controls.substr(pos, end - pos);
        std::replace(path.begin(), path.end(), '\\', '/');
        pos = end + 1;
        if (!path.empty()) {
          slot = std::clamp(slot, 0, 49);
          lastSePath_ = path;
          if (callbacks_)
            callbacks_->onPlayWave(slot, path, false);
        }
      }
    }
    if (dirty && callbacks_)
      callbacks_->onSceneDirty();
    return;
  }
  if (cmd == "bgcopy" || cmd == "bgcpy") {
    // Snapshot a scene description rather than reading back pixels every
    // frame. These immutable layers remain valid when the live sprites move.
    std::vector<ManualDrawOp> captured;
    if (manualFrameVisible_) {
      captured = manualDisplayOps_;
    } else {
      if (!backgroundCopyOps_.empty()) {
        captured = backgroundCopyOps_;
      } else {
        ManualDrawOp background;
        background.kind = ManualDrawOp::Kind::Background;
        background.spec = bg_;
        captured.push_back(std::move(background));
      }
      for (int i : {0, 2, 1}) {
        const auto &stand = standing_[i];
        if (!stand.loaded || stand.alpha <= 0)
          continue;
        ManualDrawOp op;
        op.kind = ManualDrawOp::Kind::Sprite;
        op.spec = stand.spec;
        op.x = stand.x;
        op.y = stand.y;
        op.alpha = stand.alpha;
        captured.push_back(std::move(op));
      }
      for (int i = kMaxExtendedSprites - 1; i >= 0; --i) {
        const auto &sprite = sprites_[i];
        if (!sprite.loaded || !sprite.visible || sprite.alpha <= 0 ||
            areSpritesHidden(sprite.secondaryLayer))
          continue;
        ManualDrawOp op;
        op.kind = ManualDrawOp::Kind::Sprite;
        op.spec = sprite.spec;
        op.x = sprite.x;
        op.y = sprite.y;
        op.cell = sprite.currentCell;
        op.alpha = sprite.alpha;
        captured.push_back(std::move(op));
      }
      if (textWindowVisible_ && !pagePlainText_.empty()) {
        ManualDrawOp op;
        op.kind = ManualDrawOp::Kind::Text;
        op.text = pagePlainText_;
        op.x = window_.textX;
        op.y = window_.textY;
        op.width = window_.textAreaWidth();
        op.height = window_.fontH;
        op.colorRgb = currentTextStyle_.colorRgb;
        captured.push_back(std::move(op));
      }
    }
    if (captured.size() > 4096)
      captured.resize(4096);
    backgroundCopyOps_ = std::move(captured);
    if (callbacks_)
      callbacks_->onSceneDirty();
    return;
  }
  if (cmd == "drawclear" || cmd == "drawfill") {
    ManualDrawOp op;
    op.kind = cmd == "drawclear" ? ManualDrawOp::Kind::Clear
                                 : ManualDrawOp::Kind::Fill;
    if (cmd == "drawfill") {
      const int r = std::clamp(lexer.parseIntExpr(), 0, 255);
      lexer.consumeComma();
      const int g = std::clamp(lexer.parseIntExpr(), 0, 255);
      lexer.consumeComma();
      const int b = std::clamp(lexer.parseIntExpr(), 0, 255);
      op.colorRgb = (static_cast<std::uint32_t>(r) << 16) |
                    (static_cast<std::uint32_t>(g) << 8) |
                    static_cast<std::uint32_t>(b);
    }
    // A full-screen clear supersedes everything previously drawn on
    // the accumulation buffer, but not the frame already being shown.
    manualDraftOps_.clear();
    queueManual(std::move(op));
    return;
  }
  if (cmd == "drawbg" || cmd == "drawbg2") {
    ManualDrawOp op;
    op.kind = ManualDrawOp::Kind::Background;
    op.spec = bg_;
    if (cmd == "drawbg2") {
      op.x = lexer.parseIntExpr();
      lexer.consumeComma();
      op.y = lexer.parseIntExpr();
      lexer.consumeComma();
      op.scaleX = lexer.parseIntExpr();
      lexer.consumeComma();
      op.scaleY = lexer.parseIntExpr();
      lexer.consumeComma();
      op.rotation = lexer.parseIntExpr();
    }
    queueManual(std::move(op));
    return;
  }
  if (cmd == "drawsp" || cmd == "drawsp2" || cmd == "drawsp3") {
    const int spriteNo = lexer.parseIntExpr();
    if (!lexer.consumeComma())
      return;
    const int cellNo = lexer.parseIntExpr();
    if (!lexer.consumeComma())
      return;
    const int opacity = std::clamp(lexer.parseIntExpr(), 0, 256);
    if (!lexer.consumeComma())
      return;
    const int x = lexer.parseIntExpr();
    if (!lexer.consumeComma())
      return;
    const int y = lexer.parseIntExpr();
    if (spriteNo < 0 || spriteNo >= kMaxExtendedSprites ||
        !sprites_[spriteNo].loaded)
      return;
    ManualDrawOp op;
    op.kind = ManualDrawOp::Kind::Sprite;
    op.spec = sprites_[spriteNo].spec;
    op.x = x;
    op.y = y;
    op.cell = std::clamp(cellNo, 0, std::max(0, op.spec.numCells - 1));
    op.alpha = (opacity * 255) / 256;
    if (cmd == "drawsp2") {
      if (!lexer.consumeComma())
        return;
      op.scaleX = lexer.parseIntExpr();
      if (!lexer.consumeComma())
        return;
      op.scaleY = lexer.parseIntExpr();
      if (!lexer.consumeComma())
        return;
      op.rotation = lexer.parseIntExpr();
    } else if (cmd == "drawsp3") {
      op.affine = true;
      if (!lexer.consumeComma())
        return;
      op.transformA = lexer.parseIntExpr();
      if (!lexer.consumeComma())
        return;
      op.transformB = lexer.parseIntExpr();
      if (!lexer.consumeComma())
        return;
      op.transformC = lexer.parseIntExpr();
      if (!lexer.consumeComma())
        return;
      op.transformD = lexer.parseIntExpr();
    }
    queueManual(std::move(op));
    return;
  }
  if (cmd == "drawtext") {
    ManualDrawOp op;
    op.kind = ManualDrawOp::Kind::Text;
    op.text = pagePlainText_;
    op.x = window_.textX;
    op.y = window_.textY;
    op.width = window_.textAreaWidth();
    op.height = window_.fontH;
    op.colorRgb = currentTextStyle_.colorRgb;
    queueManual(std::move(op));
    return;
  }
  if (cmd == "blt") {
    ManualDrawOp op;
    op.kind = ManualDrawOp::Kind::Blit;
    op.spec = btndefSpec_;
    op.x = lexer.parseIntExpr();
    lexer.consumeComma();
    op.y = lexer.parseIntExpr();
    lexer.consumeComma();
    op.width = lexer.parseIntExpr();
    lexer.consumeComma();
    op.height = lexer.parseIntExpr();
    lexer.consumeComma();
    op.srcX = lexer.parseIntExpr();
    lexer.consumeComma();
    op.srcY = lexer.parseIntExpr();
    lexer.consumeComma();
    op.srcW = lexer.parseIntExpr();
    lexer.consumeComma();
    op.srcH = lexer.parseIntExpr();
    if (!op.spec.imagePath.empty() && op.width > 0 && op.height > 0 &&
        op.srcW > 0 && op.srcH > 0)
      queueManual(std::move(op));
    // blt is an immediate copy into the currently displayed image.
    manualDisplayOps_ = manualDraftOps_;
    manualFrameVisible_ = true;
    if (callbacks_)
      callbacks_->onSceneDirty();
    return;
  }
  if (cmd == "draw") {
    manualDisplayOps_ = manualDraftOps_;
    manualFrameVisible_ = true;
    if (callbacks_)
      callbacks_->onSceneDirty();
    return;
  }
  if (cmd == "monocro") {
    const std::string arg = lexer.parseRawArg();
    if (arg == "off") {
      monochromeEnabled_ = false;
    } else {
      std::uint32_t rgb = 0xFFFFFFu;
      if (arg == "black")
        rgb = 0x000000u;
      else if (arg == "white")
        rgb = 0xFFFFFFu;
      else if (!parseHexColor24(arg, rgb))
        return;
      monochromeColor_ = rgb;
      monochromeEnabled_ = true;
    }
    if (callbacks_)
      callbacks_->onSceneDirty();
    return;
  }
  if (cmd == "nega") {
    negativeMode_ = std::clamp(lexer.parseIntExpr(), 0, 2);
    if (callbacks_)
      callbacks_->onSceneDirty();
    return;
  }
  if (cmd == "quake" || cmd == "quakex" || cmd == "quakey") {
    const int amplitude = std::max(0, lexer.parseIntExpr());
    if (!lexer.consumeComma())
      return;
    const int duration = std::max(0, lexer.parseIntExpr());
    quakeAmplitude_ = amplitude;
    EffectDef shake;
    shake.builtinEffect = cmd == "quakey" ? 10001 :
                          cmd == "quakex" ? 10002 : 10003;
    shake.durationMs = std::max(duration, amplitude * 4);
    triggerEffect(shake);
    return;
  }
  if (cmd == "allsphide" || cmd == "allspresume" ||
      cmd == "allsp2hide" || cmd == "allsp2resume") {
    const bool hide = (cmd == "allsphide" || cmd == "allsp2hide");
    if (cmd == "allsp2hide" || cmd == "allsp2resume")
      allSprites2Hidden_ = hide;
    else
      allSpritesHidden_ = hide;
    if (callbacks_)
      callbacks_->onSceneDirty();
    return;
  }

  if (cmd == "tal") {
    const std::string position = lexer.parseIdentifier();
    const int slot = position == "l" ? 0 : position == "c" ? 1 :
                     position == "r" ? 2 : -1;
    if (!lexer.consumeComma())
      return;
    const int opacity = std::clamp(lexer.parseIntExpr(), 0, 256);
    if (slot >= 0) {
      standing_[slot].alpha = (opacity * 255) / 256;
      if (lexer.consumeComma()) {
        const EffectDef effect = resolveEffectSpec(lexer, false);
        triggerEffect(effect);
      } else if (callbacks_) {
        callbacks_->onSceneDirty();
      }
    }
    return;
  }

  if (cmd == "strsp" || cmd == "strsph") {
    const int id = lexer.parseIntExpr();
    if (!lexer.consumeComma())
      return;
    const std::string text = lexer.parseStringExpr();
    if (!lexer.consumeComma())
      return;
    const int x = lexer.parseIntExpr();
    if (!lexer.consumeComma())
      return;
    const int y = lexer.parseIntExpr();

    // Size/spacing fields match strsp's dialogue font configuration.
    int cols = 1, rows = 1;
    int fontW = window_.fontW, fontH = window_.fontH;
    int spacingX = window_.pitchX, spacingY = window_.pitchY;
    if (lexer.consumeComma())
      cols = std::max(1, lexer.parseIntExpr());
    if (lexer.consumeComma())
      rows = std::max(1, lexer.parseIntExpr());
    if (lexer.consumeComma())
      fontW = std::max(1, lexer.parseIntExpr());
    if (lexer.consumeComma())
      fontH = std::max(1, lexer.parseIntExpr());
    if (lexer.consumeComma())
      spacingX = lexer.parseIntExpr();
    if (lexer.consumeComma())
      spacingY = lexer.parseIntExpr();
    if (lexer.consumeComma())
      lexer.parseIntExpr(); // bold
    if (lexer.consumeComma())
      lexer.parseIntExpr(); // shadow

    std::uint32_t color = 0xFFFFFFu;
    if (lexer.consumeComma())
      parseHexColor24(lexer.parseRawArg(), color);
    if (id >= 0 && id < kMaxExtendedSprites) {
      auto &slot = sprites_[id];
      slot = SpriteSlot{};
      slot.loaded = true;
      slot.visible = cmd == "strsp";
      slot.spec.trans = TransMode::String;
      slot.spec.text = TextFormatter::formatUiLabel(text, true);
      slot.spec.color = color;
      slot.spec.fontWidth = fontW;
      slot.spec.fontHeight = fontH;
      slot.spec.fontSpacing = spacingX;
      slot.x = x;
      slot.y = y;
      slot.cellWidth = std::max(1, cols * (fontW + spacingX));
      slot.cellHeight = std::max(1, rows * (fontH + spacingY));
      if (callbacks_)
        callbacks_->onSceneDirty();
    }
    return;
  }

  if (cmd == "bg") {
    backgroundCopyOps_.clear();
    manualFrameVisible_ = false;
    leaveTextDisplayMode(false);
    const std::string rawArg = lexer.parseRawArg();
    bg_ = parseTaggedString(rawArg, TransMode::Copy);
    if (!bg_.imagePath.empty()) {
      std::string lower = bg_.imagePath;
      std::transform(
          lower.begin(), lower.end(), lower.begin(),
          [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
      seenFiles_.insert(lower);
    }
    if (!lexer.eof()) {
      const EffectDef eff = resolveEffectSpec(lexer, true);
      triggerEffect(eff);
    } else if (callbacks_) {
      callbacks_->onSceneDirty();
    }
    return;
  }

  if (cmd == "ld") {
    leaveTextDisplayMode(false);
    const std::string posStr = lexer.parseIdentifier();
    int slotIdx = 1; // default center
    if (posStr == "l")
      slotIdx = 0;
    else if (posStr == "c")
      slotIdx = 1;
    else if (posStr == "r")
      slotIdx = 2;

    lexer.consumeComma();
    const std::string tagStr = lexer.parseStringExpr();
    auto &slot = standing_[slotIdx];
    slot.loaded = true;
    slot.spec = parseTaggedString(tagStr, TransMode::Alpha);
    populateStandingDimensions(slot, slotIdx);
    if (!slot.spec.imagePath.empty()) {
      std::string lower = slot.spec.imagePath;
      std::transform(
          lower.begin(), lower.end(), lower.begin(),
          [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
      seenFiles_.insert(lower);
    }

    if (!lexer.eof()) {
      const EffectDef eff = resolveEffectSpec(lexer, true);
      triggerEffect(eff);
    } else if (callbacks_) {
      callbacks_->onSceneDirty();
    }
    return;
  }

  if (cmd == "cl") {
    leaveTextDisplayMode(false);
    const std::string posStr = lexer.parseIdentifier();
    if (posStr == "a") {
      for (auto &s : standing_)
        s = StandingSlot{};
    } else if (posStr == "l") {
      standing_[0] = StandingSlot{};
    } else if (posStr == "c") {
      standing_[1] = StandingSlot{};
    } else if (posStr == "r") {
      standing_[2] = StandingSlot{};
    }

    if (!lexer.eof()) {
      const EffectDef eff = resolveEffectSpec(lexer, true);
      triggerEffect(eff);
    } else if (callbacks_) {
      callbacks_->onSceneDirty();
    }
    return;
  }

  if (cmd == "lsp" || cmd == "lsph" || cmd == "lsp2" || cmd == "lsph2" ||
      cmd == "lsp2add" || cmd == "lsp2sub" ||
      cmd == "lsph2add" || cmd == "lsph2sub") {
    const int id = lexer.parseIntExpr();
    lexer.consumeComma();
    const std::string tagStr = lexer.parseStringExpr();
    int x = 0, y = 0, alpha = 255;
    if (lexer.consumeComma())
      x = lexer.parseIntExpr();
    if (lexer.consumeComma())
      y = lexer.parseIntExpr();
    if (lexer.consumeComma())
      alpha = std::clamp(lexer.parseIntExpr(), 0, 255);

    if (id >= 0 && id < kMaxExtendedSprites) {
      auto &sp = sprites_[id];
      sp.loaded = true;
      sp.visible = (cmd.rfind("lsph", 0) != 0);
      sp.secondaryLayer = (cmd.find('2') != std::string::npos);
      sp.blendMode = cmd.find("add") != std::string::npos ? 1 :
                     cmd.find("sub") != std::string::npos ? 2 : 0;
      sp.spec = parseTaggedString(tagStr, TransMode::Alpha);
      sp.x = x;
      sp.y = y;
      sp.alpha = alpha;
      sp.currentCell = 0;
      sp.animDir = 1;
      sp.animAccumMs = 0.0;
      populateSpriteDimensions(sp);
      if (callbacks_)
        callbacks_->onSceneDirty();
    }
    return;
  }

  if (cmd == "csp" || cmd == "csp2") {
    const int id = lexer.parseIntExpr();
    if (id < 0) {
      for (auto &sp : sprites_)
        sp = SpriteSlot{};
      buttons_.clear();
      hoveredButtonId_ = 0;
    } else if (id < kMaxExtendedSprites) {
      sprites_[id] = SpriteSlot{};
    }
    if (callbacks_)
      callbacks_->onSceneDirty();
    return;
  }

  if (cmd == "vsp" || cmd == "vsp2") {
    const int id = lexer.parseIntExpr();
    lexer.consumeComma();
    const int vis = lexer.parseIntExpr();
    if (id >= 0 && id < kMaxExtendedSprites) {
      sprites_[id].visible = (vis != 0);
      if (callbacks_)
        callbacks_->onSceneDirty();
    }
    return;
  }

  if (cmd == "msp" || cmd == "msp2") {
    const int id = lexer.parseIntExpr();
    lexer.consumeComma();
    const int dx = lexer.parseIntExpr();
    lexer.consumeComma();
    const int dy = lexer.parseIntExpr();
    int dAlpha = 0;
    if (lexer.consumeComma())
      dAlpha = lexer.parseIntExpr();
    if (id >= 0 && id < kMaxExtendedSprites) {
      sprites_[id].x += dx;
      sprites_[id].y += dy;
      sprites_[id].alpha = std::clamp(sprites_[id].alpha + dAlpha, 0, 255);
      if (callbacks_)
        callbacks_->onSceneDirty();
    }
    return;
  }

  if (cmd == "amsp" || cmd == "amsp2") {
    const int id = lexer.parseIntExpr();
    lexer.consumeComma();
    const int x = lexer.parseIntExpr();
    lexer.consumeComma();
    const int y = lexer.parseIntExpr();
    if (id >= 0 && id < kMaxExtendedSprites) {
      sprites_[id].x = x;
      sprites_[id].y = y;
      if (lexer.consumeComma())
        sprites_[id].alpha = std::clamp(lexer.parseIntExpr(), 0, 255);
      if (callbacks_)
        callbacks_->onSceneDirty();
    }
    return;
  }

  if (cmd == "cell") {
    const int id = lexer.parseIntExpr();
    lexer.consumeComma();
    const int cellNo = lexer.parseIntExpr();
    if (id >= 0 && id < kMaxExtendedSprites) {
      sprites_[id].currentCell =
          std::clamp(cellNo, 0, std::max(0, sprites_[id].spec.numCells - 1));
      if (callbacks_)
        callbacks_->onSceneDirty();
    }
    return;
  }

  if (cmd == "getspsize") {
    const int id = lexer.parseIntExpr();
    lexer.consumeComma();
    const int wVar = lexer.parseVarRef();
    lexer.consumeComma();
    const int hVar = lexer.parseVarRef();
    int cVar = -1;
    if (lexer.consumeComma())
      cVar = lexer.parseVarRef();
    if (id >= 0 && id < kMaxExtendedSprites) {
      if (wVar >= 0)
        vars_.setNum(wVar, sprites_[id].cellWidth);
      if (hVar >= 0)
        vars_.setNum(hVar, sprites_[id].cellHeight);
      if (cVar >= 0)
        vars_.setNum(cVar, sprites_[id].spec.numCells);
    }
    return;
  }

  if (cmd == "getspmode") {
    const int varId = lexer.parseVarRef();
    lexer.consumeComma();
    const int id = lexer.parseIntExpr();
    if (varId >= 0 && id >= 0 && id < kMaxExtendedSprites) {
      vars_.setNum(varId, sprites_[id].visible ? 1 : 0);
    }
    return;
  }

  if (cmd == "bar") {
    const int id = lexer.parseIntExpr();
    lexer.consumeComma();
    const int val = lexer.parseIntExpr();
    lexer.consumeComma();
    const int x = lexer.parseIntExpr();
    lexer.consumeComma();
    const int y = lexer.parseIntExpr();
    lexer.consumeComma();
    const int w = lexer.parseIntExpr();
    lexer.consumeComma();
    const int h = lexer.parseIntExpr();
    lexer.consumeComma();
    const int maxVal = std::max(1, lexer.parseIntExpr());
    std::uint32_t col = 0xFFFFFFu;
    if (lexer.consumeComma()) {
      const std::string rawCol = lexer.parseRawArg();
      parseHexColor24(rawCol, col);
    }
    if (id >= 0 && id < kMaxBars) {
      bars_[id] = BarGauge{true, val, x, y, w, h, maxVal, col};
      if (callbacks_)
        callbacks_->onSceneDirty();
    }
    return;
  }

  if (cmd == "barclear") {
    for (auto &b : bars_)
      b = BarGauge{};
    if (callbacks_)
      callbacks_->onSceneDirty();
    return;
  }

  if (cmd == "prnum") {
    const int id = lexer.parseIntExpr();
    lexer.consumeComma();
    const int val = lexer.parseIntExpr();
    lexer.consumeComma();
    const int x = lexer.parseIntExpr();
    lexer.consumeComma();
    const int y = lexer.parseIntExpr();
    lexer.consumeComma();
    const int fw = lexer.parseIntExpr();
    lexer.consumeComma();
    const int fh = lexer.parseIntExpr();
    std::uint32_t col = 0xFFFFFFu;
    if (lexer.consumeComma()) {
      parseHexColor24(lexer.parseRawArg(), col);
    }
    if (id >= 0 && id < kMaxPrnums) {
      prnums_[id] = PrintedNumber{true, val, x, y, fw, fh, col};
      if (callbacks_)
        callbacks_->onSceneDirty();
    }
    return;
  }

  if (cmd == "prnumclear") {
    for (auto &p : prnums_)
      p = PrintedNumber{};
    if (callbacks_)
      callbacks_->onSceneDirty();
    return;
  }
}

void PonscripterVm::cmdEffects(LineLexer &lexer, const std::string &cmd) {
  if (cmd == "effect") {
    EffectDef def;
    def.id = lexer.parseIntExpr();
    lexer.consumeComma();
    def.builtinEffect = lexer.parseIntExpr();
    if (lexer.consumeComma())
      def.durationMs = std::max(0, lexer.parseIntExpr());
    if (lexer.consumeComma())
      def.maskPath = lexer.parseStringExpr();
    userEffects_[def.id] = std::move(def);
    return;
  }
  if (cmd == "effectcut") {
    effectCut_ = true;
    return;
  }
  if (cmd == "windoweffect") {
    windowEffect_ = resolveEffectSpec(lexer, false);
    return;
  }
  if (cmd == "print") {
    manualFrameVisible_ = false;
    const EffectDef eff = resolveEffectSpec(lexer, false);
    triggerEffect(eff);
    return;
  }
}

void PonscripterVm::cmdTextAndWindow(LineLexer &lexer, const std::string &cmd) {
  if (cmd == "h_mapfont" || cmd == "pmapfont") {
    const int slot = lexer.parseIntExpr();
    lexer.consumeComma();
    std::string path = lexer.parseStringExpr();
    std::replace(path.begin(), path.end(), '\\', '/');
    if (slot >= 0 && slot < static_cast<int>(fontMap_.size())) {
      fontMap_[slot] = std::move(path);
    }
    return;
  }

  if (cmd == "setwindow" || cmd == "setwindow3") {
    // Clear current page and push to backlog before reconfiguring window
    clearCurrentPage(true);

    window_.textX = lexer.parseIntExpr();
    lexer.consumeComma();
    window_.textY = lexer.parseIntExpr();
    lexer.consumeComma();
    window_.cols = std::max(1, lexer.parseIntExpr());
    lexer.consumeComma();
    window_.rows = std::max(1, lexer.parseIntExpr());
    lexer.consumeComma();
    window_.fontW = std::max(8, lexer.parseIntExpr());
    lexer.consumeComma();
    window_.fontH = std::max(8, lexer.parseIntExpr());
    lexer.consumeComma();
    window_.pitchX = lexer.parseIntExpr();
    lexer.consumeComma();
    window_.pitchY = lexer.parseIntExpr();
    lexer.consumeComma();
    window_.textSpeed = lexer.parseIntExpr();
    lexer.consumeComma();
    window_.bold = (lexer.parseIntExpr() != 0);
    lexer.consumeComma();
    window_.shadow = (lexer.parseIntExpr() != 0);
    if (lexer.consumeComma()) {
      const std::string bgTag = lexer.parseRawArg();
      window_.backdrop = parseTaggedString(bgTag, TransMode::Alpha);
    }
    if (lexer.consumeComma())
      window_.winX = lexer.parseIntExpr();
    if (lexer.consumeComma())
      window_.winY = lexer.parseIntExpr();
    if (lexer.consumeComma())
      window_.winW = lexer.parseIntExpr() - window_.winX;
    if (lexer.consumeComma())
      window_.winH = lexer.parseIntExpr() - window_.winY;
    if (callbacks_)
      callbacks_->onSceneDirty();
    return;
  }

  if (cmd == "setcursor" || cmd == "abssetcursor") {
    const int idx = lexer.parseIntExpr();
    lexer.consumeComma();
    const std::string tagStr = lexer.parseStringExpr();
    int ox = 0, oy = 0;
    if (lexer.consumeComma())
      ox = lexer.parseIntExpr();
    if (lexer.consumeComma())
      oy = lexer.parseIntExpr();
    if (idx >= 0 && idx < 2) {
      cursors_[idx].configured = true;
      cursors_[idx].spec = parseTaggedString(tagStr, TransMode::Alpha);
      cursors_[idx].offsetX = ox;
      cursors_[idx].offsetY = oy;
      cursors_[idx].absolute = (cmd == "abssetcursor");
      cursors_[idx].currentCell = 0;
      cursors_[idx].animAccumMs = 0.0;
    }
    return;
  }

  if (cmd == "br" || cmd == "br2") {
    enterTextDisplayMode();
    TextSpan brSpan;
    brSpan.isLineBreak = true;
    brSpan.text = "\n";
    brSpan.style = currentTextStyle_;
    pageSpans_.push_back(std::move(brSpan));
    pagePlainText_.push_back('\n');
    return;
  }

  if (cmd == "textoff" || cmd == "erasetextwindow") {
    textWindowVisible_ = false;
    if (callbacks_)
      callbacks_->onSceneDirty();
    return;
  }

  if (cmd == "texton") {
    textWindowVisible_ = true;
    if (callbacks_)
      callbacks_->onSceneDirty();
    return;
  }

  if (cmd == "textclear") {
    clearCurrentPage(true);
    if (callbacks_)
      callbacks_->onSceneDirty();
    return;
  }

  if (cmd == "textspeed") {
    window_.textSpeed = lexer.parseIntExpr();
    return;
  }

  if (cmd == "automode_time") {
    autoModeTimeMs_ = std::max(0, lexer.parseIntExpr());
    return;
  }

  if (cmd == "skipoff") {
    skipMode_ = false;
    return;
  }

  if (cmd == "texec") {
    if (textGosubPagePending_) {
      clearCurrentPage(true);
      textGosubPagePending_ = false;
      if (callbacks_)
        callbacks_->onSceneDirty();
    }
    return;
  }

  if (cmd == "puttext") {
    const std::string s = lexer.parseStringExpr();
    executeTextLine(s, pc_ > 0 ? pc_ - 1 : 0);
    return;
  }
}

void PonscripterVm::cmdButtonsAndInput(LineLexer &lexer,
                                       const std::string &cmd) {
  if (cmd == "btndef") {
    const std::string raw = lexer.parseRawArg();
    if (raw == "clear" || raw.empty()) {
      btndefSpec_ = TaggedImageSpec{};
    } else {
      btndefSpec_ = parseTaggedString(raw, TransMode::Alpha);
    }
    buttons_.clear();
    hoveredButtonId_ = 0;
    return;
  }

  if (cmd == "btn") {
    ButtonDef b;
    b.kind = ButtonDef::Kind::RectBtn;
    b.id = lexer.parseIntExpr();
    lexer.consumeComma();
    b.x = lexer.parseIntExpr();
    lexer.consumeComma();
    b.y = lexer.parseIntExpr();
    lexer.consumeComma();
    b.w = lexer.parseIntExpr();
    lexer.consumeComma();
    b.h = lexer.parseIntExpr();
    if (lexer.consumeComma())
      b.srcX = lexer.parseIntExpr();
    if (lexer.consumeComma())
      b.srcY = lexer.parseIntExpr();

    // Replace existing button with same ID or append
    auto it = std::find_if(buttons_.begin(), buttons_.end(),
                           [&](const ButtonDef &d) { return d.id == b.id; });
    if (it != buttons_.end())
      *it = b;
    else
      buttons_.push_back(b);
    return;
  }

  if (cmd == "spbtn" || cmd == "exbtn") {
    ButtonDef b;
    b.kind = ButtonDef::Kind::SpriteBtn;
    b.spriteNo = lexer.parseIntExpr();
    lexer.consumeComma();
    b.id = lexer.parseIntExpr();
    if (cmd == "exbtn" && lexer.consumeComma())
      b.exbtnControl = lexer.parseStringExpr();

    auto it = std::find_if(buttons_.begin(), buttons_.end(),
                           [&](const ButtonDef &d) { return d.id == b.id; });
    if (it != buttons_.end())
      *it = b;
    else
      buttons_.push_back(b);
    return;
  }

  if (cmd == "btnwait" || cmd == "btnwait2") {
    btnwaitDestVar_ = lexer.parseVarRef();
    btnwaitPreserveButtons_ = (cmd == "btnwait2");
    updateButtonHover();
    waitState_ = WaitState::WaitButton;
    return;
  }

  if (cmd == "getmousepos") {
    const int xVar = lexer.parseVarRef();
    lexer.consumeComma();
    const int yVar = lexer.parseVarRef();
    if (xVar >= 0)
      vars_.setNum(xVar, lastClickX_ > 0 ? lastClickX_ : mouseX_);
    if (yVar >= 0)
      vars_.setNum(yVar, lastClickY_ > 0 ? lastClickY_ : mouseY_);
    return;
  }

  if (cmd == "click" || cmd == "lrclick") {
    waitState_ = WaitState::WaitClickBare;
    return;
  }

  if (cmd == "wait" || cmd == "delay" || cmd == "waittimer") {
    const int ms = std::max(0, lexer.parseIntExpr());
    if (ms <= 0 || (skipMode_ && cmd == "delay"))
      return;
    waitTargetMs_ = static_cast<double>(ms);
    waitElapsedMs_ = 0.0;
    waitSkippable_ = (cmd == "delay");
    waitState_ = WaitState::WaitTimer;
    return;
  }
}

void PonscripterVm::cmdAudioAndVideo(LineLexer &lexer, const std::string &cmd) {
  auto normalizeAudioPath = [](std::string p) {
    std::replace(p.begin(), p.end(), '\\', '/');
    while (!p.empty() && p.front() == '/')
      p.erase(p.begin());
    return p;
  };

  if (cmd == "mp3" || cmd == "mp3loop" || cmd == "mp3save" || cmd == "bgm" ||
      cmd == "bgmonce" || cmd == "play" || cmd == "playonce") {
    const std::string path = normalizeAudioPath(lexer.parseStringExpr());
    const bool loop =
        (cmd == "mp3loop" || cmd == "mp3save" || cmd == "bgm" || cmd == "play");
    activeBgm_ = path;
    activeBgmLoop_ = loop;
    if (callbacks_)
      callbacks_->onPlayBgm(path, loop, 0);
    return;
  }

  if (cmd == "mp3stop" || cmd == "mp3fadeout" || cmd == "bgmstop" ||
      cmd == "playstop") {
    int fadeOut = 0;
    if (!lexer.eof())
      fadeOut = std::max(0, lexer.parseIntExpr());
    activeBgm_.clear();
    activeBgmLoop_ = false;
    if (callbacks_)
      callbacks_->onStopBgm(fadeOut);
    return;
  }

  if (cmd == "wave" || cmd == "waveloop") {
    const std::string path = normalizeAudioPath(lexer.parseStringExpr());
    const bool loop = (cmd == "waveloop");
    if (loop)
      loopingWaves_[0] = path;
    else
      loopingWaves_.erase(0);
    if (callbacks_)
      callbacks_->onPlayWave(0, path, loop);
    return;
  }

  if (cmd == "wavestop") {
    loopingWaves_.erase(0);
    if (callbacks_)
      callbacks_->onStopWave(0);
    return;
  }

  if (cmd == "dwave" || cmd == "dwaveloop" || cmd == "dwaveload") {
    const int ch = std::clamp(lexer.parseIntExpr(), 0, 49);
    lexer.consumeComma();
    const std::string path = normalizeAudioPath(lexer.parseStringExpr());
    const bool loop = (cmd == "dwaveloop");
    if (loop)
      loopingWaves_[ch] = path;
    else
      loopingWaves_.erase(ch);
    if (ch == 0 || path.rfind("voice/", 0) == 0)
      lastVoicePath_ = path;
    else
      lastSePath_ = path;
    if (cmd != "dwaveload" && callbacks_)
      callbacks_->onPlayWave(ch, path, loop);
    return;
  }

  if (cmd == "dwavestop") {
    const int ch = lexer.eof() ? 0 : std::clamp(lexer.parseIntExpr(), 0, 49);
    loopingWaves_.erase(ch);
    if (callbacks_)
      callbacks_->onStopWave(ch);
    return;
  }

  if (cmd == "stop") {
    activeBgm_.clear();
    activeBgmLoop_ = false;
    loopingWaves_.clear();
    if (callbacks_) {
      callbacks_->onStopBgm(0);
      callbacks_->onStopWave(-1);
    }
    return;
  }

  if (cmd == "bgmvol" || cmd == "mp3vol" || cmd == "defmp3vol") {
    bgmVol_ = std::clamp(lexer.parseIntExpr(), 0, 100);
    if (callbacks_)
      callbacks_->onVolumeChanged(bgmVol_, seVol_, voiceVol_);
    return;
  }

  if (cmd == "sevol" || cmd == "defsevol") {
    seVol_ = std::clamp(lexer.parseIntExpr(), 0, 100);
    if (callbacks_)
      callbacks_->onVolumeChanged(bgmVol_, seVol_, voiceVol_);
    return;
  }

  if (cmd == "voicevol" || cmd == "defvoicevol") {
    voiceVol_ = std::clamp(lexer.parseIntExpr(), 0, 100);
    if (callbacks_)
      callbacks_->onVolumeChanged(bgmVol_, seVol_, voiceVol_);
    return;
  }

  if (cmd == "chvol") {
    const int ch = std::clamp(lexer.parseIntExpr(), 0, 49);
    lexer.consumeComma();
    const int vol = std::clamp(lexer.parseIntExpr(), 0, 100);
    if (callbacks_)
      callbacks_->onChannelVolumeChanged(ch, vol);
    return;
  }

  if (cmd == "getbgmvol" || cmd == "getmp3vol") {
    const int varId = lexer.parseVarRef();
    if (varId >= 0)
      vars_.setNum(varId, bgmVol_);
    return;
  }

  if (cmd == "getsevol") {
    const int varId = lexer.parseVarRef();
    if (varId >= 0)
      vars_.setNum(varId, seVol_);
    return;
  }

  if (cmd == "getvoicevol") {
    const int varId = lexer.parseVarRef();
    if (varId >= 0)
      vars_.setNum(varId, voiceVol_);
    return;
  }

  if (cmd == "mpegplay" || cmd == "avi" || cmd == "movie") {
    const std::string path = normalizeAudioPath(lexer.parseStringExpr());
    bool canCancel = true;
    if (lexer.consumeComma())
      canCancel = (lexer.parseIntExpr() != 0);
    if (skipMode_)
      return;
    waitState_ = WaitState::WaitVideo;
    if (callbacks_)
      callbacks_->onPlayVideo(path, canCancel);
    return;
  }
}

void PonscripterVm::cmdMenusAndSystem(LineLexer &lexer,
                                      const std::string &cmd) {
  if (cmd == "savename") {
    saveMenuTitle_ =
        TextFormatter::formatUiLabel(lexer.parseStringExpr(), true);
    if (lexer.consumeComma())
      loadMenuTitle_ =
          TextFormatter::formatUiLabel(lexer.parseStringExpr(), true);
    if (lexer.consumeComma())
      saveSlotPrefix_ =
          TextFormatter::formatUiLabel(lexer.parseStringExpr(), true);
    return;
  }

  if (cmd == "savenumber") {
    saveNumberCount_ = std::clamp(lexer.parseIntExpr(), 1, 99);
    return;
  }

  if (cmd == "rmenu") {
    rmenuEntries_.clear();
    // Parse pairs `"^label", command` (supporting empty spacer `"^",,` in
    // `00.utf:121`!)
    while (!lexer.eof()) {
      lexer.skipWhitespace();
      if (lexer.eof())
        break;
      RMenuEntry entry;
      entry.label = TextFormatter::formatUiLabel(lexer.parseStringExpr(), true);
      lexer.consumeComma();
      lexer.skipWhitespace();
      if (lexer.peek() == ',') {
        // Empty target command (`"^",,`) -> non-selectable spacer!
        entry.command.clear();
      } else {
        entry.command = lexer.parseIdentifier();
      }
      rmenuEntries_.push_back(std::move(entry));
      if (!lexer.consumeComma())
        break;
    }
    return;
  }

  if (cmd == "rmode") {
    rmode_ = (lexer.parseIntExpr() != 0);
    return;
  }

  if (cmd == "menuselectcolor") {
    parseHexColor24(lexer.parseRawArg(), rmenuHoverColor_);
    if (lexer.consumeComma())
      parseHexColor24(lexer.parseRawArg(), rmenuIdleColor_);
    if (lexer.consumeComma())
      parseHexColor24(lexer.parseRawArg(), rmenuDisabledColor_);
    return;
  }

  if (cmd == "selectcolor") {
    parseHexColor24(lexer.parseRawArg(), selectHoverColor_);
    if (lexer.consumeComma())
      parseHexColor24(lexer.parseRawArg(), selectIdleColor_);
    return;
  }

  if (cmd == "lookbackbutton") {
    for (int i = 0; i < 4; ++i) {
      if (i > 0 && !lexer.consumeComma())
        break;
      lookbackButtons_[i] =
          parseTaggedString(lexer.parseStringExpr(), TransMode::Alpha);
    }
    return;
  }

  if (cmd == "lookbackflush") {
    backlog_.clear();
    return;
  }

  if (cmd == "systemcall") {
    const std::string kind = lexer.parseIdentifier();
    if (kind == "reset") {
      resetToStart();
      return;
    }
    if (kind == "end") {
      waitState_ = WaitState::Halted;
      if (callbacks_) {
        callbacks_->onStopBgm(0);
        callbacks_->onStopWave(-1);
        callbacks_->onPersistGlobals();
      }
      return;
    }
    if (kind == "skip") {
      skipMode_ = true;
      return;
    }
    if (kind == "automode") {
      autoMode_ = true;
      return;
    }
    enterSystemCall(kind);
    return;
  }

  if (cmd == "isfull") {
    const int varId = lexer.parseVarRef();
    if (varId >= 0) {
      const bool full = callbacks_ ? callbacks_->queryFullscreen() : false;
      vars_.setNum(varId, full ? 1 : 0);
    }
    return;
  }

  if (cmd == "menu_window") {
    if (callbacks_)
      callbacks_->onFullscreenChanged(false);
    return;
  }

  if (cmd == "menu_full") {
    if (callbacks_)
      callbacks_->onFullscreenChanged(true);
    return;
  }

  if (cmd == "savegame" || cmd == "savegame2") {
    const int slot = lexer.parseIntExpr();
    std::string customTitle;
    if (lexer.consumeComma())
      customTitle = lexer.parseStringExpr();
    if (callbacks_)
      callbacks_->onSaveSlot(slot, customTitle);
    return;
  }

  if (cmd == "loadgame") {
    const int slot = lexer.parseIntExpr();
    if (callbacks_)
      callbacks_->onLoadSlot(slot);
    return;
  }

  if (cmd == "savefileexist") {
    const int varId = lexer.parseVarRef();
    lexer.consumeComma();
    const int slot = lexer.parseIntExpr();
    if (varId >= 0) {
      const SaveTimestamp ts =
          callbacks_ ? callbacks_->querySaveSlot(slot) : SaveTimestamp{};
      vars_.setNum(varId, ts.exists ? 1 : 0);
    }
    return;
  }

  if (cmd == "savescreenshot" || cmd == "savescreenshot2") {
    const std::string path = lexer.parseStringExpr();
    if (callbacks_)
      callbacks_->onSaveScreenshot(path);
    return;
  }
}

void PonscripterVm::cmdNoOp(LineLexer &lexer, const std::string &cmd) {
  (void)lexer;
  (void)cmd;
}

} // namespace Ponscripter
