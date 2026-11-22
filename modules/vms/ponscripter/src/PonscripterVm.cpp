// Copyright 2026 Kotonoha contributors. SPDX-License-Identifier: MIT
#include <Ponscripter/PonscripterVm.hpp>

#include <algorithm>
#include <cctype>
#include <cmath>

namespace Ponscripter {

PonscripterVm::PonscripterVm(HostCallbacks *callbacks) : callbacks_(callbacks) {
  registerCommands();
}

std::pair<int, int> PonscripterVm::quakeOffset() const {
  if (!activeEffect_.active || quakeAmplitude_ <= 0 ||
      activeEffect_.builtinEffect < 10001 ||
      activeEffect_.builtinEffect > 10003 ||
      activeEffect_.durationMs <= 0)
    return {0, 0};
  const double progress = std::clamp(
      activeEffect_.elapsedMs / activeEffect_.durationMs, 0.0, 1.0);
  const double amplitude = quakeAmplitude_ * (1.0 - progress);
  const int x = static_cast<int>(std::lround(
      amplitude * std::sin(activeEffect_.elapsedMs * 0.085)));
  const int y = static_cast<int>(std::lround(
      amplitude * std::cos(activeEffect_.elapsedMs * 0.077)));
  if (activeEffect_.builtinEffect == 10001)
    return {0, y};
  if (activeEffect_.builtinEffect == 10002)
    return {x, 0};
  return {x, y};
}

void PonscripterVm::clear() {
  script_.clear();
  vars_.resetAll();
  userDefsubs_.clear();
  pc_ = 0;
  currentLabel_.clear();
  waitState_ = WaitState::Running;
  inDefineBlock_ = true;
  callStack_.clear();
  forStack_.clear();
  userEffects_.clear();
  activeEffect_ = ActiveEffect{};
  quakeAmplitude_ = 0;
  monochromeEnabled_ = false;
  monochromeColor_ = 0xFFFFFFu;
  negativeMode_ = 0;
  bg_ = TaggedImageSpec{};
  for (auto &s : standing_)
    s = StandingSlot{};
  for (auto &sp : sprites_)
    sp = SpriteSlot{};
  allSpritesHidden_ = false;
  allSprites2Hidden_ = false;
  manualFrameVisible_ = false;
  manualDraftOps_.clear();
  manualDisplayOps_.clear();
  for (auto &b : bars_)
    b = BarGauge{};
  for (auto &p : prnums_)
    p = PrintedNumber{};
  buttons_.clear();
  pageSpans_.clear();
  pageLineKeys_.clear();
  pagePlainText_.clear();
  backlog_.clear();
  pretextGosubLabel_.clear();
  textGosubLabel_.clear();
  pendingPretextPc_ = ScriptBuffer::npos;
  textGosubPagePending_ = false;
  selectOptions_.clear();
  cselOptions_.clear();
  selectionMode_ = SelectionMode::Jump;
  selectionNumberVariable_ = -1;
  rmenuEntries_.clear();
  activeBgm_.clear();
  loopingWaves_.clear();
  lastVoicePath_.clear();
  lastSePath_.clear();
  currentSectionId_ = 0;
  preSystemCallWaitState_ = WaitState::Running;
}

void PonscripterVm::appendScriptFile(std::string_view fileName,
                                     std::string_view utf8Content) {
  script_.appendFile(fileName, utf8Content);
}

bool PonscripterVm::start() {
  script_.finalize();
  vars_.setGlobalBorder(script_.directives().globalVariableBorder);
  underlineValue_ = script_.directives().resolution.height - 1;
  pc_ = 0;
  waitState_ = WaitState::Running;
  inDefineBlock_ = true;

  // Execute *define block until `game` command (which jumps to *start and sets
  // inDefineBlock_ = false) or until *start is reached.
  const std::size_t total = script_.statements().size();
  std::size_t guard = 0;
  while (inDefineBlock_ && pc_ < total && waitState_ == WaitState::Running &&
         guard < total + 100) {
    stepStatement();
    ++guard;
  }

  if (inDefineBlock_) {
    inDefineBlock_ = false;
    jumpToLabel("start");
  }
  return pc_ < total;
}

void PonscripterVm::resetToStart() {
  vars_.resetLocals();
  if (callbacks_) {
    callbacks_->onStopBgm(0);
    callbacks_->onStopWave(-1);
    callbacks_->onPersistGlobals();
  }
  activeBgm_.clear();
  activeBgmLoop_ = false;
  loopingWaves_.clear();
  pendingPretextPc_ = ScriptBuffer::npos;
  textGosubPagePending_ = false;
  callStack_.clear();
  forStack_.clear();
  selectOptions_.clear();
  cselOptions_.clear();
  selectionMode_ = SelectionMode::Jump;
  selectionNumberVariable_ = -1;
  buttons_.clear();
  hoveredButtonId_ = 0;
  btndefSpec_ = TaggedImageSpec{};
  for (auto &s : standing_)
    s = StandingSlot{};
  for (auto &sp : sprites_)
    sp = SpriteSlot{};
  allSpritesHidden_ = false;
  allSprites2Hidden_ = false;
  manualFrameVisible_ = false;
  manualDraftOps_.clear();
  manualDisplayOps_.clear();
  for (auto &b : bars_)
    b = BarGauge{};
  for (auto &p : prnums_)
    p = PrintedNumber{};
  bg_ = TaggedImageSpec{};
  clearCurrentPage(false);
  textWindowVisible_ = false;
  skipMode_ = false;
  autoMode_ = false;
  activeEffect_ = ActiveEffect{};
  quakeAmplitude_ = 0;
  monochromeEnabled_ = false;
  monochromeColor_ = 0xFFFFFFu;
  negativeMode_ = 0;
  waitState_ = WaitState::Running;
  inDefineBlock_ = false;
  jumpToLabel("start");
  // Advance pc_ past the `*start` label statement so currentLabel_ stays
  // "start" without re-executing `*start` on the next step
  if (pc_ < script_.statements().size() && script_.statements()[pc_].isLabel) {
    ++pc_;
  }
  if (callbacks_)
    callbacks_->onSceneDirty();
}

bool PonscripterVm::jumpToLabel(std::string_view label) {
  const std::size_t target = script_.findLabel(label);
  if (target == ScriptBuffer::npos)
    return false;
  pc_ = target;
  currentLabel_ = ScriptBuffer::normalizeLabel(label);
  currentSectionId_ = target;
  seenLabels_.insert(currentLabel_);
  if (waitState_ != WaitState::Halted)
    waitState_ = WaitState::Running;
  return true;
}

const std::string &PonscripterVm::mappedFont(int slot) const {
  if (slot < 0 || slot >= static_cast<int>(fontMap_.size()))
    return emptyFont_;
  return fontMap_[slot];
}

void PonscripterVm::setSkipMode(bool enabled) { skipMode_ = enabled; }

void PonscripterVm::setAutoMode(bool enabled) {
  autoMode_ = enabled;
  autoWaitAccumMs_ = 0.0;
}

void PonscripterVm::enterTextDisplayMode() {
  if (!textWindowVisible_) {
    textWindowVisible_ = true;
    if (callbacks_)
      callbacks_->onSceneDirty();
  }
}

void PonscripterVm::leaveTextDisplayMode(bool forceHide) {
  if (textWindowVisible_ && (forceHide || !windowback_)) {
    textWindowVisible_ = false;
    if (callbacks_)
      callbacks_->onSceneDirty();
  }
}

void PonscripterVm::clearCurrentPage(bool pushBacklog) {
  if (pushBacklog && !pagePlainText_.empty()) {
    backlog_.push_back(pagePlainText_);
    if (backlog_.size() > 200)
      backlog_.erase(backlog_.begin());
  }
  pageSpans_.clear();
  pageLineKeys_.clear();
  pagePlainText_.clear();
  typewriterChars_ = 0;
  totalPageChars_ = 0;
  typewriterAccumMs_ = 0.0;
  currentTextStyle_ = TextStyle{};
}

void PonscripterVm::executeTextLine(std::string_view rawLine,
                                    std::size_t stmtIndex) {
  enterTextDisplayMode();
  kidokuLines_.insert(stmtIndex);

  // Interpolation occurs in the VM, leaving menu and UI text formatting
  // unchanged. Do not interpret substituted values as formatting markup.
  std::string interpolated;
  interpolated.reserve(rawLine.size());
  for (std::size_t i = 0; i < rawLine.size();) {
    if (rawLine[i] == '{' && i + 1 < rawLine.size() &&
        rawLine[i + 1] == '|') {
      interpolated.push_back('{');
      i += 2;
      continue;
    }
    if (rawLine[i] == '{' && i + 2 < rawLine.size() &&
        (rawLine[i + 1] == '$' || rawLine[i + 1] == '%' ||
         rawLine[i + 1] == '?')) {
      const std::size_t end = rawLine.find('}', i + 2);
      if (end != std::string_view::npos) {
        LineLexer expression(rawLine.substr(i + 1, end - i - 1), &vars_);
        std::string replacement;
        if (rawLine[i + 1] == '$')
          replacement = expression.parseStringExpr();
        else
          replacement = std::to_string(expression.parseIntExpr());
        expression.skipWhitespace();
        if (expression.eof()) {
          for (char ch : replacement) {
            if (ch == '~' || ch == '#')
              interpolated.push_back(ch);
            else if (ch == '@' || ch == '\\' || ch == '/' ||
                     ch == '^' || ch == '!' || ch == '_')
              interpolated.push_back('#');
            interpolated.push_back(ch);
          }
          i = end + 1;
          continue;
        }
      }
    }
    interpolated.push_back(rawLine[i++]);
  }
  FormattedTextLine formatted =
      TextFormatter::formatLine(interpolated, currentTextStyle_, true);

  if (!formatted.plainText.empty()) {
    pageLineKeys_.push_back(formatted.plainText);
  }
  for (auto &span : formatted.spans) {
    span.lineKey = formatted.plainText;
    totalPageChars_ += span.text.size();
    pageSpans_.push_back(std::move(span));
  }
  if (!formatted.plainText.empty()) {
    if (!pagePlainText_.empty() && pagePlainText_.back() != '\n')
      pagePlainText_.push_back(' ');
    pagePlainText_ += formatted.plainText;
  }

  // In PONscripter, a bare text line (without '@', '\', or '/') adds an
  // implicit newline at the end of the line unless it ends with '@' or '\'
  // (note: in 00.utf/01.utf, '@' lines are followed by explicit `br` commands).
  if (formatted.terminator == TextTerminator::None) {
    TextSpan brSpan;
    brSpan.isLineBreak = true;
    brSpan.text = "\n";
    brSpan.style = currentTextStyle_;
    pageSpans_.push_back(std::move(brSpan));
    pagePlainText_.push_back('\n');
  }

  if (skipMode_ || window_.textSpeed <= 0) {
    typewriterChars_ = totalPageChars_;
  }

  if (formatted.terminator == TextTerminator::ClickInline) {
    waitState_ = WaitState::WaitClickInline;
    autoWaitAccumMs_ = 0.0;
  } else if (formatted.terminator == TextTerminator::ClickPageClear) {
    waitState_ = WaitState::WaitClickPage;
    autoWaitAccumMs_ = 0.0;
  } else if (formatted.inlineDelayMs > 0) {
    waitState_ = WaitState::WaitTimer;
    waitTargetMs_ = static_cast<double>(formatted.inlineDelayMs);
    waitElapsedMs_ = 0.0;
    waitSkippable_ = formatted.inlineDelaySkippable;
  }

  // textgosub substitutes the click/page wait with a script callback.
  // The callback can supply its own wait and invoke texec before return.
  if (!textGosubLabel_.empty() && script_.hasLabel(textGosubLabel_) &&
      (waitState_ == WaitState::WaitClickInline ||
       waitState_ == WaitState::WaitClickPage)) {
    textGosubPagePending_ = (waitState_ == WaitState::WaitClickPage);
    CallStackEntry frame;
    frame.returnStatementIndex = pc_;
    frame.targetLabel = textGosubLabel_;
    frame.callerLabel = currentLabel_;
    callStack_.push_back(std::move(frame));
    waitState_ = WaitState::Running;
    jumpToLabel(textGosubLabel_);
  }
}

void PonscripterVm::captureCallArguments(LineLexer &lexer,
                                        CallStackEntry &frame,
                                        bool requireLeadingComma) {
  if (requireLeadingComma && !lexer.consumeComma())
    return;
  if (!requireLeadingComma)
    lexer.consumeComma();

  while (true) {
    lexer.skipWhitespace();
    if (lexer.eof())
      break;
    if (lexer.peek() == ',') {
      frame.arguments.emplace_back();
      lexer.consumeComma();
      continue;
    }
    CallArgument arg;
    const char first = lexer.peek();
    arg.isString = first == '$' || first == '"' || first == '^' ||
                   first == '*';
    if (!arg.isString &&
        (std::isalpha(static_cast<unsigned char>(first)) || first == '_')) {
      LineLexer probe = lexer;
      const std::string identifier = probe.parseIdentifier();
      std::string aliasValue;
      arg.isString = vars_.findStrAlias(identifier, aliasValue);
    }
    // Retain the original variable identity separately from its value.
    // getparam i%N / s%N requests the caller's reference id, not its value.
    if (first == '%' || first == '$') {
      LineLexer source = lexer;
      const int id = first == '%' ? source.parseVarRef()
                                  : source.parseStrVarRef();
      source.skipWhitespace();
      if (id >= 0 && (source.eof() || source.peek() == ',')) {
        arg.sourceVariable = id;
        arg.sourceIsString = (first == '$');
      }
    }
    const std::size_t before = lexer.position();
    if (first == '*')
      arg.text = lexer.parseLabelTarget();
    else if (arg.isString)
      arg.text = lexer.parseStringExpr();
    else
      arg.number = lexer.parseIntExpr();
    if (lexer.position() == before)
      break;
    frame.arguments.push_back(std::move(arg));
    if (!lexer.consumeComma())
      break;
  }
}

void PonscripterVm::executeStatement(const StatementSlice &stmt) {
  if (stmt.isLabel) {
    currentLabel_ = stmt.labelName;
    currentSectionId_ = stmt.statementIndex;
    seenLabels_.insert(currentLabel_);
    return;
  }
  if (stmt.isText) {
    if (!pretextGosubLabel_.empty() && script_.hasLabel(pretextGosubLabel_)) {
      if (pendingPretextPc_ != stmt.statementIndex) {
        pendingPretextPc_ = stmt.statementIndex;
        CallStackEntry frame;
        frame.returnStatementIndex = stmt.statementIndex;
        frame.targetLabel = pretextGosubLabel_;
        frame.callerLabel = currentLabel_;
        callStack_.push_back(std::move(frame));
        jumpToLabel(pretextGosubLabel_);
        return;
      }
      pendingPretextPc_ = ScriptBuffer::npos;
    }
    executeTextLine(stmt.text, stmt.statementIndex);
    return;
  }

  LineLexer lexer(stmt.text, &vars_);
  const std::string cmd = lexer.parseIdentifier();
  if (cmd.empty())
    return;

  const auto it = funcLut_.find(cmd);
  if (it != funcLut_.end()) {
    const CommandHandler handler = it->second;
    (this->*handler)(lexer, cmd);
    return;
  }

  // Check user-defined defsub subroutines
  const auto subIt = userDefsubs_.find(cmd);
  if (subIt != userDefsubs_.end()) {
    CallStackEntry frame;
    if (!script_.hasLabel(subIt->second))
      return;
    frame.returnStatementIndex = pc_;
    frame.targetLabel = subIt->second;
    frame.callerLabel = currentLabel_;
    captureCallArguments(lexer, frame, false);
    callStack_.push_back(std::move(frame));
    jumpToLabel(subIt->second);
  }
}

bool PonscripterVm::stepStatement() {
  if (waitState_ != WaitState::Running)
    return false;
  const auto &stmts = script_.statements();
  if (pc_ >= stmts.size()) {
    waitState_ = WaitState::Halted;
    return false;
  }
  const std::size_t currentPc = pc_++;
  executeStatement(stmts[currentPc]);
  return true;
}

void PonscripterVm::tick(double dtMs, std::size_t maxInstructions) {
  if (dtMs < 0.0)
    dtMs = 0.0;

  // 1. Advance multi-cell sprite animations (loopMode 0, 1, 2 only; never 3!)
  for (auto &sp : sprites_) {
    if (!sp.loaded || !sp.visible || sp.spec.numCells <= 1 ||
        sp.spec.loopMode == 3 || sp.spec.durationMs <= 0)
      continue;
    sp.animAccumMs += dtMs;
    while (sp.animAccumMs >= sp.spec.durationMs) {
      sp.animAccumMs -= sp.spec.durationMs;
      if (sp.spec.loopMode == 0) {
        sp.currentCell = (sp.currentCell + 1) % sp.spec.numCells;
      } else if (sp.spec.loopMode == 1) {
        if (sp.currentCell + 1 < sp.spec.numCells)
          ++sp.currentCell;
      } else if (sp.spec.loopMode == 2) {
        sp.currentCell += sp.animDir;
        if (sp.currentCell >= sp.spec.numCells - 1) {
          sp.currentCell = sp.spec.numCells - 1;
          sp.animDir = -1;
        } else if (sp.currentCell <= 0) {
          sp.currentCell = 0;
          sp.animDir = 1;
        }
      }
    }
  }

  // Advance cursor animations
  for (auto &cur : cursors_) {
    if (!cur.configured || cur.spec.numCells <= 1 || cur.spec.loopMode == 3 ||
        cur.spec.durationMs <= 0)
      continue;
    cur.animAccumMs += dtMs;
    while (cur.animAccumMs >= cur.spec.durationMs) {
      cur.animAccumMs -= cur.spec.durationMs;
      cur.currentCell = (cur.currentCell + 1) % cur.spec.numCells;
    }
  }

  // 2. Advance typewriter text reveal if active
  if (typewriterChars_ < totalPageChars_) {
    if (skipMode_ || window_.textSpeed <= 0) {
      typewriterChars_ = totalPageChars_;
    } else {
      // PONscripter textspeed: ms per character (~24 -> ~15ms/char)
      const double msPerChar = std::max(
          2.0,
          static_cast<double>(101 - std::clamp(window_.textSpeed, 1, 100)) *
              0.4);
      typewriterAccumMs_ += dtMs;
      while (typewriterAccumMs_ >= msPerChar &&
             typewriterChars_ < totalPageChars_) {
        typewriterAccumMs_ -= msPerChar;
        ++typewriterChars_;
      }
    }
  }

  // 3. Handle active blocking states
  if (waitState_ == WaitState::WaitEffect) {
    if (skipMode_ || (effectCut_ && activeEffect_.durationMs <= 0)) {
      activeEffect_.elapsedMs = activeEffect_.durationMs;
    } else {
      activeEffect_.elapsedMs += dtMs;
    }
    if (activeEffect_.elapsedMs >= activeEffect_.durationMs) {
      activeEffect_.active = false;
      waitState_ = WaitState::Running;
      if (callbacks_)
        callbacks_->onFinishEffect();
      dtMs = 0.0; // Effect consumed this frame's dtMs
    } else {
      return;
    }
  }

  if (waitState_ == WaitState::WaitTimer) {
    if (skipMode_ && waitSkippable_) {
      waitElapsedMs_ = waitTargetMs_;
    } else {
      waitElapsedMs_ += dtMs;
    }
    if (waitElapsedMs_ >= waitTargetMs_) {
      waitState_ = WaitState::Running;
      dtMs = 0.0;
    } else {
      return;
    }
  }

  if (waitState_ == WaitState::WaitClickInline ||
      waitState_ == WaitState::WaitClickPage) {
    if (skipMode_) {
      advance();
    } else if (autoMode_ && typewriterChars_ >= totalPageChars_) {
      autoWaitAccumMs_ += dtMs;
      if (autoWaitAccumMs_ >= static_cast<double>(autoModeTimeMs_)) {
        autoWaitAccumMs_ = 0.0;
        advance();
      } else {
        return;
      }
    } else {
      return;
    }
  }

  // 4. Execute statements while Running
  std::size_t executed = 0;
  while (waitState_ == WaitState::Running && executed < maxInstructions) {
    if (!stepStatement())
      break;
    ++executed;
    // Fast-forward immediately if WaitEffect/WaitTimer has 0ms duration
    if (waitState_ == WaitState::WaitEffect && activeEffect_.durationMs <= 0) {
      activeEffect_.active = false;
      waitState_ = WaitState::Running;
      if (callbacks_)
        callbacks_->onFinishEffect();
    } else if (waitState_ == WaitState::WaitTimer && waitTargetMs_ <= 0.0) {
      waitState_ = WaitState::Running;
    }
  }
}

void PonscripterVm::updateMousePos(int x, int y) {
  mouseX_ = x;
  mouseY_ = y;
  updateButtonHover();
}

void PonscripterVm::updateButtonHover() {
  hoveredButtonId_ = 0;
  // Reset manual button cell sprites (loopMode == 3) to cell 0 first
  for (const auto &btn : buttons_) {
    if (btn.kind == ButtonDef::Kind::SpriteBtn && btn.spriteNo >= 0 &&
        btn.spriteNo < kMaxExtendedSprites) {
      auto &sp = sprites_[btn.spriteNo];
      if (sp.loaded && sp.spec.loopMode == 3)
        sp.currentCell = 0;
    }
  }

  // Hit-test buttons (last registered or lowest sprite ID wins on overlap)
  for (auto it = buttons_.rbegin(); it != buttons_.rend(); ++it) {
    const auto &btn = *it;
    int bx = btn.x, by = btn.y, bw = btn.w, bh = btn.h;
    if (btn.kind == ButtonDef::Kind::SpriteBtn) {
      if (btn.spriteNo < 0 || btn.spriteNo >= kMaxExtendedSprites)
        continue;
      const auto &sp = sprites_[btn.spriteNo];
      if (!sp.loaded || !sp.visible ||
          areSpritesHidden(sp.secondaryLayer))
        continue;
      bx = sp.x;
      by = sp.y;
      bw = sp.cellWidth > 0 ? sp.cellWidth : 160;
      bh = sp.cellHeight > 0 ? sp.cellHeight : 36;
    }
    if (mouseX_ >= bx && mouseX_ < bx + bw && mouseY_ >= by &&
        mouseY_ < by + bh) {
      hoveredButtonId_ = btn.id;
      if (btn.kind == ButtonDef::Kind::SpriteBtn && btn.spriteNo >= 0 &&
          btn.spriteNo < kMaxExtendedSprites) {
        auto &sp = sprites_[btn.spriteNo];
        if (sp.spec.numCells >= 2 && sp.spec.loopMode == 3)
          sp.currentCell = 1;
      }
      break;
    }
  }
}

bool PonscripterVm::advance() {
  if (waitState_ == WaitState::WaitEffect) {
    if (effectCut_) {
      activeEffect_.elapsedMs = activeEffect_.durationMs;
      activeEffect_.active = false;
      waitState_ = WaitState::Running;
      if (callbacks_)
        callbacks_->onFinishEffect();
      return true;
    }
    return false;
  }

  if (waitState_ == WaitState::WaitTimer) {
    if (waitSkippable_) {
      waitElapsedMs_ = waitTargetMs_;
      waitState_ = WaitState::Running;
      return true;
    }
    return false;
  }

  if (waitState_ == WaitState::WaitVideo) {
    finishVideo();
    return true;
  }

  if (waitState_ == WaitState::WaitClickBare) {
    waitState_ = WaitState::Running;
    return true;
  }

  if (waitState_ == WaitState::WaitClickInline) {
    if (typewriterChars_ < totalPageChars_) {
      typewriterChars_ = totalPageChars_;
      return true;
    }
    waitState_ = WaitState::Running;
    return true;
  }

  if (waitState_ == WaitState::WaitClickPage) {
    if (typewriterChars_ < totalPageChars_) {
      typewriterChars_ = totalPageChars_;
      return true;
    }
    clearCurrentPage(true);
    waitState_ = WaitState::Running;
    return true;
  }

  if (waitState_ == WaitState::WaitButton) {
    return selectButton(hoveredButtonId_);
  }

  return false;
}

bool PonscripterVm::rightClick() {
  if (waitState_ == WaitState::WaitButton) {
    // In PONscripter, right-clicking during `btnwait %var` stores -1 in `%var`
    // and resumes script execution (`00.utf:384,657,796,1062,1478`,
    // `01.utf:4166`).
    return selectButton(-1);
  }
  if (waitState_ == WaitState::WaitSystemCall) {
    finishSystemCall();
    return true;
  }
  if ((waitState_ == WaitState::WaitClickInline ||
       waitState_ == WaitState::WaitClickPage ||
       waitState_ == WaitState::WaitClickBare) &&
      rmode_ && !rmenuEntries_.empty()) {
    enterSystemCall("rmenu");
    return true;
  }
  return false;
}

void PonscripterVm::enterSystemCall(const std::string &kind) {
  if (kind == "end") {
    activeSystemCall_.clear();
    waitState_ = WaitState::Halted;
    if (callbacks_) {
      callbacks_->onStopBgm(0);
      callbacks_->onStopWave(-1);
      callbacks_->onPersistGlobals();
    }
    return;
  }
  if (waitState_ != WaitState::WaitSystemCall) {
    preSystemCallWaitState_ = waitState_;
  }
  activeSystemCall_ = kind;
  waitState_ = WaitState::WaitSystemCall;
  if (callbacks_)
    callbacks_->onSystemCall(kind);
}

bool PonscripterVm::clickAt(int x, int y, bool isRightClick) {
  updateMousePos(x, y);
  lastClickX_ = x;
  lastClickY_ = y;
  if (isRightClick)
    return rightClick();
  return advance();
}

bool PonscripterVm::selectButton(int buttonId) {
  if (waitState_ != WaitState::WaitButton)
    return false;
  if (btnwaitDestVar_ >= 0) {
    const bool globalChanged = vars_.setNum(btnwaitDestVar_, buttonId);
    if (globalChanged && callbacks_)
      callbacks_->onPersistGlobals();
  }
  // Reset hover cell on all manual button sprites
  for (const auto &btn : buttons_) {
    if (btn.kind == ButtonDef::Kind::SpriteBtn && btn.spriteNo >= 0 &&
        btn.spriteNo < kMaxExtendedSprites) {
      auto &sp = sprites_[btn.spriteNo];
      if (sp.loaded && sp.spec.loopMode == 3)
        sp.currentCell = 0;
    }
  }
  // Clear button definitions when a positive button is clicked (preserving
  // buttons on 0 / -1 misclicks so *select_01l and *select_02l work cleanly!)
  if (!btnwaitPreserveButtons_ && buttonId > 0) {
    buttons_.clear();
  }
  hoveredButtonId_ = 0;
  waitState_ = WaitState::Running;
  return true;
}

bool PonscripterVm::selectChoice(int optionIndex) {
  if (waitState_ != WaitState::WaitSelect)
    return false;
  if (optionIndex < 0 || optionIndex >= static_cast<int>(selectOptions_.size()))
    return false;
  const SelectionMode mode = selectionMode_;
  const std::string target = selectOptions_[optionIndex].targetLabel;
  if (mode != SelectionMode::Number && !script_.hasLabel(target))
    return false;
  if (mode == SelectionMode::Subroutine) {
    CallStackEntry frame;
    frame.returnStatementIndex = pc_;
    frame.targetLabel = target;
    frame.callerLabel = currentLabel_;
    callStack_.push_back(std::move(frame));
  } else if (mode == SelectionMode::Number &&
             selectionNumberVariable_ >= 0) {
    const bool changed = vars_.setNum(selectionNumberVariable_, optionIndex);
    if (changed && callbacks_)
      callbacks_->onPersistGlobals();
  }
  selectOptions_.clear();
  selectionMode_ = SelectionMode::Jump;
  selectionNumberVariable_ = -1;
  clearCurrentPage(true);
  waitState_ = WaitState::Running;
  return mode == SelectionMode::Number ? true : jumpToLabel(target);
}

void PonscripterVm::finishSystemCall() {
  if (waitState_ == WaitState::WaitSystemCall) {
    activeSystemCall_.clear();
    waitState_ = preSystemCallWaitState_;
    preSystemCallWaitState_ = WaitState::Running;
  }
}

void PonscripterVm::finishVideo() {
  if (waitState_ == WaitState::WaitVideo) {
    waitState_ = WaitState::Running;
  }
}

void PonscripterVm::populateSpriteDimensions(SpriteSlot &slot) {
  slot.cellWidth = 0;
  slot.cellHeight = 0;
  if (slot.spec.trans == TransMode::String) {
    const int fw =
        slot.spec.fontWidth > 0 ? slot.spec.fontWidth : window_.fontW;
    const int fh =
        slot.spec.fontHeight > 0 ? slot.spec.fontHeight : window_.fontH;
    slot.cellWidth =
        std::max(16, static_cast<int>(slot.spec.text.size()) * (fw / 2 + 2));
    slot.cellHeight = std::max(16, fh + 4);
    return;
  }
  if (slot.spec.trans == TransMode::DirectColor) {
    slot.cellWidth = script_.directives().resolution.width;
    slot.cellHeight = script_.directives().resolution.height;
    return;
  }
  if (callbacks_ && !slot.spec.imagePath.empty()) {
    int rawW = 0, rawH = 0;
    bool hasAlpha = true;
    if (callbacks_->queryImageSize(slot.spec.imagePath, rawW, rawH, hasAlpha)) {
      if (slot.spec.trans == TransMode::Alpha && !hasAlpha) {
        // Non-alpha BMP/JPG with :a splits each cell into left RGB + right mask
        rawW /= 2;
      }
      slot.cellWidth = std::max(1, rawW / std::max(1, slot.spec.numCells));
      slot.cellHeight = std::max(1, rawH);
      return;
    }
  }
  slot.cellWidth = 160;
  slot.cellHeight = 36;
}

void PonscripterVm::populateStandingDimensions(StandingSlot &slot,
                                               int slotIndex) {
  int rawW = 360;
  int rawH = 516;
  bool hasAlpha = true;
  if (callbacks_ && !slot.spec.imagePath.empty()) {
    if (callbacks_->queryImageSize(slot.spec.imagePath, rawW, rawH, hasAlpha)) {
      if (slot.spec.trans == TransMode::Alpha && !hasAlpha)
        rawW /= 2;
    }
  }
  slot.width = std::max(1, rawW / std::max(1, slot.spec.numCells));
  slot.height = std::max(1, rawH);

  // Binary-verified standing coordinates (0x0045e7de):
  // slotIndex: 0='l' (factor=1), 1='c' (factor=2), 2='r' (factor=3)
  const int factor = slotIndex + 1;
  const int screenW = script_.directives().resolution.width;
  slot.x = ((factor * screenW) / 4) - (slot.width / 2);
  slot.y = (underlineValue_ + 1) - slot.height;
}

EffectDef PonscripterVm::resolveEffectSpec(LineLexer &lexer,
                                           bool hasLeadingComma) {
  EffectDef eff;
  eff.id = 1;
  eff.builtinEffect = 1;
  eff.durationMs = 0;

  if (hasLeadingComma) {
    if (!lexer.consumeComma())
      return eff;
  } else {
    lexer.skipWhitespace();
    if (lexer.eof())
      return eff;
  }

  const int firstNum = lexer.parseIntExpr();
  if (lexer.consumeComma()) {
    // 2- or 3-argument inline effect: `<builtin_id>, <duration_ms>[, "mask"]`
    eff.id = firstNum;
    eff.builtinEffect = firstNum;
    eff.durationMs = std::max(0, lexer.parseIntExpr());
    if (lexer.consumeComma()) {
      eff.maskPath = lexer.parseStringExpr();
    }
    return eff;
  }

  // 1-argument effect number N:
  // CRITICAL binary behavior (0x00415e24): if N > 255 (like `print 2000` in
  // `01.utf:4170`), clamp to 0 (instant blit)!
  if (firstNum < 0 || firstNum > 255) {
    eff.id = 0;
    eff.builtinEffect = 0;
    eff.durationMs = 0;
    return eff;
  }
  if (firstNum == 0 || firstNum == 1) {
    eff.id = firstNum;
    eff.builtinEffect = firstNum;
    eff.durationMs = 0;
    return eff;
  }

  const auto it = userEffects_.find(firstNum);
  if (it != userEffects_.end())
    return it->second;

  eff.id = firstNum;
  eff.builtinEffect = 10;
  eff.durationMs = 0;
  return eff;
}

void PonscripterVm::triggerEffect(const EffectDef &eff) {
  if (callbacks_)
    callbacks_->onSceneDirty();

  if (eff.builtinEffect <= 1 || eff.durationMs <= 0 || skipMode_) {
    activeEffect_ = ActiveEffect{};
    return;
  }

  activeEffect_.active = true;
  activeEffect_.builtinEffect = eff.builtinEffect;
  activeEffect_.durationMs = eff.durationMs;
  activeEffect_.elapsedMs = 0.0;
  activeEffect_.maskPath = eff.maskPath;
  waitState_ = WaitState::WaitEffect;
  if (callbacks_)
    callbacks_->onBeginEffect(activeEffect_);
}

GlobalSaveData PonscripterVm::exportGlobalSave() const {
  GlobalSaveData data;
  data.globals = vars_.exportGlobals();
  data.seenFiles = seenFiles_;
  data.seenLabels = seenLabels_;
  data.kidokuLines = kidokuLines_;
  return data;
}

void PonscripterVm::importGlobalSave(const GlobalSaveData &data) {
  vars_.importGlobals(data.globals);
  seenFiles_ = data.seenFiles;
  seenLabels_ = data.seenLabels;
  kidokuLines_ = data.kidokuLines;
}

SlotSaveData PonscripterVm::exportSlotSave() const {
  SlotSaveData data;
  data.statementIndex = pc_;
  data.currentLabel = currentLabel_;
  data.locals = vars_.exportLocals();
  data.callStack = callStack_;
  data.forStack = forStack_;
  data.bg = bg_;
  data.standing = standing_;
  data.allSpritesHidden = allSpritesHidden_;
  data.allSprites2Hidden = allSprites2Hidden_;
  data.activeEffect = activeEffect_;
  data.quakeAmplitude = quakeAmplitude_;
  data.monochromeEnabled = monochromeEnabled_;
  data.monochromeColor = monochromeColor_;
  data.negativeMode = negativeMode_;
  data.manualFrameVisible = manualFrameVisible_;
  data.manualDraftOps = manualDraftOps_;
  data.manualDisplayOps = manualDisplayOps_;
  for (int i = 0; i < kMaxExtendedSprites; ++i) {
    if (sprites_[i].loaded)
      data.sprites[i] = sprites_[i];
  }
  data.window = window_;
  data.textWindowVisible = textWindowVisible_;
  data.pageSpans = pageSpans_;
  data.pagePlainText = pagePlainText_;
  data.activeBgm = activeBgm_;
  data.activeBgmLoop = activeBgmLoop_;
  data.loopingWaves = loopingWaves_;
  data.waitState = (waitState_ == WaitState::WaitSystemCall)
                       ? preSystemCallWaitState_
                       : waitState_;
  data.btnwaitDestVar = btnwaitDestVar_;
  data.btnwaitPreserveButtons = btnwaitPreserveButtons_;
  data.buttons = buttons_;
  data.selectOptions = selectOptions_;
  data.cselOptions = cselOptions_;
  data.pendingPretextStatementIndex = pendingPretextPc_;
  data.textGosubPagePending = textGosubPagePending_;
  data.selectionMode = static_cast<int>(selectionMode_);
  data.selectionNumberVariable = selectionNumberVariable_;
  return data;
}

void PonscripterVm::importSlotSave(const SlotSaveData &data) {
  pc_ = data.statementIndex;
  currentLabel_ = data.currentLabel;
  vars_.importLocals(data.locals);
  callStack_ = data.callStack;
  forStack_ = data.forStack;
  bg_ = data.bg;
  standing_ = data.standing;
  allSpritesHidden_ = data.allSpritesHidden;
  allSprites2Hidden_ = data.allSprites2Hidden;
  activeEffect_ = data.activeEffect;
  quakeAmplitude_ = data.quakeAmplitude;
  monochromeEnabled_ = data.monochromeEnabled;
  monochromeColor_ = data.monochromeColor;
  negativeMode_ = data.negativeMode;
  manualFrameVisible_ = data.manualFrameVisible;
  manualDraftOps_ = data.manualDraftOps;
  manualDisplayOps_ = data.manualDisplayOps;
  for (int i = 0; i < 3; ++i) {
    if (standing_[i].loaded)
      populateStandingDimensions(standing_[i], i);
  }
  for (auto &sp : sprites_)
    sp = SpriteSlot{};
  for (const auto &[id, sp] : data.sprites) {
    if (id >= 0 && id < kMaxExtendedSprites) {
      sprites_[id] = sp;
      populateSpriteDimensions(sprites_[id]);
    }
  }
  window_ = data.window;
  textWindowVisible_ = data.textWindowVisible;
  pageSpans_ = data.pageSpans;
  pageLineKeys_.clear();
  for (const auto &s : pageSpans_) {
    if (!s.lineKey.empty())
      pageLineKeys_.push_back(s.lineKey);
  }
  pagePlainText_ = data.pagePlainText;
  totalPageChars_ = 0;
  for (const auto &s : pageSpans_)
    totalPageChars_ += s.text.size();
  typewriterChars_ = totalPageChars_;

  activeBgm_ = data.activeBgm;
  activeBgmLoop_ = data.activeBgmLoop;
  loopingWaves_ = data.loopingWaves;

  btnwaitDestVar_ = data.btnwaitDestVar;
  btnwaitPreserveButtons_ = data.btnwaitPreserveButtons;
  buttons_ = data.buttons;
  selectOptions_ = data.selectOptions;
  cselOptions_ = data.cselOptions;
  pendingPretextPc_ = data.pendingPretextStatementIndex;
  textGosubPagePending_ = data.textGosubPagePending;
  selectionMode_ = data.selectionMode == 1 ? SelectionMode::Subroutine :
                   data.selectionMode == 2 ? SelectionMode::Number :
                                             SelectionMode::Jump;
  selectionNumberVariable_ = data.selectionNumberVariable;
  hoveredButtonId_ = 0;

  if (callbacks_) {
    if (!activeBgm_.empty())
      callbacks_->onPlayBgm(activeBgm_, activeBgmLoop_, 0);
    else
      callbacks_->onStopBgm(0);
    callbacks_->onStopWave(-1);
    for (const auto &[ch, path] : loopingWaves_) {
      callbacks_->onPlayWave(ch, path, true);
    }
    callbacks_->onSceneDirty();
  }

  WaitState restoredWait = data.waitState;
  if (restoredWait == WaitState::WaitSystemCall ||
      restoredWait == WaitState::Halted) {
    restoredWait =
        !pageSpans_.empty() ? WaitState::WaitClickInline : WaitState::Running;
  }
  activeSystemCall_.clear();
  waitState_ = restoredWait;
  preSystemCallWaitState_ = restoredWait;
}

} // namespace Ponscripter
