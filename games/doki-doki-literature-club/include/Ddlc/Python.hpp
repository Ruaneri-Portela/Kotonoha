#pragma once
// A small Python 2 interpreter for the code embedded in Ren'Py scripts:
// expressions of statements, `python:` blocks and init code (assignments,
// if/for/while/try, def and simple classes).
#include "Ddlc/Value.hpp"

#include <map>
#include <memory>
#include <random>
#include <string>
#include <unordered_map>
#include <vector>

namespace Ddlc {

struct Ex;
struct St;
using ExP = std::shared_ptr<Ex>;
using StP = std::shared_ptr<St>;

// renpy.jump / renpy.call from Python code.
struct JumpSignal {
    std::string label;
    bool call = false;
};
struct QuitSignal {};

struct Scope {
    std::unordered_map<std::string, Value> vars;
    std::vector<std::string> globals;
    std::shared_ptr<Scope> parent; // enclosing function scope (closures)
};
using ScopeP = std::shared_ptr<Scope>;

class Interp {
public:
    Interp();

    std::unordered_map<std::string, Value> store; // the Ren'Py store (module globals)
    std::unordered_map<std::string, Value> builtins;

    Value eval(const std::string& src, const ScopeP& locals = nullptr);
    void exec(const std::string& src, const ScopeP& locals = nullptr);
    Value call(const Value& fn, Args args, Kwargs kwargs = {});

    Value getAttr(const Value& obj, const std::string& name);
    void setAttr(const Value& obj, const std::string& name, const Value& v);
    Value getItem(const Value& obj, const Value& key);
    void setItem(const Value& obj, const Value& key, const Value& v);

    Value lookup(const std::string& name, const ScopeP& scope = nullptr) const;
    bool has(const std::string& name) const { return store.count(name) || builtins.count(name); }
    void def(const std::string& name, NativeFn fn) { store[name] = makeFunction(name, std::move(fn)); }
    // A namespace object (module-like) whose missing attributes read as None.
    static Value nameSpace(const std::string& name);

    std::mt19937_64 rng{12345};
    double random() { return std::uniform_real_distribution<double>(0.0, 1.0)(rng); }
    int64_t randint(int64_t a, int64_t b) {
        return b < a ? a : std::uniform_int_distribution<int64_t>(a, b)(rng);
    }

    // Parsed code cache.
    ExP parseExpr(const std::string& src);
    std::vector<StP> parseCode(const std::string& src);

    // Evaluation internals (public for the executor helpers).
    Value evalEx(const Ex& e, const ScopeP& scope);
    enum class Flow { Normal, Break, Continue, Return };
    Flow execBlock(const std::vector<StP>& body, const ScopeP& scope);
    Value binary(const std::string& op, const Value& a, const Value& b);
    bool compare(const std::string& op, const Value& a, const Value& b);
    bool contains(const Value& container, const Value& item);
    void assign(const Ex& target, const Value& v, const ScopeP& scope);
    std::vector<Value> iterate(const Value& v);
    Value returnValue;

private:
    Flow execSt(const St& s, const ScopeP& scope);
    void setName(const std::string& name, const Value& v, const ScopeP& scope);
    Value makeUserFunction(const St& def, const ScopeP& scope);
    Value makeClass(const St& def, const ScopeP& scope);
    Value methodOf(const Value& self, const std::string& name);

    std::unordered_map<std::string, ExP> exprCache_;
    std::unordered_map<std::string, std::vector<StP>> codeCache_;
};

// UTF-8 helpers.
std::vector<uint32_t> utf8Decode(const std::string& s);
std::string utf8Encode(const std::vector<uint32_t>& cps);
size_t utf8Length(const std::string& s);

} // namespace Ddlc
