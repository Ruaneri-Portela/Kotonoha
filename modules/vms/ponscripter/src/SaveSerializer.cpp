// Copyright 2026 Kotonoha contributors. SPDX-License-Identifier: MIT
#include <Ponscripter/SaveSerializer.hpp>

#include <algorithm>
#include <sstream>
#include <string>

namespace Ponscripter {
namespace {

std::string escapeLine(std::string_view s) {
  std::string out;
  out.reserve(s.size());
  for (char c : s) {
    if (c == '\\')
      out += "\\\\";
    else if (c == '\n')
      out += "\\n";
    else if (c == '\r')
      out += "\\r";
    else if (c == '\t')
      out += "\\t";
    else
      out.push_back(c);
  }
  return out;
}

std::string unescapeLine(std::string_view s) {
  std::string out;
  out.reserve(s.size());
  for (std::size_t i = 0; i < s.size(); ++i) {
    if (s[i] == '\\' && i + 1 < s.size()) {
      const char next = s[++i];
      if (next == 'n')
        out.push_back('\n');
      else if (next == 'r')
        out.push_back('\r');
      else if (next == 't')
        out.push_back('\t');
      else
        out.push_back(next);
    } else {
      out.push_back(s[i]);
    }
  }
  return out;
}

} // namespace

std::string SaveSerializer::serializeGlobals(const GlobalSaveData &data) {
  std::ostringstream out;
  out << "PONSCR_GLOBAL_V1\n";
  for (const auto &[id, val] : data.globals) {
    out << "VAR\t" << id << '\t' << val << '\n';
  }
  for (const auto &file : data.seenFiles) {
    out << "FILE\t" << escapeLine(file) << '\n';
  }
  for (const auto &lbl : data.seenLabels) {
    out << "LABEL\t" << escapeLine(lbl) << '\n';
  }
  for (std::size_t line : data.kidokuLines) {
    out << "KIDOKU\t" << line << '\n';
  }
  return out.str();
}

bool SaveSerializer::deserializeGlobals(std::string_view text,
                                        GlobalSaveData &outData) {
  outData = GlobalSaveData{};
  std::istringstream in{std::string(text)};
  std::string header;
  if (!std::getline(in, header) || header != "PONSCR_GLOBAL_V1")
    return false;

  std::string line;
  while (std::getline(in, line)) {
    if (line.empty())
      continue;
    const std::size_t tab1 = line.find('\t');
    if (tab1 == std::string::npos)
      continue;
    const std::string_view kind(line.data(), tab1);
    const std::string_view rest(line.data() + tab1 + 1,
                                line.size() - (tab1 + 1));
    if (kind == "VAR") {
      const std::size_t tab2 = rest.find('\t');
      if (tab2 != std::string_view::npos) {
        const int id = std::stoi(std::string(rest.substr(0, tab2)));
        const int val = std::stoi(std::string(rest.substr(tab2 + 1)));
        outData.globals[id] = val;
      }
    } else if (kind == "FILE") {
      outData.seenFiles.insert(unescapeLine(rest));
    } else if (kind == "LABEL") {
      outData.seenLabels.insert(unescapeLine(rest));
    } else if (kind == "KIDOKU") {
      outData.kidokuLines.insert(
          static_cast<std::size_t>(std::stoull(std::string(rest))));
    }
  }
  return true;
}

std::string SaveSerializer::serializeSlot(const SlotSaveData &data) {
  std::ostringstream out;
  out << "PONSCR_SLOT_V1\n";
  out << "TIME\t" << data.timestamp.year << '\t' << data.timestamp.month << '\t'
      << data.timestamp.day << '\t' << data.timestamp.hour << '\t'
      << data.timestamp.minute << '\t' << escapeLine(data.timestamp.title)
      << '\n';
  out << "PC\t" << data.statementIndex << '\t' << escapeLine(data.currentLabel)
      << '\n';
  for (const auto &[id, val] : data.locals) {
    out << "LOCAL\t" << id << '\t' << val << '\n';
  }
  for (const auto &frame : data.callStack) {
    out << "CALL\t" << frame.returnStatementIndex << '\t'
        << escapeLine(frame.targetLabel) << '\n';
    if (!frame.callerLabel.empty())
      out << "CALLFROM\t" << escapeLine(frame.callerLabel) << '\n';
    out << "CALLCURSOR\t" << frame.nextArgument << '\n';
    for (const auto &arg : frame.arguments) {
      out << "CALLARG\t" << (arg.isString ? 1 : 0) << '\t'
          << arg.number << '\t' << escapeLine(arg.text) << '\n';
      if (arg.sourceVariable >= 0)
        out << "CALLREF\t" << arg.sourceVariable << '\t'
            << (arg.sourceIsString ? 1 : 0) << '\n';
    }
  }
  for (const auto &f : data.forStack) {
    out << "FOR\t" << f.varId << '\t' << f.endValue << '\t' << f.step << '\t'
        << f.bodyStatementIndex << '\n';
  }
  out << "BG\t" << escapeLine(data.bg.rawTag) << '\n';
  out << "HIDESPR\t" << (data.allSpritesHidden ? 1 : 0) << '\t'
      << (data.allSprites2Hidden ? 1 : 0) << '\n';
  out << "MANUAL\t" << (data.manualFrameVisible ? 1 : 0) << '\n';
  out << "ACTIVEFX\t" << (data.activeEffect.active ? 1 : 0) << '\t'
      << data.activeEffect.builtinEffect << '\t'
      << data.activeEffect.durationMs << '\t'
      << data.activeEffect.elapsedMs << '\t'
      << data.quakeAmplitude << '\t'
      << escapeLine(data.activeEffect.maskPath) << '\n';
  out << "POSTFX\t" << (data.monochromeEnabled ? 1 : 0) << '\t'
      << data.monochromeColor << '\t' << data.negativeMode << '\n';
  auto writeDrawOps = [&](char bucket, const std::vector<ManualDrawOp> &ops) {
    for (const auto &op : ops) {
      out << "MANUALOP\t" << bucket << '\t'
          << static_cast<int>(op.kind) << '\t' << op.colorRgb << '\t'
          << op.x << '\t' << op.y << '\t' << op.width << '\t' << op.height
          << '\t' << op.srcX << '\t' << op.srcY << '\t' << op.srcW
          << '\t' << op.srcH << '\t' << op.cell << '\t' << op.alpha
          << '\t' << op.scaleX << '\t' << op.scaleY << '\t' << op.rotation
          << '\t' << op.transformA << '\t' << op.transformB
          << '\t' << op.transformC << '\t' << op.transformD
          << '\t' << (op.affine ? 1 : 0)
          << '\t' << static_cast<int>(op.spec.trans)
          << '\t' << op.spec.fontWidth << '\t' << op.spec.fontHeight
          << '\t' << op.spec.color << '\t'
          << escapeLine(op.spec.rawTag) << '\t'
          << escapeLine(op.spec.text) << '\t'
          << escapeLine(op.text) << '\n';
    }
  };
  writeDrawOps('D', data.manualDraftOps);
  writeDrawOps('V', data.manualDisplayOps);
  writeDrawOps('B', data.backgroundCopyOps);
  for (int i = 0; i < 3; ++i) {
    if (data.standing[i].loaded) {
      out << "STAND\t" << i << '\t' << data.standing[i].x << '\t'
          << data.standing[i].y << '\t'
          << escapeLine(data.standing[i].spec.rawTag) << '\n';
      out << "STALPHA\t" << i << '\t' << data.standing[i].alpha << '\n';
    }
  }
  for (const auto &[id, sp] : data.sprites) {
    if (sp.loaded) {
      out << "SP\t" << id << '\t' << (sp.visible ? 1 : 0) << '\t' << sp.x
          << '\t' << sp.y << '\t' << sp.alpha << '\t' << sp.currentCell << '\t'
          << escapeLine(sp.spec.rawTag) << '\n';
      if (sp.secondaryLayer || sp.blendMode != 0)
        out << "SPLAYER\t" << id << '\t' << (sp.secondaryLayer ? 1 : 0)
            << '\t' << sp.blendMode << '\n';
      if (sp.spec.trans == TransMode::String) {
        out << "SPTEXT\t" << id << '\t' << sp.spec.fontWidth << '\t'
            << sp.spec.fontHeight << '\t' << sp.spec.fontSpacing << '\t'
            << sp.spec.color << '\t' << sp.spec.hoverColor << '\t'
            << (sp.spec.hasHoverColor ? 1 : 0) << '\t'
            << escapeLine(sp.spec.text) << '\n';
      }
    }
  }
  out << "WIN\t" << (data.textWindowVisible ? 1 : 0) << '\t'
      << data.window.textX << '\t' << data.window.textY << '\t'
      << data.window.cols << '\t' << data.window.rows << '\t'
      << data.window.fontW << '\t' << data.window.fontH << '\t'
      << data.window.pitchX << '\t' << data.window.pitchY << '\t'
      << data.window.textSpeed << '\t' << (data.window.bold ? 1 : 0) << '\t'
      << (data.window.shadow ? 1 : 0) << '\t' << data.window.winX << '\t'
      << data.window.winY << '\t' << escapeLine(data.window.backdrop.rawTag)
      << '\n';
  out << "PAGETEXT\t" << escapeLine(data.pagePlainText) << '\n';
  for (const auto &span : data.pageSpans) {
    out << "SPAN\t" << (span.isLineBreak ? 1 : 0) << '\t' << span.style.fontSlot
        << '\t' << (span.style.italic ? 1 : 0) << '\t'
        << (span.style.bold ? 1 : 0) << '\t' << span.style.colorRgb << '\t'
        << escapeLine(span.text) << '\n';
    if (!span.lineKey.empty()) {
      out << "SPANKEY\t" << escapeLine(span.lineKey) << '\n';
    }
  }
  out << "WAIT\t" << static_cast<int>(data.waitState) << '\t'
      << data.btnwaitDestVar << '\t' << (data.btnwaitPreserveButtons ? 1 : 0)
      << '\n';
  out << "SELMODE\t" << data.selectionMode << '\t'
      << data.selectionNumberVariable << '\n';
  if (data.pendingPretextStatementIndex != static_cast<std::size_t>(-1))
    out << "PRETEXTPC\t" << data.pendingPretextStatementIndex << '\n';
  out << "TEXTPAGE\t" << (data.textGosubPagePending ? 1 : 0) << '\n';
  for (const auto &btn : data.buttons) {
    out << "BTN\t" << static_cast<int>(btn.kind) << '\t' << btn.id << '\t'
        << btn.spriteNo << '\t' << btn.x << '\t' << btn.y << '\t' << btn.w
        << '\t' << btn.h << '\t' << btn.srcX << '\t' << btn.srcY
        << '\t' << escapeLine(btn.text) << '\n';
  }
  for (const auto &opt : data.selectOptions) {
    out << "SEL\t" << escapeLine(opt.text) << '\t'
        << escapeLine(opt.targetLabel) << '\n';
  }
  for (const auto &opt : data.cselOptions) {
    out << "CSEL\t" << escapeLine(opt.text) << '\t'
        << escapeLine(opt.targetLabel) << '\n';
  }
  out << "BGM\t" << (data.activeBgmLoop ? 1 : 0) << '\t'
      << escapeLine(data.activeBgm) << '\n';
  for (const auto &[ch, path] : data.loopingWaves) {
    out << "DWAVELOOP\t" << ch << '\t' << escapeLine(path) << '\n';
  }
  return out.str();
}

SaveTimestamp SaveSerializer::peekSlotTimestamp(std::string_view text) {
  SaveTimestamp ts;
  std::istringstream in{std::string(text)};
  std::string header;
  if (!std::getline(in, header) || header != "PONSCR_SLOT_V1")
    return ts;
  std::string line;
  while (std::getline(in, line)) {
    if (line.rfind("TIME\t", 0) == 0) {
      std::istringstream ls(line.substr(5));
      std::string titleRest;
      ls >> ts.year >> ts.month >> ts.day >> ts.hour >> ts.minute;
      if (ls.peek() == '\t')
        ls.get();
      std::getline(ls, titleRest);
      ts.title = unescapeLine(titleRest);
      ts.exists = true;
      return ts;
    }
  }
  return ts;
}

bool SaveSerializer::deserializeSlot(std::string_view text,
                                     SlotSaveData &outData) {
  outData = SlotSaveData{};
  std::istringstream in{std::string(text)};
  std::string header;
  if (!std::getline(in, header) || header != "PONSCR_SLOT_V1")
    return false;

  std::string line;
  while (std::getline(in, line)) {
    if (line.empty())
      continue;
    std::istringstream ls(line);
    std::string kind;
    if (!std::getline(ls, kind, '\t'))
      continue;
    if (kind == "TIME") {
      ls >> outData.timestamp.year >> outData.timestamp.month >>
          outData.timestamp.day >> outData.timestamp.hour >>
          outData.timestamp.minute;
      if (ls.peek() == '\t')
        ls.get();
      std::string t;
      std::getline(ls, t);
      outData.timestamp.title = unescapeLine(t);
      outData.timestamp.exists = true;
    } else if (kind == "PC") {
      ls >> outData.statementIndex;
      if (ls.peek() == '\t')
        ls.get();
      std::string lbl;
      std::getline(ls, lbl);
      outData.currentLabel = unescapeLine(lbl);
    } else if (kind == "LOCAL") {
      int id = 0, val = 0;
      ls >> id >> val;
      outData.locals[id] = val;
    } else if (kind == "CALL") {
      CallStackEntry frame;
      ls >> frame.returnStatementIndex;
      if (ls.peek() == '\t')
        ls.get();
      std::string lbl;
      std::getline(ls, lbl);
      frame.targetLabel = unescapeLine(lbl);
      outData.callStack.push_back(std::move(frame));
    } else if (kind == "CALLFROM") {
      if (!outData.callStack.empty()) {
        std::string source;
        std::getline(ls, source);
        outData.callStack.back().callerLabel = unescapeLine(source);
      }
    } else if (kind == "CALLCURSOR") {
      if (!outData.callStack.empty())
        ls >> outData.callStack.back().nextArgument;
    } else if (kind == "CALLARG") {
      if (!outData.callStack.empty()) {
        CallArgument arg;
        int flag = 0;
        ls >> flag >> arg.number;
        arg.isString = flag != 0;
        if (ls.peek() == '\t')
          ls.get();
        std::string value;
        std::getline(ls, value);
        arg.text = unescapeLine(value);
        outData.callStack.back().arguments.push_back(std::move(arg));
      }
    } else if (kind == "CALLREF") {
      if (!outData.callStack.empty() &&
          !outData.callStack.back().arguments.empty()) {
        auto &arg = outData.callStack.back().arguments.back();
        int isString = 0;
        ls >> arg.sourceVariable >> isString;
        arg.sourceIsString = (isString != 0);
      }
    } else if (kind == "FOR") {
      ForLoopFrame f;
      ls >> f.varId >> f.endValue >> f.step >> f.bodyStatementIndex;
      outData.forStack.push_back(f);
    } else if (kind == "BG") {
      std::string tag;
      std::getline(ls, tag);
      outData.bg = parseTaggedString(unescapeLine(tag), TransMode::Copy);
    } else if (kind == "HIDESPR") {
      int first = 0, second = 0;
      ls >> first >> second;
      outData.allSpritesHidden = first != 0;
      outData.allSprites2Hidden = second != 0;
    } else if (kind == "MANUAL") {
      int shown = 0;
      ls >> shown;
      outData.manualFrameVisible = shown != 0;
    } else if (kind == "ACTIVEFX") {
      int active = 0;
      ls >> active >> outData.activeEffect.builtinEffect
         >> outData.activeEffect.durationMs
         >> outData.activeEffect.elapsedMs >> outData.quakeAmplitude;
      if (ls.peek() == '\t')
        ls.get();
      std::string mask;
      std::getline(ls, mask);
      outData.activeEffect.active = active != 0;
      outData.activeEffect.maskPath = unescapeLine(mask);
    } else if (kind == "POSTFX") {
      int enabled = 0;
      ls >> enabled >> outData.monochromeColor >> outData.negativeMode;
      outData.monochromeEnabled = enabled != 0;
      outData.negativeMode = std::clamp(outData.negativeMode, 0, 2);
    } else if (kind == "MANUALOP") {
      char bucket = '\0';
      int kindValue = -1, affine = 0, trans = 0;
      ManualDrawOp op;
      ls >> bucket >> kindValue >> op.colorRgb >> op.x >> op.y
         >> op.width >> op.height >> op.srcX >> op.srcY >> op.srcW
         >> op.srcH >> op.cell >> op.alpha >> op.scaleX >> op.scaleY
         >> op.rotation >> op.transformA >> op.transformB
         >> op.transformC >> op.transformD >> affine >> trans
         >> op.spec.fontWidth >> op.spec.fontHeight >> op.spec.color;
      if (!ls || kindValue < 0 ||
          kindValue > static_cast<int>(ManualDrawOp::Kind::Window))
        continue;
      op.kind = static_cast<ManualDrawOp::Kind>(kindValue);
      op.affine = affine != 0;
      if (ls.peek() == '\t')
        ls.get();
      std::string raw, spriteText, text;
      std::getline(ls, raw, '\t');
      std::getline(ls, spriteText, '\t');
      std::getline(ls, text);
      const int fontWidth = op.spec.fontWidth;
      const int fontHeight = op.spec.fontHeight;
      const std::uint32_t color = op.spec.color;
      op.spec = parseTaggedString(unescapeLine(raw), TransMode::Copy);
      if (op.spec.rawTag.empty())
        op.spec.trans = static_cast<TransMode>(trans);
      op.spec.fontWidth = fontWidth;
      op.spec.fontHeight = fontHeight;
      op.spec.color = color;
      op.spec.text = unescapeLine(spriteText);
      op.text = unescapeLine(text);
      auto *dest = bucket == 'D' ? &outData.manualDraftOps :
                   bucket == 'V' ? &outData.manualDisplayOps :
                   bucket == 'B' ? &outData.backgroundCopyOps : nullptr;
      if (dest && dest->size() < 4096)
        dest->push_back(std::move(op));
    } else if (kind == "STALPHA") {
      int index = 0, alpha = 255;
      ls >> index >> alpha;
      if (index >= 0 && index < 3)
        outData.standing[index].alpha = std::clamp(alpha, 0, 255);
    } else if (kind == "STAND") {
      int idx = 0, x = 0, y = 0;
      ls >> idx >> x >> y;
      if (ls.peek() == '\t')
        ls.get();
      std::string tag;
      std::getline(ls, tag);
      if (idx >= 0 && idx < 3) {
        outData.standing[idx].loaded = true;
        outData.standing[idx].x = x;
        outData.standing[idx].y = y;
        outData.standing[idx].spec =
            parseTaggedString(unescapeLine(tag), TransMode::Alpha);
      }
    } else if (kind == "SP") {
      int id = 0, vis = 0, x = 0, y = 0, alpha = 255, cell = 0;
      ls >> id >> vis >> x >> y >> alpha >> cell;
      if (ls.peek() == '\t')
        ls.get();
      std::string tag;
      std::getline(ls, tag);
      SpriteSlot sp;
      sp.loaded = true;
      sp.visible = (vis != 0);
      sp.x = x;
      sp.y = y;
      sp.alpha = alpha;
      sp.currentCell = cell;
      sp.spec = parseTaggedString(unescapeLine(tag), TransMode::Alpha);
      outData.sprites[id] = std::move(sp);
    } else if (kind == "SPLAYER") {
      int id = -1, secondary = 0, blend = 0;
      ls >> id >> secondary >> blend;
      const auto it = outData.sprites.find(id);
      if (it != outData.sprites.end()) {
        it->second.secondaryLayer = secondary != 0;
        it->second.blendMode = std::clamp(blend, 0, 2);
      }
    } else if (kind == "SPTEXT") {
      int id = -1, width = 0, height = 0, spacing = 0, hoverFlag = 0;
      std::uint32_t color = 0xFFFFFFu, hover = 0xFFFFFFu;
      ls >> id >> width >> height >> spacing >> color >> hover >> hoverFlag;
      if (ls.peek() == '\t')
        ls.get();
      std::string caption;
      std::getline(ls, caption);
      const auto it = outData.sprites.find(id);
      if (it != outData.sprites.end()) {
        auto &spec = it->second.spec;
        spec.trans = TransMode::String;
        spec.fontWidth = width;
        spec.fontHeight = height;
        spec.fontSpacing = spacing;
        spec.color = color;
        spec.hoverColor = hover;
        spec.hasHoverColor = hoverFlag != 0;
        spec.text = unescapeLine(caption);
      }
    } else if (kind == "WIN") {
      int vis = 0, bold = 0, shadow = 1;
      ls >> vis >> outData.window.textX >> outData.window.textY >>
          outData.window.cols >> outData.window.rows >> outData.window.fontW >>
          outData.window.fontH >> outData.window.pitchX >>
          outData.window.pitchY >> outData.window.textSpeed >> bold >> shadow >>
          outData.window.winX >> outData.window.winY;
      outData.textWindowVisible = (vis != 0);
      outData.window.bold = (bold != 0);
      outData.window.shadow = (shadow != 0);
      if (ls.peek() == '\t')
        ls.get();
      std::string tag;
      std::getline(ls, tag);
      outData.window.backdrop =
          parseTaggedString(unescapeLine(tag), TransMode::Alpha);
    } else if (kind == "PAGETEXT") {
      std::string t;
      std::getline(ls, t);
      outData.pagePlainText = unescapeLine(t);
    } else if (kind == "SPAN") {
      int br = 0, slot = 0, it = 0, bd = 0;
      std::uint32_t col = 0xFFFFFFu;
      ls >> br >> slot >> it >> bd >> col;
      if (ls.peek() == '\t')
        ls.get();
      std::string t;
      std::getline(ls, t);
      TextSpan span;
      span.isLineBreak = (br != 0);
      span.style.fontSlot = slot;
      span.style.italic = (it != 0);
      span.style.bold = (bd != 0);
      span.style.colorRgb = col;
      span.text = unescapeLine(t);
      outData.pageSpans.push_back(std::move(span));
    } else if (kind == "SPANKEY") {
      std::string k;
      std::getline(ls, k);
      if (!outData.pageSpans.empty()) {
        outData.pageSpans.back().lineKey = unescapeLine(k);
      }
    } else if (kind == "WAIT") {
      int ws = static_cast<int>(WaitState::WaitClickInline);
      int preserve = 0;
      ls >> ws >> outData.btnwaitDestVar >> preserve;
      outData.waitState = static_cast<WaitState>(ws);
      outData.btnwaitPreserveButtons = (preserve != 0);
    } else if (kind == "SELMODE") {
      ls >> outData.selectionMode >> outData.selectionNumberVariable;
      if (outData.selectionMode < 0 || outData.selectionMode > 2) {
        outData.selectionMode = 0;
        outData.selectionNumberVariable = -1;
      }
    } else if (kind == "PRETEXTPC") {
      ls >> outData.pendingPretextStatementIndex;
    } else if (kind == "TEXTPAGE") {
      int pending = 0;
      ls >> pending;
      outData.textGosubPagePending = (pending != 0);
    } else if (kind == "BTN") {
      int k = 0;
      ButtonDef btn;
      ls >> k >> btn.id >> btn.spriteNo >> btn.x >> btn.y >> btn.w >> btn.h >>
          btn.srcX >> btn.srcY;
      btn.kind = static_cast<ButtonDef::Kind>(k);
      if (ls.peek() == '\t')
        ls.get();
      std::string label;
      std::getline(ls, label);
      btn.text = unescapeLine(label);
      outData.buttons.push_back(std::move(btn));
    } else if (kind == "SEL") {
      std::string t, lbl;
      std::getline(ls, t, '\t');
      std::getline(ls, lbl);
      outData.selectOptions.push_back({unescapeLine(t), unescapeLine(lbl)});
    } else if (kind == "CSEL") {
      std::string t, lbl;
      std::getline(ls, t, '\t');
      std::getline(ls, lbl);
      outData.cselOptions.push_back({unescapeLine(t), unescapeLine(lbl)});
    } else if (kind == "BGM") {
      int loop = 0;
      ls >> loop;
      if (ls.peek() == '\t')
        ls.get();
      std::string p;
      std::getline(ls, p);
      outData.activeBgmLoop = (loop != 0);
      outData.activeBgm = unescapeLine(p);
    } else if (kind == "DWAVELOOP") {
      int ch = 0;
      ls >> ch;
      if (ls.peek() == '\t')
        ls.get();
      std::string p;
      std::getline(ls, p);
      outData.loopingWaves[ch] = unescapeLine(p);
    }
  }
  return true;
}

} // namespace Ponscripter
