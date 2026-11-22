#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace Kotonoha::LargePonPon {

struct DecodedText {
  std::string utf8;
  int fontProfile = 0;
  int fontSizeDelta = 0;
  int fontStyle = 0;
  int textSpeedOverride = 0;
};

// Host interface invoked by Large PonPon's 43 host opcodes (0x80..0xFD).
// Methods returning bool return true when the VM should yield (wait for user
// input, timer, transition, or modal UI) before executing the next instruction.
class LargePonPonHost {
public:
  virtual ~LargePonPonHost() = default;

  // Script loader for 0xF0 SCRIPT_JUMP and 0xF1 SCRIPT_CALL.
  virtual std::vector<uint8_t> loadScript(const std::string &name) = 0;

  // 0x80..0x89 Dialogue & Choices
  virtual void onMsgAppend(const std::vector<uint16_t> &words) = 0;
  virtual bool onMsgShow() = 0;
  virtual bool onWinOpen() = 0;
  virtual bool onWinClose() = 0;
  virtual void onSetSpeaker(const std::vector<uint16_t> &words) = 0;
  virtual void onChoiceClear() = 0;
  virtual void onChoiceAdd(int32_t id, int32_t flag,
                           const std::vector<uint16_t> &words) = 0;
  virtual bool onChoiceRun(int32_t defaultId) = 0;
  virtual void onSetSeenFlag(int32_t seen) = 0;
  virtual void onAttachVoice(const std::string &voice, int32_t channel) = 0;

  // 0xA0..0xAB Graphics, Sprites & Transitions
  virtual bool onTransition(int32_t type, int32_t speed) = 0;
  virtual void onLoadCg(const std::string &name) = 0;
  virtual void onLoadBg(const std::string &name) = 0;
  virtual void onLoadChar(int32_t slot, const std::string &name) = 0;
  virtual void onFillColor(int32_t bgr24) = 0;
  virtual bool onPlayPchr(const std::string &pchr, int32_t flags, int32_t x,
                          int32_t y, int32_t loops, const std::string &se) = 0;
  virtual bool onScreenShake(int32_t dx, int32_t dy, int32_t reps) = 0;
  virtual void onLoadCgVariant(const std::string &name, int32_t h,
                               int32_t p) = 0;
  virtual void onLoadCanm(int32_t slot, const std::string &name) = 0;
  virtual void onComposite() = 0;
  virtual void onDrawCanm(int32_t buf, int32_t slot, int32_t type,
                          int32_t frame) = 0;
  virtual void onSetCharPos(int32_t slot, int32_t x, int32_t y) = 0;

  // 0xC0..0xC7 Audio (BGM & Voice/SE)
  virtual void onBgmPlay(const std::string &name, int32_t loop) = 0;
  virtual void onBgmStop() = 0;
  virtual void onBgmFade(int32_t speed) = 0;
  virtual void onVoicePlay(const std::string &name, int32_t unk) = 0;
  virtual void onVoiceStop() = 0;
  virtual bool onVoiceWait(int32_t flags) = 0;
  virtual void onSetVoiceSync(int32_t val) = 0;

  // 0xF2..0xFD System, Modes & Saves
  virtual bool onWait(int32_t ms, int32_t flags) = 0;
  virtual bool onNameInput() = 0;
  virtual bool onMapMode() = 0;
  virtual void onSaveCheckpoint() = 0;
  virtual void onSetSaveEnable(int32_t enable) = 0;
  virtual bool onCheckSaveData() = 0;
  virtual bool onLoadDialog() = 0;
  virtual bool onReturnTitle(int32_t noSysSave) = 0;
  virtual bool onHeartMode(int32_t mode) = 0;
  virtual bool onCgMode(int32_t mode) = 0;
  virtual bool onTitleMenu(int32_t page) = 0;
  virtual bool onEndingWait(int32_t count) = 0;
};

class LargePonPonVm {
public:
  static constexpr size_t kSaveFileSize = 1568;
  static constexpr size_t kSavePayloadSize = 1536;

  explicit LargePonPonVm(LargePonPonHost &host);

  // Initialize global config words from MAINSYS.INI Config00..63.
  void setGlobalConfig(const std::array<int16_t, 64> &config);
  void setPlayerNames(const std::array<std::string, 4> &names);
  const std::array<std::string, 4> &playerNames() const { return playerNames_; }

  // Load and run DEFINE.OBJ to END, keeping it resident for CALL_SUB.
  bool loadDefine(std::vector<uint8_t> defineBytes);

  // Jump to a top-level script (e.g., "TITLE" or "START") at byte offset pc.
  bool jumpScript(const std::string &name, size_t pc = 0);

  // Execute one instruction. Returns true if VM yielded or halted.
  bool step();

  // Execute up to maxSteps instructions until yield or halt.
  size_t runUntilYield(size_t maxSteps = 4096);

  bool halted() const { return halted_; }
  const std::string &currentScriptName() const { return currentScriptName_; }
  size_t pc() const { return pc_; }

  // Variable accessors
  bool getBit(int32_t idx) const;
  void setBit(int32_t idx, bool val);
  int8_t getS8(int32_t idx) const;
  uint8_t getU8(int32_t idx) const;
  void setS8(int32_t idx, int32_t val);
  void setU8(int32_t idx, int32_t val);
  int16_t getW(int32_t idx) const;
  void setW(int32_t idx, int32_t val);
  uint8_t getGB(int32_t idx) const;
  void setGB(int32_t idx, uint8_t val);
  int16_t getGW(int32_t idx) const;
  void setGW(int32_t idx, int16_t val);

  // Decode Large PonPon u16 word stream (Shift-JIS + inline control codes) into
  // UTF-8 + metadata using current protagonist names.
  DecodedText decodeTextWords(const std::vector<uint16_t> &words) const;

  // Save/Load exact 1,568-byte SAVEDATAxx.DAT format.
  std::vector<uint8_t> serializeSaveData(bool useCheckpoint = true) const;
  bool deserializeSaveData(const std::vector<uint8_t> &data,
                           std::string &outScriptName, uint32_t &outPc);
  bool mergeGlobalFlagsFromSave(const std::vector<uint8_t> &data,
                                bool isSystemSave99 = false);

  // Reset per-playthrough state (preserving global persistent flags B[1024..4095],
  // GB, GW, DEFINE.OBJ subroutines).
  void resetForTitle();

private:
  struct CodeRef {
    std::shared_ptr<std::vector<uint8_t>> code;
    size_t offset = 0;
  };

  struct ScriptFrame {
    std::string name;
    std::shared_ptr<std::vector<uint8_t>> code;
    size_t pc = 0;
    std::vector<CodeRef> subStack;
  };

  uint8_t readU8();
  int32_t readVarInt();
  int32_t readArg();
  int32_t evalExpr();
  std::string readAsciiString();
  std::vector<uint16_t> readTextWords();
  void storeScriptNameInBytes(const std::string &name);
  void snapshotPayload(uint8_t out[kSavePayloadSize]) const;

  LargePonPonHost &host_;

  std::shared_ptr<std::vector<uint8_t>> defineCode_;
  std::shared_ptr<std::vector<uint8_t>> activeCode_;
  std::shared_ptr<std::vector<uint8_t>> currentScriptCode_;
  std::string currentScriptName_;
  size_t pc_ = 0;
  bool halted_ = false;

  std::array<CodeRef, 256> subTable_{};
  std::vector<CodeRef> subStack_;
  std::vector<ScriptFrame> scriptStack_;

  // Variable banks (1,536 bytes total for saveable state + global banks)
  std::array<uint8_t, 512> bits_{};        // B[0..4095]
  std::array<uint8_t, 512> bytes_{};       // S8/U8[0..511]
  std::array<int16_t, 256> words_{};       // W[0..255]
  std::array<uint8_t, 8> globalBytes_{};   // GB[0..7]
  std::array<int16_t, 64> globalWords_{};  // GW[0..63]

  std::array<uint8_t, kSavePayloadSize> checkpointSnapshot_{};
  bool hasCheckpoint_ = false;
  std::array<std::string, 4> playerNames_{"伊能", "歩", "いのう", "あゆむ"};
};

} // namespace Kotonoha::LargePonPon
