#pragma once

#include "Ddlc/Value.hpp"

#include <memory>
#include <random>
#include <string>
#include <unordered_map>
#include <vector>

namespace Ddlc {

struct JumpSignal {
    std::string label;
    bool call = false;
};
struct QuitSignal {};

struct Scope {
    std::unordered_map<std::string, Value> vars;
    std::shared_ptr<Scope> parent;
};
using ScopeP = std::shared_ptr<Scope>;

class Interp {
public:
    Interp();
    ~Interp();
    Interp(const Interp&) = delete;
    Interp& operator=(const Interp&) = delete;

    Value eval(const std::string& source, const ScopeP& locals = nullptr);
    void exec(const std::string& source, const ScopeP& locals = nullptr);
    Value call(const Value& function, Args args, Kwargs kwargs = {});

    Value getAttr(const Value& object, const std::string& name);
    void setAttr(const Value& object, const std::string& name, const Value& value);
    Value getItem(const Value& object, const Value& key);
    void setItem(const Value& object, const Value& key, const Value& value);
    Value lookup(const std::string& name, const ScopeP& locals = nullptr) const;
    bool has(const std::string& name) const;
    void set(const std::string& name, const Value& value);
    void def(const std::string& name, NativeFn fn);
    void registerModule(const std::string& name, const Value& module);
    static Value nameSpace(const std::string& name);

    std::vector<Value> iterate(const Value& value);
    double random() { return std::uniform_real_distribution<double>(0.0, 1.0)(rng); }
    int64_t randint(int64_t a, int64_t b) {
        return std::uniform_int_distribution<int64_t>(a, b)(rng);
    }

    // Ren'Py compatibility modules; Python language builtins come from CPython.
    std::unordered_map<std::string, Value> builtins;
    std::mt19937_64 rng{12345};

private:
    friend struct PythonValue;
    friend bool pythonTruthy(const PythonValue&);
    friend std::string pythonString(const PythonValue&, bool);
    friend const Value* pythonAttribute(Object&, const std::string&);
    friend void pythonRefreshItems(Object&);
    friend void pythonRefreshDict(Object&);
    struct State;
    std::unique_ptr<State> state_;
};

std::vector<uint32_t> utf8Decode(const std::string& s);
std::string utf8Encode(const std::vector<uint32_t>& cps);
size_t utf8Length(const std::string& s);

// Script interactions may suspend inside a native Python callback. Releasing
// the GIL while the host handles the interaction lets it access Python too.
class PythonThreadRelease {
public:
    PythonThreadRelease();
    ~PythonThreadRelease();
    PythonThreadRelease(const PythonThreadRelease&) = delete;
    PythonThreadRelease& operator=(const PythonThreadRelease&) = delete;
private:
    void* state_ = nullptr;
};

} // namespace Ddlc
