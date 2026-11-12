#pragma once
#include <Kotonoha/core/Vm.hpp>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace Pmail {

// Compiled script object (*.obj in script.dat):
//   [Magic: kPM7\0 or pm7\0][Filename][Nulls][Metadata][Content]
//   Metadata format: [length of metadata in bytes] (wait, I need to confirm this)
// Or metadata for 01m1.obj: cravis_,02&m3jlyn$1.3509 1 =)= .o2vp10Xp
// For 03e1.obj: 16 bytes before filename.
struct ScriptObject {
  std::string name;
  std::vector<uint8_t> data;
  uint16_t id = 0;
  std::vector<std::string> strings; // Not sure if Pmail has strings, maybe in the metadata?
  uint32_t codeOffset = 0;
  size_t codeWords = 0;

  size_t parse(const std::string &objName, std::vector<uint8_t> bytes);
  uint32_t word(size_t pc) const;
  const std::string &str(size_t idx) const;
};
using ObjectPtr = std::shared_ptr<ScriptObject>;

// A VM word that crosses the native boundary.
// Based on SnowRadish, but let's see if Pmail is different if we need it.
struct Arg {
  uint32_t raw = 0;
  uint16_t value() const { return raw & 0xffff; }
  int tag() const { return (raw >> 16) & 0xf; }
  uint32_t object() const { return raw >> 20; }
};

class Vm;

class Host {
public:
  virtual ~Host() = default;
  virtual void native(Vm &vm, int id, const std::vector<Arg> &args) = 0;
  virtual ObjectPtr loadByName(const std::string &name) = 0;
  virtual ObjectPtr loadById(uint32_t id12) = 0;
  virtual void log(const std::string &) {}
};

class Vm : public Kotonoha::Vm {
public:
  explicit Vm(Host &host);

  void registerObject(const ObjectPtr &obj);
  void unregisterObject(const ObjectPtr &obj);

  void start(const ObjectPtr &obj, uint32_t pc = 0);

  bool step();
  size_t run(size_t maxSteps);

  void block() { blocked_ = true; }
  void unblock() { blocked_ = false; }
  bool blocked() const { return blocked_; }
  bool ended() const { return ended_; }

  uint32_t var(uint32_t id) const { return id < vars_.size() ? vars_[id] : 0; }
  void setVar(uint32_t id, uint32_t v);
  int32_t value(const Arg &a) const;
  std::string text(const Arg &a);
  std::string textAt(uint32_t object12, uint32_t idx);

  struct Line {
    uint32_t object;
    uint32_t index;
  };
  std::vector<Line> &lines() { return lines_; }

  ObjectPtr current() const { return cur_; }
  uint32_t pc() const { return pc_; }

  void jumpTo(const ObjectPtr &obj, uint32_t pc);

  size_t stackDepth() const { return stack_.size(); }

protected:
  void dispatch(std::size_t budget) override { run(budget); }

private:
  struct Entry {
    uint32_t word = 0;
    ObjectPtr obj;
  };

  // We'll need to store usr functions too if any.
  // For now, let's assume they are handled by the host or in a table.
  struct UsrSlot {
    bool valid = false;
    ObjectPtr obj;
    int16_t argc = 0;
    int16_t pc = 0;
  };

  ObjectPtr findObject(uint32_t id12);

  Host &host_;
  std::vector<uint32_t> vars_;
  std::vector<Entry> stack_;
  std::vector<Line> lines_;
  UsrSlot usr_[256];
  std::map<uint32_t, ObjectPtr> known_;
  ObjectPtr cur_;
  uint32_t pc_ = 0;
  uint32_t acc_ = 0;
  uint32_t curFramePc_ = 0;
  uint32_t rng_ = 12345;
  bool blocked_ = false;
  bool ended_ = true;
};

} // namespace Pmail
