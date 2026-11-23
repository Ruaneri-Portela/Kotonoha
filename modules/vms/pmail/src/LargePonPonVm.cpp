#include <Pmail/LargePonPonVm.hpp>

#include <Kotonoha/localization/Encoding.hpp>

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <cstring>

namespace Kotonoha::LargePonPon {

namespace {

// Exact 30-byte CP932 header for SAVEDATAxx.DAT:
// "このFILEはSAVEデーターです。    "
constexpr std::array<uint8_t, 30> kSaveHeader = {
    0x82, 0xB1, 0x82, 0xCC, 0x46, 0x49, 0x4C, 0x45, 0x82, 0xCD,
    0x53, 0x41, 0x56, 0x45, 0x83, 0x66, 0x81, 0x5B, 0x83, 0x5E,
    0x81, 0x5B, 0x82, 0xC5, 0x82, 0xB7, 0x81, 0x42, 0x20, 0x20};

// Extract the n-th UTF-8 character from a string.
std::string utf8CharAt(const std::string &s, size_t charIndex) {
  size_t idx = 0;
  for (size_t i = 0; i < s.size();) {
    unsigned char c = static_cast<unsigned char>(s[i]);
    size_t len = 1;
    if ((c & 0xE0) == 0xC0)
      len = 2;
    else if ((c & 0xF0) == 0xE0)
      len = 3;
    else if ((c & 0xF8) == 0xF0)
      len = 4;
    if (i + len > s.size())
      len = s.size() - i;
    if (idx == charIndex)
      return s.substr(i, len);
    i += len;
    ++idx;
  }
  return {};
}

} // namespace

LargePonPonVm::LargePonPonVm(LargePonPonHost &host) : host_(host) {}

void LargePonPonVm::setGlobalConfig(const std::array<int16_t, 64> &config) {
  globalWords_ = config;
}

void LargePonPonVm::setPlayerNames(const std::array<std::string, 4> &names) {
  playerNames_ = names;
}

bool LargePonPonVm::loadDefine(std::vector<uint8_t> defineBytes) {
  if (defineBytes.empty())
    return false;
  defineCode_ = std::make_shared<std::vector<uint8_t>>(std::move(defineBytes));
  activeCode_ = defineCode_;
  currentScriptCode_ = defineCode_;
  currentScriptName_ = "DEFINE";
  pc_ = 0;
  halted_ = false;
  subStack_.clear();
  scriptStack_.clear();
  for (auto &s : subTable_)
    s = {};

  // Execute DEFINE.OBJ until END (0x00) to populate subTable_.
  size_t guard = 0;
  while (!halted_ && guard++ < 65536) {
    step();
  }
  halted_ = false;
  return true;
}

void LargePonPonVm::storeScriptNameInBytes(const std::string &name) {
  std::memset(bytes_.data(), 0, 16);
  for (size_t i = 0; i < name.size() && i < 15; ++i)
    bytes_[i] = static_cast<uint8_t>(name[i]);
}

bool LargePonPonVm::jumpScript(const std::string &name, size_t pc) {
  auto bytes = host_.loadScript(name);
  if (bytes.empty())
    return false;
  currentScriptCode_ = std::make_shared<std::vector<uint8_t>>(std::move(bytes));
  activeCode_ = currentScriptCode_;
  currentScriptName_ = name;
  storeScriptNameInBytes(name);
  pc_ = pc;
  halted_ = false;
  subStack_.clear();
  return true;
}

void LargePonPonVm::resetForTitle() {
  // Preserve global persistent flags B[1024..4095] (bytes 128..511 of bits_)
  std::memset(bits_.data(), 0, 128);
  const uint8_t persistentByte21 = bytes_[21];
  bytes_.fill(0);
  bytes_[21] = persistentByte21;
  words_.fill(0);
  subStack_.clear();
  scriptStack_.clear();
  hasCheckpoint_ = false;
  halted_ = false;
}

uint8_t LargePonPonVm::readU8() {
  if (!activeCode_ || pc_ >= activeCode_->size()) {
    halted_ = true;
    return 0;
  }
  return (*activeCode_)[pc_++];
}

int32_t LargePonPonVm::readVarInt() {
  const uint8_t b = readU8();
  if (b < 0x80)
    return static_cast<int32_t>(b);
  int extra = (b >> 4) & 3;
  int32_t v = b & 0x0F;
  while (extra-- > 0)
    v = (v << 8) | readU8();
  return (b & 0x40) ? -v : v;
}

int32_t LargePonPonVm::readArg() {
  const uint8_t tag = readU8();
  if (tag == 0x04)
    return evalExpr();
  if (tag == 0x44)
    return readVarInt();
  return 0;
}

std::string LargePonPonVm::readAsciiString() {
  const int32_t len = readVarInt();
  if (len <= 0)
    return {};
  std::string out;
  out.reserve(static_cast<size_t>(len));
  for (int32_t i = 0; i < len; ++i) {
    const uint8_t raw = readU8();
    const uint8_t key = static_cast<uint8_t>((0x5A - i) & 0xFF);
    const char c = static_cast<char>(raw ^ key);
    if (c != '\0')
      out.push_back(c);
  }
  return out;
}

std::vector<uint16_t> LargePonPonVm::readTextWords() {
  const int32_t byteLen = readVarInt();
  if (byteLen <= 0)
    return {};
  const int32_t wordCount = byteLen / 2;
  std::vector<uint16_t> out;
  out.reserve(static_cast<size_t>(wordCount));
  for (int32_t i = 0; i < wordCount; ++i) {
    const uint8_t lo = readU8();
    const uint8_t hi = readU8();
    const uint16_t raw =
        static_cast<uint16_t>(lo | (static_cast<uint16_t>(hi) << 8));
    const uint16_t key = static_cast<uint16_t>((0x5A5A + i) & 0xFFFF);
    out.push_back(static_cast<uint16_t>(raw ^ key));
  }
  if (byteLen & 1)
    (void)readU8();
  return out;
}

bool LargePonPonVm::getBit(int32_t idx) const {
  if (idx < 0 || idx >= 4096)
    return false;
  return (bits_[static_cast<size_t>(idx) >> 3] & (1u << (idx & 7))) != 0;
}

void LargePonPonVm::setBit(int32_t idx, bool val) {
  if (idx < 0 || idx >= 4096)
    return;
  uint8_t &b = bits_[static_cast<size_t>(idx) >> 3];
  const uint8_t mask = static_cast<uint8_t>(1u << (idx & 7));
  if (val)
    b |= mask;
  else
    b &= static_cast<uint8_t>(~mask);
}

int8_t LargePonPonVm::getS8(int32_t idx) const {
  if (idx < 0 || idx >= 512)
    return 0;
  return static_cast<int8_t>(bytes_[static_cast<size_t>(idx)]);
}

uint8_t LargePonPonVm::getU8(int32_t idx) const {
  if (idx < 0 || idx >= 512)
    return 0;
  return bytes_[static_cast<size_t>(idx)];
}

void LargePonPonVm::setS8(int32_t idx, int32_t val) {
  if (idx < 0 || idx >= 512)
    return;
  const int32_t clamped = std::clamp(val, -128, 127);
  bytes_[static_cast<size_t>(idx)] =
      static_cast<uint8_t>(static_cast<int8_t>(clamped));
}

void LargePonPonVm::setU8(int32_t idx, int32_t val) {
  if (idx < 0 || idx >= 512)
    return;
  bytes_[static_cast<size_t>(idx)] =
      static_cast<uint8_t>(std::clamp(val, 0, 255));
}

int16_t LargePonPonVm::getW(int32_t idx) const {
  if (idx < 0 || idx >= 256)
    return 0;
  if (idx == 2)
    return static_cast<int16_t>(std::rand() & 0x7FFF);
  if (idx >= 3 && idx <= 15)
    return -1;
  return words_[static_cast<size_t>(idx)];
}

void LargePonPonVm::setW(int32_t idx, int32_t val) {
  if (idx < 0 || idx >= 256)
    return;
  words_[static_cast<size_t>(idx)] =
      static_cast<int16_t>(std::clamp(val, -32768, 32767));
}

uint8_t LargePonPonVm::getGB(int32_t idx) const {
  if (idx < 0 || idx >= 8)
    return 0;
  return globalBytes_[static_cast<size_t>(idx)];
}

void LargePonPonVm::setGB(int32_t idx, uint8_t val) {
  if (idx < 0 || idx >= 8)
    return;
  globalBytes_[static_cast<size_t>(idx)] = val;
}

int16_t LargePonPonVm::getGW(int32_t idx) const {
  if (idx < 0 || idx >= 64)
    return 0;
  return globalWords_[static_cast<size_t>(idx)];
}

void LargePonPonVm::setGW(int32_t idx, int16_t val) {
  if (idx < 0 || idx >= 64)
    return;
  globalWords_[static_cast<size_t>(idx)] = val;
}

int32_t LargePonPonVm::evalExpr() {
  std::vector<int32_t> stack;
  stack.reserve(16);

  auto pop = [&]() -> int32_t {
    if (stack.empty())
      return 0;
    const int32_t v = stack.back();
    stack.pop_back();
    return v;
  };

  while (!halted_) {
    const uint8_t op = readU8();
    switch (op) {
    case 0x00: {
      const int32_t result = stack.empty() ? 0 : stack.back();
      setW(0, result);
      return result;
    }
    case 0x08:
      stack.push_back(getGB(readVarInt()));
      break;
    case 0x09:
      stack.push_back(getGW(readVarInt()));
      break;
    case 0x40:
      stack.push_back(getBit(readVarInt()) ? 1 : 0);
      break;
    case 0x41:
      stack.push_back(getS8(readVarInt()));
      break;
    case 0x42:
      stack.push_back(getU8(readVarInt()));
      break;
    case 0x43:
      stack.push_back(getW(readVarInt()));
      break;
    case 0x44:
      stack.push_back(readVarInt());
      break;
    case 0x20: {
      const int32_t b = pop();
      const int32_t a = pop();
      stack.push_back(a + b);
      break;
    }
    case 0x21: {
      const int32_t b = pop();
      const int32_t a = pop();
      stack.push_back(a - b);
      break;
    }
    case 0x22: {
      const int32_t b = pop();
      const int32_t a = pop();
      stack.push_back(a * b);
      break;
    }
    case 0x23: {
      const int32_t b = pop();
      const int32_t a = pop();
      stack.push_back(b != 0 ? (a / b) : 0);
      break;
    }
    case 0x24: {
      const int32_t b = pop();
      const int32_t a = pop();
      stack.push_back(a & b);
      break;
    }
    case 0x25: {
      const int32_t b = pop();
      const int32_t a = pop();
      stack.push_back(a | b);
      break;
    }
    case 0x26: {
      const int32_t b = pop();
      const int32_t a = pop();
      stack.push_back(a ^ b);
      break;
    }
    case 0x27: {
      const int32_t b = pop();
      const int32_t a = pop();
      stack.push_back((a != 0 || b != 0) ? -1 : 0);
      break;
    }
    case 0x28: {
      const int32_t b = pop();
      const int32_t a = pop();
      stack.push_back((a != 0 && b != 0) ? -1 : 0);
      break;
    }
    case 0x30: {
      const int32_t b = pop();
      const int32_t a = pop();
      stack.push_back(a < b ? -1 : 0);
      break;
    }
    case 0x31: {
      const int32_t b = pop();
      const int32_t a = pop();
      stack.push_back(a > b ? -1 : 0);
      break;
    }
    case 0x32: {
      const int32_t b = pop();
      const int32_t a = pop();
      stack.push_back(a <= b ? -1 : 0);
      break;
    }
    case 0x33: {
      const int32_t b = pop();
      const int32_t a = pop();
      stack.push_back(a >= b ? -1 : 0);
      break;
    }
    case 0x34: {
      const int32_t b = pop();
      const int32_t a = pop();
      stack.push_back(a == b ? -1 : 0);
      break;
    }
    case 0x35: {
      const int32_t b = pop();
      const int32_t a = pop();
      stack.push_back(a != b ? -1 : 0);
      break;
    }
    default:
      // Unknown RPN op: terminate safely
      setW(0, stack.empty() ? 0 : stack.back());
      return stack.empty() ? 0 : stack.back();
    }
  }
  return 0;
}

DecodedText
LargePonPonVm::decodeTextWords(const std::vector<uint16_t> &words) const {
  DecodedText out;
  std::string sjisBuf;

  auto flushSjis = [&]() {
    if (sjisBuf.empty())
      return;
    try {
      out.utf8 += Kotonoha::ShiftJisToUtf8(sjisBuf);
    } catch (const std::exception &) {
      out.utf8 += sjisBuf;
    }
    sjisBuf.clear();
  };

  for (size_t i = 0; i < words.size(); ++i) {
    const uint16_t w = words[i];
    const uint8_t hi = static_cast<uint8_t>(w >> 8);
    const uint8_t lo = static_cast<uint8_t>(w & 0xFF);
    if (hi == 0x00) {
      if (lo == 0x00)
        continue;
      if (lo == 0x0A || lo == 0x0D) {
        flushSjis();
        out.utf8.push_back('\n');
      } else {
        sjisBuf.push_back(static_cast<char>(lo));
      }
    } else if (hi == 0x01) {
      out.fontStyle = static_cast<int>(lo) - 0x80;
    } else if (hi == 0x02) {
      out.fontSizeDelta = static_cast<int>(lo) - 0x80;
    } else if (hi == 0x03) {
      out.textSpeedOverride = static_cast<int>(lo) - 0x80;
    } else if (hi == 0x04) {
      // Inline pause marker; ignored in plain text buffer
    } else if (hi == 0x05) {
      const int profile = static_cast<int>(lo) - 0x80;
      if (profile >= 0 && profile < 21)
        out.fontProfile = profile;
    } else if (hi == 0x10) {
      flushSjis();
      const int idx = static_cast<int>(lo) - 0x80;
      if (idx >= 0 && idx < 4)
        out.utf8 += playerNames_[static_cast<size_t>(idx)];
    } else if (hi == 0x11) {
      flushSjis();
      const int idx = static_cast<int>(lo) - 0x80;
      uint16_t charIdx = 0;
      if (i + 1 < words.size())
        charIdx = words[++i];
      if (idx >= 0 && idx < 4)
        out.utf8 += utf8CharAt(playerNames_[static_cast<size_t>(idx)], charIdx);
    } else if (hi >= 0x81) {
      sjisBuf.push_back(static_cast<char>(hi));
      sjisBuf.push_back(static_cast<char>(lo));
    }
  }
  flushSjis();
  return out;
}

bool LargePonPonVm::step() {
  if (halted_ || !activeCode_ || pc_ >= activeCode_->size()) {
    halted_ = true;
    return true;
  }

  const size_t opAddr = pc_;
  const uint8_t op = readU8();

  switch (op) {
  // ---- Core Opcodes (0x00..0x67) ----
  case 0x00: { // END
    if (!scriptStack_.empty()) {
      auto frame = std::move(scriptStack_.back());
      scriptStack_.pop_back();
      currentScriptName_ = std::move(frame.name);
      currentScriptCode_ = std::move(frame.code);
      activeCode_ = currentScriptCode_;
      pc_ = frame.pc;
      subStack_ = std::move(frame.subStack);
      storeScriptNameInBytes(currentScriptName_);
      return false;
    }
    halted_ = true;
    return true;
  }
  case 0x01: { // JMP
    const int32_t off = readVarInt();
    pc_ = static_cast<size_t>(static_cast<int64_t>(opAddr + 1) + off);
    return false;
  }
  case 0x02: { // JNZ
    const int32_t off = readVarInt();
    if (evalExpr() != 0)
      pc_ = static_cast<size_t>(static_cast<int64_t>(opAddr + 1) + off);
    return false;
  }
  case 0x03: { // JZ
    const int32_t off = readVarInt();
    if (evalExpr() == 0)
      pc_ = static_cast<size_t>(static_cast<int64_t>(opAddr + 1) + off);
    return false;
  }
  case 0x04: { // EVAL
    (void)evalExpr();
    return false;
  }
  case 0x0A: { // RET
    if (!subStack_.empty()) {
      auto ret = subStack_.back();
      subStack_.pop_back();
      activeCode_ = std::move(ret.code);
      pc_ = ret.offset;
    }
    return false;
  }
  case 0x12: // STORE_B
    setBit(readVarInt(), words_[0] != 0);
    return false;
  case 0x13: // STORE_S8
    setS8(readVarInt(), words_[0]);
    return false;
  case 0x14: // STORE_U8
    setU8(readVarInt(), words_[0]);
    return false;
  case 0x15: // STORE_W
    setW(readVarInt(), words_[0]);
    return false;
  case 0x52: { // DEF_SUB
    const uint8_t slot = readU8();
    (void)readU8(); // pad
    const int32_t len = readVarInt();
    subTable_[slot] = {activeCode_, pc_};
    pc_ = static_cast<size_t>(static_cast<int64_t>(opAddr + 3) + len);
    return false;
  }
  case 0x53: { // CALL_SUB
    const uint8_t slot = readU8();
    if (subTable_[slot].code) {
      subStack_.push_back({activeCode_, pc_});
      activeCode_ = subTable_[slot].code;
      pc_ = subTable_[slot].offset;
    }
    return false;
  }
  case 0x54: { // CLEAR_RANGE
    const uint8_t type = readU8();
    const int32_t start = readVarInt();
    const int32_t count = readVarInt();
    for (int32_t i = 0; i < count; ++i) {
      const int32_t idx = start + i;
      if (type == 0x40)
        setBit(idx, false);
      else if (type == 0x41)
        setS8(idx, 0);
      else if (type == 0x42)
        setU8(idx, 0);
      else if (type == 0x43)
        setW(idx, 0);
    }
    return false;
  }
  case 0x60:
    setBit(readVarInt(), false);
    return false;
  case 0x61:
    setS8(readVarInt(), 0);
    return false;
  case 0x62:
    setU8(readVarInt(), 0);
    return false;
  case 0x63:
    setW(readVarInt(), 0);
    return false;
  case 0x64:
    setBit(readVarInt(), true);
    return false;
  case 0x65:
    setS8(readVarInt(), 1);
    return false;
  case 0x66:
    setU8(readVarInt(), 1);
    return false;
  case 0x67:
    setW(readVarInt(), 1);
    return false;

  // ---- Host Opcodes (0x80..0xFD) ----
  case 0x80: // MSG_APPEND
    host_.onMsgAppend(readTextWords());
    return false;
  case 0x81: // MSG_SHOW
    return host_.onMsgShow();
  case 0x82: // WIN_OPEN
    return host_.onWinOpen();
  case 0x83: // WIN_CLOSE
    return host_.onWinClose();
  case 0x84: // SET_SPEAKER
    host_.onSetSpeaker(readTextWords());
    return false;
  case 0x85: // CHOICE_CLEAR
    host_.onChoiceClear();
    return false;
  case 0x86: { // CHOICE_ADD
    const int32_t id = readArg();
    const int32_t flag = readArg();
    const auto words = readTextWords();
    host_.onChoiceAdd(id, flag, words);
    return false;
  }
  case 0x87: // CHOICE_RUN
    return host_.onChoiceRun(readArg());
  case 0x88: // SET_SEEN_FLAG
    host_.onSetSeenFlag(readArg());
    return false;
  case 0x89: { // ATTACH_VOICE
    const std::string voice = readAsciiString();
    const int32_t ch = readArg();
    host_.onAttachVoice(voice, ch);
    return false;
  }
  case 0xA0: { // TRANSITION
    const int32_t type = readArg();
    const int32_t speed = readArg();
    return host_.onTransition(type, speed);
  }
  case 0xA1: // LOAD_CG
    host_.onLoadCg(readAsciiString());
    return false;
  case 0xA2: // LOAD_BG
    host_.onLoadBg(readAsciiString());
    return false;
  case 0xA3: { // LOAD_CHAR
    const int32_t slot = readArg();
    const std::string name = readAsciiString();
    host_.onLoadChar(slot, name);
    return false;
  }
  case 0xA4: // FILL_COLOR
    host_.onFillColor(readArg());
    return false;
  case 0xA5: { // PLAY_PCHR
    const std::string pchr = readAsciiString();
    const int32_t flags = readArg();
    const int32_t x = readArg();
    const int32_t y = readArg();
    const int32_t loops = readArg();
    const std::string se = readAsciiString();
    return host_.onPlayPchr(pchr, flags, x, y, loops, se);
  }
  case 0xA6: { // SCREEN_SHAKE
    const int32_t dx = readArg();
    const int32_t dy = readArg();
    const int32_t reps = readArg();
    return host_.onScreenShake(dx, dy, reps);
  }
  case 0xA7: { // LOAD_CG_VARIANT
    const std::string name = readAsciiString();
    const int32_t h = readArg();
    const int32_t p = readArg();
    host_.onLoadCgVariant(name, h, p);
    return false;
  }
  case 0xA8: { // LOAD_CANM
    const int32_t slot = readArg();
    const std::string name = readAsciiString();
    host_.onLoadCanm(slot, name);
    return false;
  }
  case 0xA9: // COMPOSITE
    host_.onComposite();
    return false;
  case 0xAA: { // DRAW_CANM
    const int32_t buf = readArg();
    const int32_t slot = readArg();
    const int32_t type = readArg();
    const int32_t frame = readArg();
    host_.onDrawCanm(buf, slot, type, frame);
    return false;
  }
  case 0xAB: { // SET_CHAR_POS
    const int32_t slot = readArg();
    const int32_t x = readArg();
    const int32_t y = readArg();
    host_.onSetCharPos(slot, x, y);
    return false;
  }
  case 0xC0: { // BGM_PLAY
    const std::string name = readAsciiString();
    const int32_t loop = readArg();
    host_.onBgmPlay(name, loop);
    return false;
  }
  case 0xC1: // BGM_STOP
    host_.onBgmStop();
    return false;
  case 0xC2: // BGM_FADE
    host_.onBgmFade(readArg());
    return false;
  case 0xC4: { // VOICE_PLAY
    const std::string name = readAsciiString();
    const int32_t unk = readArg();
    host_.onVoicePlay(name, unk);
    return false;
  }
  case 0xC5: // VOICE_STOP
    host_.onVoiceStop();
    return false;
  case 0xC6: // VOICE_WAIT
    return host_.onVoiceWait(readArg());
  case 0xC7: // SET_VOICE_SYNC
    host_.onSetVoiceSync(readArg());
    return false;
  case 0xF0: { // SCRIPT_JUMP
    const std::string name = readAsciiString();
    if (!jumpScript(name, 0))
      halted_ = true;
    return false;
  }
  case 0xF1: { // SCRIPT_CALL
    const std::string name = readAsciiString();
    auto bytes = host_.loadScript(name);
    if (!bytes.empty()) {
      scriptStack_.push_back(
          {currentScriptName_, currentScriptCode_, pc_, std::move(subStack_)});
      currentScriptCode_ =
          std::make_shared<std::vector<uint8_t>>(std::move(bytes));
      activeCode_ = currentScriptCode_;
      currentScriptName_ = name;
      storeScriptNameInBytes(name);
      pc_ = 0;
      subStack_.clear();
    }
    return false;
  }
  case 0xF2: { // WAIT
    const int32_t ms = readArg();
    const int32_t flags = readArg();
    return host_.onWait(ms, flags);
  }
  case 0xF3: // NAME_INPUT
    return host_.onNameInput();
  case 0xF4: // MAP_MODE
    return host_.onMapMode();
  case 0xF5: { // SAVE_CHECKPOINT
    const uint32_t savedPc = static_cast<uint32_t>(pc_);
    bytes_[16] = static_cast<uint8_t>(savedPc & 0xFF);
    bytes_[17] = static_cast<uint8_t>((savedPc >> 8) & 0xFF);
    bytes_[18] = static_cast<uint8_t>((savedPc >> 16) & 0xFF);
    bytes_[19] = static_cast<uint8_t>((savedPc >> 24) & 0xFF);
    snapshotPayload(checkpointSnapshot_.data());
    hasCheckpoint_ = true;
    host_.onSaveCheckpoint();
    return false;
  }
  case 0xF6: // SET_SAVE_ENABLE
    host_.onSetSaveEnable(readArg());
    return false;
  case 0xF7: // CHECK_SAVEDATA
    setW(1, host_.onCheckSaveData() ? -1 : 0);
    return false;
  case 0xF8: // LOAD_DIALOG
    return host_.onLoadDialog();
  case 0xF9: // RETURN_TITLE
    return host_.onReturnTitle(readArg());
  case 0xFA: // HEART_MODE
    return host_.onHeartMode(readArg());
  case 0xFB: // CG_MODE
    return host_.onCgMode(readArg());
  case 0xFC: // TITLE_MENU
    return host_.onTitleMenu(readArg());
  case 0xFD: // ENDING_WAIT
    return host_.onEndingWait(readArg());
  default:
    halted_ = true;
    return true;
  }
}

size_t LargePonPonVm::runUntilYield(size_t maxSteps) {
  size_t executed = 0;
  while (!halted_ && executed < maxSteps) {
    ++executed;
    if (step())
      break;
  }
  return executed;
}

void LargePonPonVm::snapshotPayload(uint8_t out[kSavePayloadSize]) const {
  std::memcpy(out + 0x000, bits_.data(), 512);
  std::memcpy(out + 0x200, bytes_.data(), 512);
  for (size_t i = 0; i < 256; ++i) {
    const uint16_t w = static_cast<uint16_t>(words_[i]);
    out[0x400 + i * 2 + 0] = static_cast<uint8_t>(w & 0xFF);
    out[0x400 + i * 2 + 1] = static_cast<uint8_t>((w >> 8) & 0xFF);
  }
}

std::vector<uint8_t>
LargePonPonVm::serializeSaveData(bool useCheckpoint) const {
  std::vector<uint8_t> out(kSaveFileSize, 0);
  std::memcpy(out.data(), kSaveHeader.data(), kSaveHeader.size());

  uint8_t *payload = out.data() + 0x20;
  if (useCheckpoint && hasCheckpoint_) {
    std::memcpy(payload, checkpointSnapshot_.data(), kSavePayloadSize);
    // Always keep the latest global persistent flags B[1024..4095] merged in.
    for (size_t i = 128; i < 512; ++i)
      payload[i] |= bits_[i];
  } else {
    snapshotPayload(payload);
  }

  uint32_t sum = 0;
  for (size_t i = 0; i < kSavePayloadSize; ++i)
    sum += payload[i];
  const uint16_t checksum = static_cast<uint16_t>(sum & 0xFFFF);
  out[0x1E] = static_cast<uint8_t>(checksum & 0xFF);
  out[0x1F] = static_cast<uint8_t>((checksum >> 8) & 0xFF);
  return out;
}

bool LargePonPonVm::deserializeSaveData(const std::vector<uint8_t> &data,
                                        std::string &outScriptName,
                                        uint32_t &outPc) {
  if (data.size() < kSaveFileSize)
    return false;
  const uint8_t *payload = data.data() + 0x20;
  uint32_t sum = 0;
  for (size_t i = 0; i < kSavePayloadSize; ++i)
    sum += payload[i];
  const uint16_t stored = static_cast<uint16_t>(
      data[0x1E] | (static_cast<uint16_t>(data[0x1F]) << 8));
  if ((sum & 0xFFFF) != stored)
    return false;

  // Restore local bits B[0..1023], OR global persistent bits B[1024..4095].
  std::memcpy(bits_.data(), payload + 0x000, 128);
  for (size_t i = 128; i < 512; ++i)
    bits_[i] |= payload[i];

  std::memcpy(bytes_.data(), payload + 0x200, 512);
  for (size_t i = 0; i < 256; ++i) {
    const uint16_t w = static_cast<uint16_t>(
        payload[0x400 + i * 2] |
        (static_cast<uint16_t>(payload[0x400 + i * 2 + 1]) << 8));
    words_[i] = static_cast<int16_t>(w);
  }

  outScriptName.clear();
  for (size_t i = 0; i < 16 && bytes_[i] != 0; ++i)
    outScriptName.push_back(static_cast<char>(bytes_[i]));

  outPc = static_cast<uint32_t>(bytes_[16]) |
          (static_cast<uint32_t>(bytes_[17]) << 8) |
          (static_cast<uint32_t>(bytes_[18]) << 16) |
          (static_cast<uint32_t>(bytes_[19]) << 24);

  subStack_.clear();
  scriptStack_.clear();
  return !outScriptName.empty();
}

bool LargePonPonVm::mergeGlobalFlagsFromSave(const std::vector<uint8_t> &data,
                                             bool isSystemSave99) {
  if (data.size() < kSaveFileSize)
    return false;
  const uint8_t *payload = data.data() + 0x20;
  uint32_t sum = 0;
  for (size_t i = 0; i < kSavePayloadSize; ++i)
    sum += payload[i];
  const uint16_t stored = static_cast<uint16_t>(
      data[0x1E] | (static_cast<uint16_t>(data[0x1F]) << 8));
  if ((sum & 0xFFFF) != stored)
    return false;

  for (size_t i = 128; i < 512; ++i)
    bits_[i] |= payload[i];
  if (isSystemSave99)
    bytes_[21] = payload[0x200 + 21];
  return true;
}

} // namespace Kotonoha::LargePonPon
