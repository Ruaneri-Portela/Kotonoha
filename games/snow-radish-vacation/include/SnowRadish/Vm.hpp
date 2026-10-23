#pragma once
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace SnowRadish {

// Compiled script object (*.obj in SCRIPT.DAT):
//   u32 usrOffset; u16 nStrings; nStrings x {u32 off; u16 size}; strings;
//   at usrOffset: u32 usrSize, (usrSize-4)/8 x {u8 id; u8; s16 argc; s16 pc; s16 end};
//   code words follow the usr table; the last 4 bytes hold {u16 objectId; u16}.
struct ScriptObject {
    std::string name;
    std::vector<uint8_t> data;
    uint16_t id = 0;
    std::vector<std::string> strings;
    uint32_t codeOffset = 0;
    struct Usr {
        uint8_t id;
        int16_t argc, pc, end;
    };
    std::vector<Usr> usr;
    size_t codeWords = 0;

    bool parse(const std::string& objName, std::vector<uint8_t> bytes);
    uint32_t word(size_t pc) const;
    // Strings keep the original Shift-JIS bytes.
    const std::string& str(size_t idx) const;
};
using ObjectPtr = std::shared_ptr<ScriptObject>;

// A VM word that crosses the native boundary: value | tag << 16 | object << 20.
// tag 0 = integer, 1 = variable reference, 2 = string reference, 4 = frame.
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
    // Native ("sys") call. The host may call Vm::block() to pause execution.
    virtual void native(Vm& vm, int id, const std::vector<Arg>& args) = 0;
    // Resolves an object by script name (e.g. "start.obj") or by 12-bit id.
    virtual ObjectPtr loadByName(const std::string& name) = 0;
    virtual ObjectPtr loadById(uint32_t id12) = 0;
    virtual void log(const std::string&) {}
};

class Vm {
public:
    explicit Vm(Host& host);

    // Registers the usr functions of an object (sub/lib/main slots).
    void registerObject(const ObjectPtr& obj);
    void unregisterObject(const ObjectPtr& obj);
    // Makes the object the running one, restarting at pc 0.
    void start(const ObjectPtr& obj, uint32_t pc = 0);

    // Executes one instruction. Returns false once the script ended.
    bool step();
    // Runs until the VM ends, blocks, or maxSteps instructions were executed.
    size_t run(size_t maxSteps);

    void block() { blocked_ = true; }
    void unblock() { blocked_ = false; }
    bool blocked() const { return blocked_; }
    bool ended() const { return ended_; }

    uint32_t var(uint32_t id) const { return id < vars_.size() ? vars_[id] : 0; }
    void setVar(uint32_t id, uint32_t v);
    // Value of an argument: variable references are dereferenced.
    int32_t value(const Arg& a) const;
    // String of a string-reference argument ("" when not a string).
    std::string text(const Arg& a);
    // String from (object, index); object is the 12-bit id.
    std::string textAt(uint32_t object12, uint32_t idx);

    // Text lines queued with ARG, consumed by the display native.
    struct Line {
        uint32_t object;
        uint32_t index;
    };
    std::vector<Line>& lines() { return lines_; }

    ObjectPtr current() const { return cur_; }
    uint32_t pc() const { return pc_; }
    // Frame helpers for call/return style natives: the most recent frame is the
    // native call itself, so its pc is the instruction following the call.
    uint32_t returnPc() const { return curFramePc_; }

    // Replaces the running script, restoring a pc (used by script-level call/return).
    void jumpTo(const ObjectPtr& obj, uint32_t pc);

    size_t stackDepth() const { return stack_.size(); }

private:
    struct Entry {
        uint32_t word = 0;
        ObjectPtr obj;
    };
    struct UsrSlot {
        bool valid = false;
        ObjectPtr obj;
        int16_t argc = 0;
        int16_t pc = 0;
    };

    ObjectPtr findObject(uint32_t id12);
    int32_t evalExpr(const std::string& e);
    uint32_t peek(size_t n) const;

    Host& host_;
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

} // namespace SnowRadish
