#pragma once
#include <Amagami/Formats.hpp>
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
  std::string text;
  std::shared_ptr<VmObject> object;
  static VmValue num(double n);
  static VmValue str(std::string text, Kind kind = Text);
  static VmValue ref(std::shared_ptr<VmObject> o, Kind kind = Object);
  bool truth() const;
};
struct VmObject {
  std::string type;
  std::vector<VmValue> fields;
};
// The SCF stack machine, recovered from SLPS_259.18 @ 0x11dfe8.
// A host implements the native PS2 API; unhandled calls fail with a script PC.
class Vm {
public:
  using Native = std::function<std::optional<VmValue>(
      const VmValue &, const std::string &, const std::vector<VmValue> &)>;
  void add(Script script);
  void reset();
  void call(const VmValue &receiver, const std::string &name,
            std::vector<VmValue> args = {});
  void run(size_t budget = 100000);
  void pause() { paused_ = true; }
  void resume(std::optional<VmValue> result = {});
  bool paused() const { return paused_; }
  bool finished() const { return frames_.empty(); }
  VmValue result() const { return result_; }
  VmValue instance(const std::string &name);
  VmValue &classField(const std::string &name, size_t slot);
  Native native;
  std::string location() const;

private:
  struct Frame {
    const Script *script = nullptr;
    const Method *method = nullptr;
    VmValue self, result;
    std::vector<VmValue> locals, stack;
    size_t pc = 0;
  };
  std::map<std::string, Script> scripts_;
  std::map<std::string, std::shared_ptr<VmObject>> classes_;
  std::vector<Frame> frames_;
  VmValue result_;
  bool paused_ = false;
  VmValue constant(const Value &value);
  VmValue field(const VmValue &, size_t slot) const;
  void setField(const VmValue &, size_t slot, VmValue);
  void send(VmValue receiver, std::string selector, std::vector<VmValue> args,
            bool super = false);
  void deliver(VmValue value);
};
} // namespace Amagami
