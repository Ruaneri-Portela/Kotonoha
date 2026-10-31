#pragma once
// Dynamic values shared by the pickle loader and the embedded Python bridge.
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace Ddlc {

struct Object;
struct PythonValue;
using ObjRef = std::shared_ptr<Object>;

struct Value {
    enum class T : uint8_t { None, Bool, Int, Float, Str, Obj };
    T t = T::None;
    bool b = false;
    int64_t i = 0;
    double f = 0;
    std::string s;
    ObjRef o;

    Value() = default;
    static Value boolean(bool v) { Value r; r.t = T::Bool; r.b = v; return r; }
    static Value integer(int64_t v) { Value r; r.t = T::Int; r.i = v; return r; }
    static Value real(double v) { Value r; r.t = T::Float; r.f = v; return r; }
    static Value str(std::string v) { Value r; r.t = T::Str; r.s = std::move(v); return r; }
    static Value object(ObjRef v) { Value r; r.t = v ? T::Obj : T::None; r.o = std::move(v); return r; }

    bool isNone() const { return t == T::None; }
    bool isStr() const { return t == T::Str; }
    bool isNum() const { return t == T::Int || t == T::Float || t == T::Bool; }
    bool isObj() const { return t == T::Obj; }
    double num() const { return t == T::Float ? f : t == T::Int ? double(i) : t == T::Bool ? (b ? 1 : 0) : 0; }
    int64_t integer() const { return t == T::Int ? i : t == T::Float ? int64_t(f) : t == T::Bool ? (b ? 1 : 0) : 0; }
    bool truthy() const;
    // Instance class name, or "" for anything else.
    const std::string& cls() const;
    // Instance attribute (nullptr when missing or not an object).
    const Value* attr(const std::string& name) const;
    Value get(const std::string& name) const { const Value* v = attr(name); return v ? *v : Value(); }
    std::string text() const; // str(): strings as is
    std::string repr() const;
    // Sequence items for lists and tuples, empty otherwise.
    const std::vector<Value>& items() const;
};

bool operator==(const Value& a, const Value& b);
inline bool operator!=(const Value& a, const Value& b) { return !(a == b); }

class Interp;
using Args = std::vector<Value>;
using Kwargs = std::vector<std::pair<std::string, Value>>;
using NativeFn = std::function<Value(Interp&, Args&, Kwargs&)>;

struct Object {
    enum class K : uint8_t { List, Tuple, Dict, Instance, Function, Class } k = K::Instance;
    std::string cls;                         // instance / class name ("renpy.ast.Say", "Poem", ...)
    std::vector<Value> items;                // list / tuple items, reduce arguments
    std::vector<std::pair<Value, Value>> dict;
    std::map<std::string, Value> attrs;      // instance attributes
    NativeFn fn;                             // functions and callable classes
    std::shared_ptr<void> payload;           // C++ data (displayables, transforms...)
    std::shared_ptr<PythonValue> python;      // an object owned by CPython
    bool openAttrs = false;                  // missing attributes read as None (persistent)

    Value* findKey(const Value& key);
};

inline ObjRef makeObj(Object::K k, std::string cls = {}) {
    auto o = std::make_shared<Object>();
    o->k = k;
    o->cls = std::move(cls);
    return o;
}
Value makeList(std::vector<Value> items = {});
Value makeTuple(std::vector<Value> items = {});
Value makeDict();
Value makeInstance(const std::string& cls);
Value makeFunction(const std::string& name, NativeFn fn);

struct PyError : std::runtime_error {
    using std::runtime_error::runtime_error;
};

} // namespace Ddlc
