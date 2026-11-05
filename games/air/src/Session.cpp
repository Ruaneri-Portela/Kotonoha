// Copyright 2026 Kotonoha contributors. SPDX-License-Identifier: MIT
#include "Air/Session.hpp"
#include <algorithm>
#include <cctype>
#include <cstring>
#include <iomanip>
#include <sstream>
#include <stdexcept>
namespace Air {
using libreallive::CommandElement;
Session::Session(ScriptArchive &a, int start) : archive_(a) {
  machine_.onAdvance = [this] { ++pc_; };
  machine_.onText = [this](std::string s) {
    if (text)
      text(std::move(s));
  };
  machine_.onCommand = [this](const CommandElement &c) { execute(c); };
  jump(start);
}
const std::vector<const libreallive::BytecodeElement *> &
Session::script(int n) {
  auto it = scripts_.find(n);
  if (it != scripts_.end())
    return it->second;
  std::vector<const libreallive::BytecodeElement *> elements;
  for (const auto &e : archive_.Scenario(n))
    elements.push_back(e.get());
  return scripts_.emplace(n, std::move(elements)).first->second;
}
void Session::seek(int n, std::size_t index) {
  if (index > script(n).size())
    throw std::out_of_range("AIR instruction index");
  scene_ = n;
  pc_ = index;
  blocked_ = finished_ = false;
}
void Session::jump(int n, int entry) {
  auto &s = archive_.Scenario(n);
  auto it = s.FindEntrypoint(entry);
  seek(n, static_cast<std::size_t>(std::distance(s.begin(), it)));
}
bool Session::step() {
  if (blocked_ || finished_)
    return false;
  const auto &s = script(scene_);
  if (pc_ >= s.size()) {
    finished_ = true;
    return false;
  }
  const int scene = scene_;
  const auto index = pc_;
  try {
    s[pc_]->RunOnMachine(machine_);
  } catch (const std::exception &e) {
    throw std::runtime_error("AIR scenario " + std::to_string(scene) + ":" +
                             std::to_string(index) + ": " + e.what());
  }
  return !blocked_;
}
std::size_t Session::run(std::size_t budget) {
  std::size_t n = 0;
  while (n < budget && !blocked_ && !finished_) {
    if (!step())
      break;
    ++n;
  }
  return n;
}
void Session::target(const CommandElement &c, int which, bool call) {
  if (which < 0 || static_cast<std::size_t>(which) >= c.GetPointersCount()) {
    ++pc_;
    return;
  }
  if (call)
    stack_.push_back({scene_, pc_ + 1});
  const auto *ptr = c.GetPointer(which)->get();
  const auto &s = script(scene_);
  auto it = std::find(s.begin(), s.end(), ptr);
  if (it == s.end())
    throw std::runtime_error("AIR jump target outside scenario");
  pc_ = static_cast<std::size_t>(it - s.begin());
}
bool Session::flow(const CommandElement &c) {
  if (c.modtype() != 0 || (c.module() != 1 && c.module() != 6))
    return false;
  int op = c.opcode();
  if (c.module() == 6 && op < 2)
    op = 1 - op;
  auto value = [&](int i) {
    auto raw = c.GetParam(i);
    const char *p = raw.c_str();
    return libreallive::GetExpression(p).GetIntegerValue(machine_);
  };
  if (op == 0 || op == 5) {
    target(c, 0, op == 5);
    return true;
  }
  if (op == 1 || op == 2 || op == 6 || op == 7) {
    bool take = value(0) != 0;
    if (op == 2 || op == 7)
      take = !take;
    if (take)
      target(c, 0, op >= 6);
    else
      ++pc_;
    return true;
  }
  if (op == 3 || op == 8) {
    target(c, value(0), op == 8);
    return true;
  }
  if (op == 4 || op == 9) {
    int v = value(0);
    for (std::size_t i = 0; i < c.GetCaseCount(); ++i) {
      auto raw = c.GetCase(static_cast<int>(i));
      if (raw == "()") {
        target(c, static_cast<int>(i), op == 9);
        return true;
      }
      if (raw.size() < 2)
        throw std::runtime_error("AIR malformed case");
      const char *p = raw.c_str() + 1;
      if (libreallive::GetExpression(p).GetIntegerValue(machine_) == v) {
        target(c, static_cast<int>(i), op == 9);
        return true;
      }
    }
    throw std::runtime_error("AIR case has no match");
  }
  if (op == 10 || op == 13 || op == 17 || op == 19) {
    if ((op == 17 || op == 19) && c.GetParamCount())
      machine_.set_store_register(value(0));
    if (stack_.empty()) {
      finished_ = true;
      return true;
    }
    auto f = stack_.back();
    stack_.pop_back();
    seek(f.scene, f.pc);
    return true;
  }
  if (op == 11 || op == 12 || op == 16 || op == 18) {
    int n = value(0), entry = c.GetParamCount() > 1 ? value(1) : 0;
    if (op == 12 || op == 18)
      stack_.push_back({scene_, pc_ + 1});
    if (op == 16)
      target(c, 0, false);
    else
      jump(n, entry);
    return true;
  }
  return false;
}
namespace {
std::size_t CharOffset(const std::string &s, int n) {
  std::size_t p = 0;
  for (int i = 0; i < n && p < s.size(); ++i) {
    unsigned char c = s[p++];
    if (((c >= 0x81 && c <= 0x9f) || (c >= 0xe0 && c <= 0xfc)) && p < s.size())
      ++p;
  }
  return p;
}
} // namespace
bool Session::strings(const CommandElement &c) {
  if (c.modtype() != 1 || c.module() != 10)
    return false;
  auto p = [&](int i) { return Parameter(c, i); };
  auto num = [&](int i) { return IntegerParameter(machine_, c, i); };
  auto str = [&](int i) { return StringParameter(machine_, c, i); };
  auto write = [&](int i, const std::string &v) {
    p(i).SetStringValue(machine_, v);
  };
  const int op = c.opcode();
  switch (op) {
  case 0: {
    auto s = str(1);
    if (c.overload())
      s.resize(
          std::min(s.size(), static_cast<std::size_t>(std::max(0, num(2)))));
    write(0, s);
    break;
  }
  case 1: {
    if (!c.overload())
      write(0, "");
    else {
      auto a = p(0).GetStringReferenceIterator(machine_),
           b = p(1).GetStringReferenceIterator(machine_);
      for (std::size_t n = 0; a != b && n < 100000; ++n, ++a)
        *a = "";
      *b = "";
    }
    break;
  }
  case 2:
    write(0, str(0) + str(1));
    break;
  case 3:
    machine_.set_store_register(static_cast<int>(str(0).size()));
    break;
  case 4:
    machine_.set_store_register(std::strcmp(str(0).c_str(), str(1).c_str()));
    break;
  case 15:
    write(c.overload() ? 1 : 0, str(0));
    break;
  case 5:
  case 6: {
    auto s = str(1);
    int off = num(2);
    if (off < 0)
      throw std::out_of_range("AIR negative substring offset");
    if (op == 6) {
      int length = 0;
      for (std::size_t k = 0; k < s.size(); k = CharOffset(s, ++length)) {
      }
      off = std::max(0, length - off);
    }
    auto begin = CharOffset(s, off);
    auto end =
        c.overload() ? CharOffset(s, off + std::max(0, num(3))) : s.size();
    write(0, s.substr(begin, end - begin));
    break;
  }
  case 7: {
    auto s = str(0);
    int len = 0;
    for (std::size_t k = 0; k < s.size(); k = CharOffset(s, ++len)) {
    }
    machine_.set_store_register(len);
    break;
  }
  case 8: {
    auto s = str(0);
    s.resize(CharOffset(s, std::max(0, num(1))));
    write(0, s);
    break;
  }
  case 10:
  case 11: {
    if (archive_.Scenario(scene_).encoding() == 0)
      return false;
    write(c.overload() ? 1 : 0, str(0));
    break;
  }
  case 12:
  case 13: {
    auto s = str(0);
    for (char &ch : s)
      if (static_cast<unsigned char>(ch) < 128)
        ch = op == 12 ? std::toupper(static_cast<unsigned char>(ch))
                      : std::tolower(static_cast<unsigned char>(ch));
    write(c.overload() ? 1 : 0, s);
    break;
  }
  case 17: {
    int n = num(0), width = c.overload() ? num(2) : 0;
    std::ostringstream out;
    if (n < 0)
      out << '-';
    out << std::setw(std::clamp(width, 0, 10000)) << std::setfill('0')
        << (n < 0 ? -static_cast<long long>(n) : n);
    write(1, out.str());
    break;
  }
  case 18: {
    try {
      machine_.set_store_register(std::stoi(str(0)));
    } catch (const std::exception &) {
      machine_.set_store_register(0);
    }
    break;
  }
  case 30:
  case 31: {
    auto s = str(0), find = str(1);
    auto n = op == 30 ? s.find(find) : s.rfind(find);
    machine_.set_store_register(n == std::string::npos ? -1
                                                       : static_cast<int>(n));
    break;
  }
  case 100:
    if (text)
      text(c.overload() ? std::to_string(num(0))
                        : libreallive::EvaluatePRINT(machine_, str(0)));
    break;
  case 200:
    machine_.set_store_register(str(0).empty() ? 0 : 1);
    break;
  default:
    return false;
  }
  ++pc_;
  return true;
}
bool Session::memory(const CommandElement &c) {
  if (c.modtype() != 1 || c.module() != 11)
    return false;
  auto p = [&](int i) { return Parameter(c, i); };
  auto num = [&](int i) { return IntegerParameter(machine_, c, i); };
  auto ref = [&](int i) { return p(i).GetIntegerReferenceIterator(machine_); };
  switch (c.opcode()) {
  case 0:
  case 3: {
    auto dest = ref(0);
    int step = c.opcode() == 3 ? num(1) : 1;
    std::size_t start = c.opcode() == 3 ? 2 : 1;
    for (std::size_t i = start; i < c.GetParamCount(); ++i) {
      *dest = num(static_cast<int>(i));
      dest = dest + step;
    }
    break;
  }
  case 1: {
    auto a = ref(0), b = ref(1);
    int v = c.overload() ? num(2) : 0;
    std::size_t n = 0;
    while (a != b) {
      if (++n > 1000000)
        throw std::runtime_error("AIR invalid memory range");
      *a = v;
      ++a;
    }
    *b = v;
    break;
  }
  case 2: {
    auto a = ref(0), b = ref(1);
    int count = num(2);
    if (count < 0 || count > 1000000)
      throw std::out_of_range("AIR copy count");
    std::vector<int> values;
    for (int i = 0; i < count; ++i, ++a)
      values.push_back(*a);
    for (int v : values) {
      *b = v;
      ++b;
    }
    break;
  }
  case 4: {
    auto dest = ref(0);
    int step = num(1), count = num(2), v = c.overload() ? num(3) : 0;
    if (count < 0 || count > 1000000)
      throw std::out_of_range("AIR set count");
    for (int i = 0; i < count; ++i) {
      *dest = v;
      dest = dest + step;
    }
    break;
  }
  case 6: {
    auto dest = ref(0);
    int off = num(1);
    for (std::size_t i = 2; i < c.GetParamCount(); ++i) {
      *dest = *(ref(static_cast<int>(i)) + off);
      ++dest;
    }
    break;
  }
  case 100: {
    auto a = ref(0), b = ref(1);
    std::uint32_t total = 0;
    std::size_t n = 0;
    while (a != b) {
      if (++n > 1000000)
        throw std::runtime_error("AIR invalid sum range");
      total += int(*a);
      ++a;
    }
    total += int(*b);
    machine_.set_store_register(static_cast<std::int32_t>(total));
    break;
  }
  case 101: {
    auto a = ref(0), b = ref(1);
    int count = 0;
    std::size_t n = 0;
    while (a != b) {
      if (++n > 1000000)
        throw std::runtime_error("AIR invalid memory range");
      ++count;
      ++a;
    }
    ++count;
    machine_.set_store_register(count);
    break;
  }
  default:
    return false;
  }
  ++pc_;
  return true;
}
std::string &Session::babelString(int address) {
  int bank = (static_cast<unsigned>(address) >> 16), index = address & 0xffff;
  if (bank != 12 && bank != 18) {
    bank = 18;
    index = 0;
  }
  return machine_.memory().strings[{bank, index}];
}
int Session::babelAppend(int address) {
  auto &source = babelString(address);
  // The shipped DLL mutates the source on the first textStart (native evidence
  // FUN_1000fa80); this quirk affects the script-visible string too.
  if (babelStarts_ == 1)
    source.insert(source.begin(), ' ');
  for (std::size_t i = 0; i < source.size();) {
    unsigned ch = static_cast<unsigned char>(source[i++]);
    if (ch == 8) {
      babelBuffer_ += '"';
      continue;
    }
    if (ch == 9) {
      babelItalic_ = true;
      continue;
    }
    if (ch == 10) {
      babelItalic_ = false;
      continue;
    }
    if (ch == 0x81 && i + 2 < source.size() &&
        (static_cast<unsigned char>(source[i]) == 0x93 ||
         static_cast<unsigned char>(source[i]) == 0x96) &&
        static_cast<unsigned char>(source[i + 1]) == 0x82 &&
        static_cast<unsigned char>(source[i + 2]) >= 0x60 &&
        static_cast<unsigned char>(source[i + 2]) <= 0x79) {
      bool global = static_cast<unsigned char>(source[i]) == 0x96;
      int index = static_cast<unsigned char>(source[i + 2]) - 0x60;
      i += 3;
      if (i + 1 < source.size() &&
          static_cast<unsigned char>(source[i]) == 0x82 &&
          static_cast<unsigned char>(source[i + 1]) >= 0x60 &&
          static_cast<unsigned char>(source[i + 1]) <= 0x79) {
        index =
            (index + 1) * 26 + static_cast<unsigned char>(source[i + 1]) - 0x60;
        i += 2;
      }
      auto name = resolveName ? resolveName(global, index) : std::string();
      if (i + 1 < source.size() &&
          static_cast<unsigned char>(source[i]) == 0x82 &&
          static_cast<unsigned char>(source[i + 1]) >= 0x4f &&
          static_cast<unsigned char>(source[i + 1]) <= 0x58) {
        int offset = static_cast<unsigned char>(source[i + 1]) - 0x4f;
        i += 2;
        auto pos = CharOffset(name, offset), end = CharOffset(name, offset + 1);
        name = name.substr(pos, end - pos);
      }
      babelBuffer_ += name;
      continue;
    }
    if (((ch >= 0x81 && ch <= 0x9f) || (ch >= 0xe0 && ch <= 0xfc)) &&
        i < source.size())
      ch = (ch << 8) | static_cast<unsigned char>(source[i++]);
    if (babelItalic_ && ch != 0x20 && ch <= 0xffff) {
      if (ch > 0x8900)
        ch -= 0x200;
      else if (ch <= 0xff)
        ch += ch >= 0x60 ? 0x8320 : 0x831f;
    }
    if (ch > 255)
      babelBuffer_ += static_cast<char>(ch >> 8);
    babelBuffer_ += static_cast<char>(ch);
  }
  return 1;
}
int Session::babelGetChar(int output, int xmod) {
  auto &dest = babelString(output);
  dest.clear();
  auto result = [&](int code) {
    if (babelControl && code != 2)
      babelControl(code);
    return code;
  };
  while (babelIndex_ < babelBuffer_.size()) {
    unsigned ch = static_cast<unsigned char>(babelBuffer_[babelIndex_++]);
    if (ch == 0)
      return result(1);
    if (ch == 1) {
      auto end = babelBuffer_.find(char(2), babelIndex_);
      if (end == std::string::npos)
        return result(1);
      auto name = babelBuffer_.substr(babelIndex_, end - babelIndex_);
      if (speaker)
        speaker(name);
      if (babelNameModes_[currentWindow] >= 1)
        babelIndex_ = end + 1;
      continue;
    }
    if (ch == 2) {
      if (babelIndex_ >= babelBuffer_.size())
        return result(1);
      continue;
    }
    if (ch == 3)
      return result(lineBreak ? lineBreak() : 3);
    if (ch == 4)
      return result(5);
    if (ch == 5)
      return result(6);
    if (ch == 8 || ch == 9 || ch == 10)
      continue;
    int code = 2;
    if (ch == 31) {
      code = 7;
      if (babelIndex_ >= babelBuffer_.size())
        return result(1);
      ch = static_cast<unsigned char>(babelBuffer_[babelIndex_++]);
    }
    if (ch == 6 || ch == 7)
      throw std::runtime_error(
          "AIR rlBabel extended-font glyph requires native atlas support");
    dest += static_cast<char>(ch);
    if (((ch >= 0x81 && ch <= 0x9f) || (ch >= 0xe0 && ch <= 0xfc)) &&
        babelIndex_ < babelBuffer_.size())
      dest += babelBuffer_[babelIndex_++];
    if (dest == std::string("\x89\xa0", 2))
      dest = " ";
    int position = glyphPosition ? glyphPosition(dest) : 0;
    machine_.SetIntValue(
        libreallive::IntMemRef(
            static_cast<int>((static_cast<unsigned>(xmod) >> 16) % 26),
            xmod & 0xffff),
        position);
    return result(code);
  }
  return result(1);
}
bool Session::babel(const CommandElement &c) {
  if (c.modtype() != 2 || c.module() != 1 || c.opcode() != 12 ||
      c.GetParamCount() < 2)
    return false;
  auto arg = [&](std::size_t i) {
    return i < c.GetParamCount() ? IntegerParameter(machine_, c, i) : 0;
  };
  const int function = arg(1), a = arg(2), b = arg(3);
  int result = 1;
  switch (function) {
  case 0:
    break;
  case 10:
    ++babelStarts_;
    babelBuffer_.clear();
    babelIndex_ = 0;
    babelItalic_ = false;
    if (a != -1)
      result = babelAppend(a);
    break;
  case 11:
    result = babelAppend(a);
    break;
  case 12:
  case 99:
    result = babelGetChar(a, b);
    break;
  case 13:
    if (!babelString(a).empty()) {
      auto rest = babelBuffer_.substr(babelIndex_);
      babelBuffer_ = babelString(a) + char(2) + '"' + rest;
      babelIndex_ = 0;
      babelItalic_ = false;
    }
    break;
  case 20:
    babelGlosses_.clear();
    glossRectangles_.clear();
    break;
  case 21:
    if (!glossCursor)
      throw std::runtime_error("AIR glossary requires text layout cursor");
    glossStart_ = glossCursor();
    break;
  case 22: {
    if (!glossCursor)
      throw std::runtime_error("AIR glossary requires text layout cursor");
    auto end = glossCursor(), start = glossStart_;
    if (start.lineHeight <= 0 || end.y < start.y)
      throw std::runtime_error("AIR invalid glossary layout");
    std::vector<GlossRectangle> rectangles;
    for (int y = start.y; y < end.y; y += start.lineHeight)
      rectangles.push_back({y == start.y ? start.x : start.left, y, start.right,
                            y + start.lineHeight});
    rectangles.push_back({end.y == start.y ? start.x : start.left, end.y, end.x,
                          end.y + start.lineHeight});
    babelGlosses_.push_back(babelString(a));
    glossRectangles_.push_back(std::move(rectangles));
    break;
  }
  case 23: {
    result = 0;
    for (std::size_t i = 0; i < glossRectangles_.size() && !result; ++i)
      for (const auto &rect : glossRectangles_[i])
        if (a >= rect.left && a < rect.right && b >= rect.top &&
            b < rect.bottom) {
          babelString(arg(4)) = babelGlosses_[i];
          result = 1;
          break;
        }
    break;
  }
  case 98:
    break;
  case 100: {
    int win = a < 0 ? currentWindow : a;
    result = babelNameModes_[win];
    babelNameModes_[win] = b;
    break;
  }
  case 101:
    result = babelNameModes_[a < 0 ? currentWindow : a];
    break;
  case 102:
    if (speaker)
      speaker(babelString(a));
    break;
  case 103:
    result = currentWindow;
    break;
  case 104:
  case 105:
    return false;
  case 200: {
    std::vector<std::string> labels;
    for (std::size_t i = 3; i < c.GetParamCount(); ++i)
      if (arg(i))
        labels.push_back(babelString(arg(i)));
    if (!selectionText)
      return false;
    selectionText(a, labels);
    break;
  }
  default:
    return false;
  }
  machine_.set_store_register(result);
  ++pc_;
  return true;
}
namespace {
constexpr std::size_t kSaveLimit = 64 * 1024 * 1024;
struct SaveWriter {
  std::string bytes = "AIRSES02";
  void number(std::uint32_t v) {
    for (int n = 0; n < 4; ++n)
      bytes += static_cast<char>(v >> (n * 8));
  }
  void signedNumber(int v) { number(static_cast<std::uint32_t>(v)); }
  void size(std::size_t n) {
    if (n > 1000000)
      throw std::runtime_error("AIR snapshot collection too large");
    number(static_cast<std::uint32_t>(n));
  }
  void string(const std::string &s) {
    if (s.size() > 16 * 1024 * 1024)
      throw std::runtime_error("AIR snapshot string too large");
    number(static_cast<std::uint32_t>(s.size()));
    bytes += s;
    if (bytes.size() > kSaveLimit)
      throw std::runtime_error("AIR snapshot too large");
  }
};
struct SaveReader {
  const std::string &bytes;
  std::size_t pos = 8;
  explicit SaveReader(const std::string &b) : bytes(b) {
    if (b.size() > kSaveLimit || b.size() < 8 ||
        (b.compare(0, 8, "AIRSES01") != 0 && b.compare(0, 8, "AIRSES02") != 0))
      throw std::runtime_error("Invalid AIR snapshot version or size");
  }
  std::uint32_t number() {
    if (bytes.size() - pos < 4)
      throw std::runtime_error("Truncated AIR snapshot");
    std::uint32_t v = 0;
    for (int n = 0; n < 4; ++n)
      v |= std::uint32_t(static_cast<unsigned char>(bytes[pos++])) << (n * 8);
    return v;
  }
  int signedNumber() {
    auto n = number();
    std::int32_t v;
    std::memcpy(&v, &n, sizeof v);
    return v;
  }
  std::size_t size() {
    auto n = number();
    if (n > 1000000)
      throw std::runtime_error("Invalid AIR snapshot count");
    return n;
  }
  bool boolean() {
    auto n = number();
    if (n > 1)
      throw std::runtime_error("Invalid AIR snapshot boolean");
    return n != 0;
  }
  std::string string() {
    auto n = number();
    if (n > 16 * 1024 * 1024 || n > bytes.size() - pos)
      throw std::runtime_error("Invalid AIR snapshot string");
    auto s = bytes.substr(pos, n);
    pos += n;
    return s;
  }
};
} // namespace
std::string Session::snapshot() const {
  SaveWriter out;
  out.signedNumber(scene_);
  out.size(pc_);
  out.number(blocked_);
  out.number(finished_);
  out.signedNumber(machine_.store_register());
  out.signedNumber(machine_.lineNumber);
  out.signedNumber(machine_.kidokuMarker);
  out.signedNumber(currentWindow);
  out.size(stack_.size());
  for (const auto &f : stack_) {
    out.signedNumber(f.scene);
    out.size(f.pc);
  }
  const auto &mem = machine_.memory();
  out.size(mem.integers.size());
  for (const auto &[key, value] : mem.integers) {
    out.signedNumber(key.first);
    out.signedNumber(key.second);
    out.signedNumber(value);
  }
  out.size(mem.strings.size());
  for (const auto &[key, value] : mem.strings) {
    out.signedNumber(key.first);
    out.signedNumber(key.second);
    out.string(value);
  }
  out.string(babelBuffer_);
  out.size(babelIndex_);
  out.number(babelStarts_);
  out.number(babelItalic_);
  out.size(babelNameModes_.size());
  for (const auto &[win, mode] : babelNameModes_) {
    out.signedNumber(win);
    out.signedNumber(mode);
  }
  out.size(babelGlosses_.size());
  for (const auto &gloss : babelGlosses_)
    out.string(gloss);
  auto cursor = [&](const GlossCursor &p) {
    out.signedNumber(p.x);
    out.signedNumber(p.y);
    out.signedNumber(p.left);
    out.signedNumber(p.right);
    out.signedNumber(p.lineHeight);
  };
  cursor(glossStart_);
  out.size(glossRectangles_.size());
  for (const auto &regions : glossRectangles_) {
    out.size(regions.size());
    for (const auto &r : regions) {
      out.signedNumber(r.left);
      out.signedNumber(r.top);
      out.signedNumber(r.right);
      out.signedNumber(r.bottom);
    }
  }
  if (out.bytes.size() > kSaveLimit)
    throw std::runtime_error("AIR snapshot too large");
  return out.bytes;
}
void Session::restore(const std::string &data) {
  SaveReader in(data);
  const int scene = in.signedNumber();
  const auto pc = in.size();
  const bool blocked = in.boolean(), finished = in.boolean();
  const int store = in.signedNumber(), line = in.signedNumber(),
            kidoku = in.signedNumber(), window = in.signedNumber();
  if (window < 0 || window > 65535)
    throw std::runtime_error("Invalid AIR snapshot window");
  std::vector<Frame> stack;
  auto count = in.size();
  if (count > 65536)
    throw std::runtime_error("Invalid AIR snapshot stack depth");
  stack.reserve(count);
  for (std::size_t n = 0; n < count; ++n) {
    const int sc = in.signedNumber();
    const auto index = in.size();
    stack.push_back({sc, index});
  }
  RLMemory memory;
  auto key = [&]() {
    int bank = in.signedNumber(), index = in.signedNumber();
    if (bank < 0 || bank > 255 || index < 0 || index > 1000000)
      throw std::runtime_error("Invalid AIR snapshot memory address");
    return std::make_pair(bank, index);
  };
  count = in.size();
  for (std::size_t n = 0; n < count; ++n) {
    auto address = key();
    int value = in.signedNumber();
    if (!memory.integers.emplace(address, value).second)
      throw std::runtime_error("Duplicate AIR snapshot integer");
  }
  count = in.size();
  for (std::size_t n = 0; n < count; ++n) {
    auto address = key();
    auto value = in.string();
    if (!memory.strings.emplace(address, std::move(value)).second)
      throw std::runtime_error("Duplicate AIR snapshot string");
  }
  auto buffer = in.string();
  const auto index = in.size();
  const unsigned starts = in.number();
  const bool italic = in.boolean();
  if (index > buffer.size())
    throw std::runtime_error("Invalid AIR snapshot Babel cursor");
  std::map<int, int> modes;
  count = in.size();
  for (std::size_t n = 0; n < count; ++n) {
    const int win = in.signedNumber(), mode = in.signedNumber();
    if (win < 0 || win > 65535 || !modes.emplace(win, mode).second)
      throw std::runtime_error("Invalid AIR snapshot name mode");
  }
  std::vector<std::string> glosses;
  count = in.size();
  glosses.reserve(count);
  for (std::size_t n = 0; n < count; ++n)
    glosses.push_back(in.string());
  GlossCursor glossStart;
  std::vector<std::vector<GlossRectangle>> rectangles;
  if (data.compare(0, 8, "AIRSES02") == 0) {
    glossStart.x = in.signedNumber();
    glossStart.y = in.signedNumber();
    glossStart.left = in.signedNumber();
    glossStart.right = in.signedNumber();
    glossStart.lineHeight = in.signedNumber();
    if (glossStart.lineHeight <= 0 || glossStart.lineHeight > 4096)
      throw std::runtime_error("Invalid AIR glossary line height");
    auto regionCount = in.size();
    if (regionCount != glosses.size())
      throw std::runtime_error("Invalid AIR glossary count");
    rectangles.resize(regionCount);
    for (auto &regions : rectangles) {
      auto n = in.size();
      if (n > 65536)
        throw std::runtime_error("Invalid AIR glossary rectangles");
      for (std::size_t j = 0; j < n; ++j) {
        GlossRectangle r;
        r.left = in.signedNumber();
        r.top = in.signedNumber();
        r.right = in.signedNumber();
        r.bottom = in.signedNumber();
        if (r.right < r.left || r.bottom < r.top)
          throw std::runtime_error("Invalid AIR glossary rectangle");
        regions.push_back(r);
      }
    }
  }
  if (data.compare(0, 8, "AIRSES01") == 0)
    rectangles.resize(glosses.size());
  if (in.pos != data.size())
    throw std::runtime_error("Trailing AIR snapshot bytes");
  // Loading scenario caches is read-only with respect to interpreter state.
  // Every potentially throwing parse and validation completes before commit.
  if (pc > script(scene).size())
    throw std::runtime_error("Invalid AIR snapshot program counter");
  for (const auto &f : stack)
    if (f.pc > script(f.scene).size())
      throw std::runtime_error("Invalid AIR snapshot return address");
  machine_.memory().integers.swap(memory.integers);
  machine_.memory().strings.swap(memory.strings);
  machine_.set_store_register(store);
  machine_.lineNumber = line;
  machine_.kidokuMarker = kidoku;
  scene_ = scene;
  pc_ = pc;
  blocked_ = blocked;
  finished_ = finished;
  currentWindow = window;
  stack_.swap(stack);
  babelBuffer_.swap(buffer);
  babelIndex_ = index;
  babelStarts_ = starts;
  babelItalic_ = italic;
  babelNameModes_.swap(modes);
  babelGlosses_.swap(glosses);
  glossStart_ = glossStart;
  glossRectangles_.swap(rectangles);
}
void Session::execute(const CommandElement &c) {
  if (flow(c) || strings(c) || memory(c) || babel(c))
    return;
  auto beforeScene = scene_;
  auto beforePc = pc_;
  if (command && command(c)) {
    if (scene_ == beforeScene && pc_ == beforePc)
      ++pc_;
  } else
    blocked_ = true;
}
} // namespace Air
