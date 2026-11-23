#pragma once
// Stack-based .SCR bytecode VM for Overflow's Imouto de Ikou! (runscrmod.dll).
#include <SummerRadish/Pack.hpp>
#include <SummerRadish/Codec.hpp>
#include <SummerRadish/Images.hpp>

#include <array>
#include <cstdint>
#include <functional>
#include <map>
#include <string>
#include <vector>

namespace SummerRadish {

struct ImoutoInstruction {
  uint32_t op = 0;
  int32_t a1 = 0;
  int32_t a2 = 0;
};

struct ImoutoScriptData {
  std::string sourcePath;
  std::vector<std::string> varNames;
  std::vector<std::string> strings;
  std::vector<ImoutoInstruction> code;
};

ImoutoScriptData parseImoutoScript(const Bytes &bytes);

struct ImoutoSlot {
  enum class Type : uint8_t { Int = 0, String = 1, Ref = 2 };
  Type type = Type::Int;
  int32_t intVal = 0;
  std::string strVal;
  size_t refSlot = 0;

  static ImoutoSlot makeInt(int32_t v) {
    ImoutoSlot s;
    s.type = Type::Int;
    s.intVal = v;
    return s;
  }
  static ImoutoSlot makeString(std::string v) {
    ImoutoSlot s;
    s.type = Type::String;
    s.strVal = std::move(v);
    return s;
  }
  static ImoutoSlot makeRef(size_t idx) {
    ImoutoSlot s;
    s.type = Type::Ref;
    s.refSlot = idx;
    return s;
  }
};

class ImoutoVM {
public:
  static constexpr size_t kSlotCount = 2048;
  static constexpr size_t kStackLimit = 32;
  static constexpr size_t kVarBase = 0x400;

  std::array<ImoutoSlot, kSlotCount> slots{};
  std::map<std::string, int32_t> persistentVars;
  std::map<std::string, std::vector<uint8_t>> readBits;

  std::string currentScriptName;
  ImoutoScriptData script;
  size_t sp = 0;
  size_t pc = 0;
  int32_t currentLine = 0;
  bool waiting = false;
  bool finished = false;
  bool lastSetBitWasRead = false;

  // Command handler: (commandName, paramsWithoutCmdName) -> int32 return value.
  std::function<int32_t(const std::string &, const std::vector<ImoutoSlot> &)>
      onCall;

  void reset();
  void loadScript(std::string name, const Bytes &bytes);
  void run(size_t maxSteps = 200000);
  void resume();

  int32_t systemReg(size_t index) const;
  void setSystemReg(size_t index, int32_t value);

private:
  bool scriptChanged_ = false;
  void push(ImoutoSlot slot);
  ImoutoSlot pop();
  int32_t popInt();
  size_t resolveSlot(int32_t base, int32_t mode);
  bool testAndSetReadBit(int32_t bitId);
};

} // namespace SummerRadish
