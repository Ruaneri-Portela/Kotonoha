#include "Ddlc/Value.hpp"

#include <cmath>
#include <cstdio>

namespace Ddlc {

namespace {
const std::string kEmpty;
const std::vector<Value> kNoItems;
} // namespace

bool Value::truthy() const {
    switch (t) {
    case T::None: return false;
    case T::Bool: return b;
    case T::Int: return i != 0;
    case T::Float: return f != 0;
    case T::Str: return !s.empty();
    case T::Obj:
        if (o->k == Object::K::List || o->k == Object::K::Tuple) return !o->items.empty();
        if (o->k == Object::K::Dict) return !o->dict.empty();
        return true;
    }
    return false;
}

const std::string& Value::cls() const { return t == T::Obj ? o->cls : kEmpty; }

const Value* Value::attr(const std::string& name) const {
    if (t != T::Obj) return nullptr;
    auto it = o->attrs.find(name);
    return it == o->attrs.end() ? nullptr : &it->second;
}

const std::vector<Value>& Value::items() const {
    if (t == T::Obj && (o->k == Object::K::List || o->k == Object::K::Tuple)) return o->items;
    return kNoItems;
}

static std::string formatFloat(double v) {
    if (std::isfinite(v) && v == std::floor(v) && std::fabs(v) < 1e15) {
        char b[64];
        std::snprintf(b, sizeof b, "%.1f", v);
        return b;
    }
    char b[64];
    std::snprintf(b, sizeof b, "%.12g", v);
    return b;
}

std::string Value::text() const {
    switch (t) {
    case T::None: return "None";
    case T::Bool: return b ? "True" : "False";
    case T::Int: return std::to_string(i);
    case T::Float: return formatFloat(f);
    case T::Str: return s;
    case T::Obj: return repr();
    }
    return {};
}

std::string Value::repr() const {
    switch (t) {
    case T::Str: {
        std::string r = "'";
        for (char c : s) {
            if (c == '\'' || c == '\\') r += '\\';
            r += c;
        }
        return r + "'";
    }
    case T::Obj: {
        const auto& ob = *o;
        if (ob.k == Object::K::List || ob.k == Object::K::Tuple) {
            std::string r = ob.k == Object::K::List ? "[" : "(";
            for (size_t n = 0; n < ob.items.size(); ++n) r += (n ? ", " : "") + ob.items[n].repr();
            if (ob.k == Object::K::Tuple && ob.items.size() == 1) r += ",";
            return r + (ob.k == Object::K::List ? "]" : ")");
        }
        if (ob.k == Object::K::Dict) {
            std::string r = "{";
            for (size_t n = 0; n < ob.dict.size(); ++n)
                r += (n ? ", " : "") + ob.dict[n].first.repr() + ": " + ob.dict[n].second.repr();
            return r + "}";
        }
        return "<" + ob.cls + ">";
    }
    default: return text();
    }
}

bool operator==(const Value& a, const Value& b) {
    if (a.isNum() && b.isNum()) {
        if (a.t == Value::T::Float || b.t == Value::T::Float) return a.num() == b.num();
        return a.integer() == b.integer();
    }
    if (a.t != b.t) return false;
    switch (a.t) {
    case Value::T::None: return true;
    case Value::T::Str: return a.s == b.s;
    case Value::T::Obj: {
        if (a.o == b.o) return true;
        const auto &x = *a.o, &y = *b.o;
        if (x.k != y.k) return false;
        if (x.k == Object::K::List || x.k == Object::K::Tuple) {
            if (x.items.size() != y.items.size()) return false;
            for (size_t n = 0; n < x.items.size(); ++n)
                if (x.items[n] != y.items[n]) return false;
            return true;
        }
        return false;
    }
    default: return false;
    }
}

Value* Object::findKey(const Value& key) {
    for (auto& [k, v] : dict)
        if (k == key) return &v;
    return nullptr;
}

Value makeList(std::vector<Value> items) {
    auto o = makeObj(Object::K::List, "list");
    o->items = std::move(items);
    return Value::object(o);
}

Value makeTuple(std::vector<Value> items) {
    auto o = makeObj(Object::K::Tuple, "tuple");
    o->items = std::move(items);
    return Value::object(o);
}

Value makeDict() { return Value::object(makeObj(Object::K::Dict, "dict")); }

Value makeInstance(const std::string& cls) { return Value::object(makeObj(Object::K::Instance, cls)); }

Value makeFunction(const std::string& name, NativeFn fn) {
    auto o = makeObj(Object::K::Function, name);
    o->fn = std::move(fn);
    return Value::object(o);
}

} // namespace Ddlc
