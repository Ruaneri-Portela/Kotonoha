#pragma once
#include <Amagami/Formats.hpp>
#include <Kotonoha/core/Vm.hpp>
#include <functional>
#include <map>
#include <memory>
#include <optional>

namespace Amagami {
struct VmObject;
struct VmValue {
  enum Kind {
    Nil,
    Number,
    Boolean,
    Text,
    Symbol,
    Class,
    Object,
    Array
  } kind = Nil;
  double number = 0;
  bool floating = false;
  std::string text;
  std::shared_ptr<VmObject> object;
  static VmValue num(double n, bool floating = false);
  static VmValue str(std::string text, Kind kind = Text);
  static VmValue ref(std::shared_ptr<VmObject> o, Kind kind = Object);
  bool truth() const;
  const std::string &asText() const;
};
struct VmObject {
  std::string type;
  std::vector<VmValue> fields;
  std::optional<std::string> stringValue;
};
// The SCF stack machine, recovered from SLPS_259.18 @ 0x11dfe8.
// A host implements the native PS2 API; unhandled calls fail with a script PC.
class Vm : public Kotonoha::Vm {
public:
  using Native = std::function<std::optional<VmValue>(
      const VmValue &, const std::string &, const std::vector<VmValue> &)>;
  // Host-provided native primitives, opcode 0x34. The receiver is the running
  // frame's self; args are the method's declared locals (its arguments).
  // Returning nullopt signals "not implemented" and fails the call.
  using Primitive = std::function<std::optional<VmValue>(
      const VmValue &, unsigned, const std::vector<VmValue> &)>;
  using ScopedPrimitive = std::function<std::optional<VmValue>(
      const std::string &owner, const std::string &method, bool classMethod,
      const VmValue &, unsigned, const std::vector<VmValue> &)>;
  void add(Script script);
  void reset();
  // Begin another script entry while preserving campaign globals and objects.
  void resetExecution();
  Vm fork(const VmValue &receiver, const std::string &method) const;
  void call(const VmValue &receiver, const std::string &name,
            std::vector<VmValue> args = {});
  struct OpcodeInfo {
    const char *name;
    uint8_t operands;
    bool supported;
  };
  static const OpcodeInfo *opcodeInfo(uint8_t opcode);
  void run(size_t budget = 100000);
  void pause() { paused_ = true; }
  void resume(std::optional<VmValue> result = {});
  bool paused() const { return paused_; }
  bool finished() const { return frames_.empty(); }
  VmValue result() const { return result_; }
  VmValue instance(const std::string &name);
  VmValue &classField(const std::string &name, size_t slot);
  VmValue classFieldValue(const std::string &name, size_t slot) const;
  // Class-name of a value using the SCF class hierarchy (True/False ->
  // Boolean -> Object).
  std::string typeOf(const VmValue &v) const;
  // Walks the script class hierarchy from |type| to root.
  bool isKindOf(const std::string &type, const std::string &klass) const;
  bool hasClass(const std::string &name) const {
    return scripts_.count(name) != 0;
  }
  Native native;
  Primitive primitive;
  ScopedPrimitive scopedPrimitive;
  // Callback chamado periodicamente durante run(). Se retornar true, a VM
  // pausa e devolve controle ao caller (usado pelo Session para avançar o
  // input sintetico entre instrucoes SCF).
  std::function<bool()> shouldYield;
  std::string location() const;
  // PC do frame atual (0 se nao houver frames). Usado para detectar loops
  // de sleep no mesmo ponto do script.
  size_t pc() const;

protected:
  void dispatch(std::size_t budget) override { run(budget); }

private:
  struct Frame {
    const Script *script = nullptr;
    const Method *method = nullptr;
    VmValue self, result;
    std::vector<VmValue> locals, stack;
    size_t pc = 0;
    std::optional<VmValue> constructedResult;
    struct Handler {
      size_t target, stackSize;
    };
    std::vector<Handler> handlers;
  };
  std::map<std::string, Script> scripts_;
  std::map<std::string, std::shared_ptr<VmObject>> classes_;
  std::vector<Frame> frames_;
  VmValue result_, exception_;
  void raise(VmValue value);
  bool paused_ = false;
  VmValue constant(const Value &value);
  VmValue field(const VmValue &, size_t slot) const;
  void setField(const VmValue &, size_t slot, VmValue);
  void send(VmValue receiver, std::string selector, std::vector<VmValue> args,
            bool super = false);
  void deliver(VmValue value);
};
} // namespace Amagami
