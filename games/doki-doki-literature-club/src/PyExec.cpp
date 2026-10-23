#include "PyAst.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <functional>
#include <map>

namespace Ddlc {

std::vector<uint32_t> utf8Decode(const std::string& s) {
    std::vector<uint32_t> out;
    for (size_t i = 0; i < s.size();) {
        const uint8_t c = uint8_t(s[i]);
        uint32_t cp = c;
        size_t len = 1;
        if (c >= 0xF0) { cp = c & 0x07; len = 4; }
        else if (c >= 0xE0) { cp = c & 0x0F; len = 3; }
        else if (c >= 0xC0) { cp = c & 0x1F; len = 2; }
        for (size_t k = 1; k < len && i + k < s.size(); ++k) cp = cp << 6 | (uint8_t(s[i + k]) & 0x3F);
        out.push_back(cp);
        i += len;
    }
    return out;
}

std::string utf8Encode(const std::vector<uint32_t>& cps) {
    std::string out;
    for (uint32_t cp : cps) {
        if (cp < 0x80) out += char(cp);
        else if (cp < 0x800) { out += char(0xC0 | cp >> 6); out += char(0x80 | (cp & 0x3F)); }
        else if (cp < 0x10000) {
            out += char(0xE0 | cp >> 12); out += char(0x80 | (cp >> 6 & 0x3F)); out += char(0x80 | (cp & 0x3F));
        } else {
            out += char(0xF0 | cp >> 18); out += char(0x80 | (cp >> 12 & 0x3F));
            out += char(0x80 | (cp >> 6 & 0x3F)); out += char(0x80 | (cp & 0x3F));
        }
    }
    return out;
}

size_t utf8Length(const std::string& s) {
    size_t n = 0;
    for (char c : s)
        if ((uint8_t(c) & 0xC0) != 0x80) ++n;
    return n;
}

Value Interp::nameSpace(const std::string& name) {
    Value v = makeInstance(name);
    v.o->openAttrs = true;
    return v;
}

ExP Interp::parseExpr(const std::string& src) {
    auto it = exprCache_.find(src);
    if (it != exprCache_.end()) return it->second;
    ExP e = parseExpression(src);
    exprCache_[src] = e;
    return e;
}

std::vector<StP> Interp::parseCode(const std::string& src) {
    auto it = codeCache_.find(src);
    if (it != codeCache_.end()) return it->second;
    auto code = parseProgram(src);
    codeCache_[src] = code;
    return code;
}

Value Interp::eval(const std::string& src, const ScopeP& locals) { return evalEx(*parseExpr(src), locals); }

void Interp::exec(const std::string& src, const ScopeP& locals) { execBlock(parseCode(src), locals); }

Value Interp::lookup(const std::string& name, const ScopeP& scope) const {
    for (Scope* s = scope.get(); s; s = s->parent.get()) {
        auto it = s->vars.find(name);
        if (it != s->vars.end()) return it->second;
    }
    auto it = store.find(name);
    if (it != store.end()) return it->second;
    auto b = builtins.find(name);
    if (b != builtins.end()) return b->second;
    throw PyError("NameError: " + name);
}

void Interp::setName(const std::string& name, const Value& v, const ScopeP& scope) {
    if (scope && std::find(scope->globals.begin(), scope->globals.end(), name) == scope->globals.end())
        scope->vars[name] = v;
    else
        store[name] = v;
}

std::vector<Value> Interp::iterate(const Value& v) {
    if (v.isStr()) {
        std::vector<Value> out;
        for (uint32_t cp : utf8Decode(v.s)) out.push_back(Value::str(utf8Encode({cp})));
        return out;
    }
    if (v.isObj()) {
        if (v.o->k == Object::K::List || v.o->k == Object::K::Tuple) return v.o->items;
        if (v.o->k == Object::K::Dict) {
            std::vector<Value> out;
            for (const auto& kv : v.o->dict) out.push_back(kv.first);
            return out;
        }
        if (const Value* lines = v.attr("__lines__")) return iterate(*lines);
    }
    throw PyError("TypeError: object is not iterable: " + v.repr());
}

void Interp::assign(const Ex& target, const Value& v, const ScopeP& scope) {
    switch (target.k) {
    case Ex::K::Name: setName(target.s, v, scope); return;
    case Ex::K::Attr: setAttr(evalEx(*target.a[0], scope), target.s, v); return;
    case Ex::K::Sub: setItem(evalEx(*target.a[0], scope), evalEx(*target.a[1], scope), v); return;
    case Ex::K::Tuple:
    case Ex::K::List: {
        auto items = iterate(v);
        if (items.size() != target.a.size()) throw PyError("ValueError: unpack mismatch");
        for (size_t i = 0; i < items.size(); ++i) assign(*target.a[i], items[i], scope);
        return;
    }
    default: throw PyError("SyntaxError: can't assign");
    }
}

static int64_t floorDiv(int64_t a, int64_t b) {
    int64_t q = a / b;
    if ((a % b != 0) && ((a < 0) != (b < 0))) --q;
    return q;
}

static std::string percentFormat(Interp& in, const std::string& fmt, const Value& args) {
    std::vector<Value> list = args.isObj() && args.o->k == Object::K::Tuple ? args.o->items : std::vector<Value>{args};
    std::string out;
    size_t ai = 0;
    for (size_t i = 0; i < fmt.size(); ++i) {
        if (fmt[i] != '%' || i + 1 >= fmt.size()) { out += fmt[i]; continue; }
        size_t j = i + 1;
        std::string spec = "%";
        while (j < fmt.size() && std::strchr("-+ #0123456789.", fmt[j])) spec += fmt[j++];
        if (j >= fmt.size()) break;
        const char conv = fmt[j];
        i = j;
        if (conv == '%') { out += '%'; continue; }
        const Value a = ai < list.size() ? list[ai++] : Value();
        char buf[256];
        if (conv == 'd' || conv == 'i') std::snprintf(buf, sizeof buf, (spec + "lld").c_str(), (long long)a.integer());
        else if (conv == 'f' || conv == 'g' || conv == 'e') std::snprintf(buf, sizeof buf, (spec + conv).c_str(), a.num());
        else if (conv == 'r') { out += a.repr(); continue; }
        else { out += a.text(); continue; }
        out += buf;
    }
    (void)in;
    return out;
}

Value Interp::binary(const std::string& op, const Value& a, const Value& b) {
    if (a.isNum() && b.isNum()) {
        const bool real = a.t == Value::T::Float || b.t == Value::T::Float;
        if (op == "+") return real ? Value::real(a.num() + b.num()) : Value::integer(a.integer() + b.integer());
        if (op == "-") return real ? Value::real(a.num() - b.num()) : Value::integer(a.integer() - b.integer());
        if (op == "*") return real ? Value::real(a.num() * b.num()) : Value::integer(a.integer() * b.integer());
        if (op == "/" || op == "//") {
            if (b.num() == 0) throw PyError("ZeroDivisionError");
            if (!real) return Value::integer(floorDiv(a.integer(), b.integer()));
            return Value::real(op == "/" ? a.num() / b.num() : std::floor(a.num() / b.num()));
        }
        if (op == "%") {
            if (b.num() == 0) throw PyError("ZeroDivisionError");
            if (!real) {
                int64_t r = a.integer() % b.integer();
                if (r != 0 && ((r < 0) != (b.integer() < 0))) r += b.integer();
                return Value::integer(r);
            }
            double r = std::fmod(a.num(), b.num());
            if (r != 0 && ((r < 0) != (b.num() < 0))) r += b.num();
            return Value::real(r);
        }
        if (op == "**") {
            if (!real && b.integer() >= 0) {
                int64_t r = 1;
                for (int64_t k = 0; k < b.integer(); ++k) r *= a.integer();
                return Value::integer(r);
            }
            return Value::real(std::pow(a.num(), b.num()));
        }
        if (op == "&") return Value::integer(a.integer() & b.integer());
        if (op == "|") return Value::integer(a.integer() | b.integer());
        if (op == "^") return Value::integer(a.integer() ^ b.integer());
        if (op == "<<") return Value::integer(a.integer() << b.integer());
        if (op == ">>") return Value::integer(a.integer() >> b.integer());
    }
    if (op == "+" && a.isStr() && b.isStr()) return Value::str(a.s + b.s);
    if (op == "%" && a.isStr()) return Value::str(percentFormat(*this, a.s, b));
    if (op == "*" && (a.isStr() || b.isStr()) && (a.isNum() || b.isNum())) {
        const std::string& s = a.isStr() ? a.s : b.s;
        std::string r;
        for (int64_t k = 0, n = a.isStr() ? b.integer() : a.integer(); k < n; ++k) r += s;
        return Value::str(r);
    }
    if (a.isObj() && b.isObj() && (a.o->k == Object::K::List || a.o->k == Object::K::Tuple) && op == "+") {
        std::vector<Value> items = a.o->items;
        items.insert(items.end(), b.o->items.begin(), b.o->items.end());
        return a.o->k == Object::K::List ? makeList(items) : makeTuple(items);
    }
    if (op == "*" && a.isObj() && a.o->k == Object::K::List && b.isNum()) {
        std::vector<Value> items;
        for (int64_t k = 0; k < b.integer(); ++k) items.insert(items.end(), a.o->items.begin(), a.o->items.end());
        return makeList(items);
    }
    if (op == "-" && a.cls() == "datetime" && b.cls() == "datetime") {
        Value d = makeInstance("timedelta");
        const double secs = a.get("t").num() - b.get("t").num();
        d.o->attrs["t"] = Value::real(secs);
        d.o->attrs["total_seconds"] = makeFunction("total_seconds", [secs](Interp&, Args&, Kwargs&) { return Value::real(secs); });
        return d;
    }
    // Operator overloading on user classes.
    static const std::map<std::string, std::string> dunder = {{"+", "__add__"}, {"-", "__sub__"}, {"*", "__mul__"},
                                                              {"/", "__div__"}, {"%", "__mod__"}};
    if (a.isObj() && a.o->k == Object::K::Instance) {
        auto it = dunder.find(op);
        if (it != dunder.end()) {
            Value m = methodOf(a, it->second);
            if (!m.isNone()) return call(m, {b});
        }
    }
    throw PyError("TypeError: unsupported operand " + op + " for " + a.repr() + " and " + b.repr());
}

bool Interp::contains(const Value& container, const Value& item) {
    if (container.isStr()) return item.isStr() && container.s.find(item.s) != std::string::npos;
    if (container.isObj()) {
        const auto& o = *container.o;
        if (o.k == Object::K::Dict) {
            for (const auto& kv : o.dict)
                if (kv.first == item) return true;
            return false;
        }
        for (const auto& v : o.items)
            if (v == item) return true;
        return false;
    }
    throw PyError("TypeError: argument is not iterable");
}

static int order(const Value& a, const Value& b) {
    if (a.isNum() && b.isNum()) return a.num() < b.num() ? -1 : a.num() > b.num() ? 1 : 0;
    if (a.isStr() && b.isStr()) return a.s < b.s ? -1 : a.s > b.s ? 1 : 0;
    if (a.isNone() && b.isNone()) return 0;
    // Python 2: None is smaller than everything, numbers smaller than other types.
    if (a.isNone()) return -1;
    if (b.isNone()) return 1;
    if (a.isNum()) return -1;
    if (b.isNum()) return 1;
    const auto& x = a.items();
    const auto& y = b.items();
    for (size_t i = 0; i < x.size() && i < y.size(); ++i)
        if (int c = order(x[i], y[i])) return c;
    return x.size() < y.size() ? -1 : x.size() > y.size() ? 1 : 0;
}

bool Interp::compare(const std::string& op, const Value& a, const Value& b) {
    if (op == "==") return a == b;
    if (op == "!=") return a != b;
    if (op == "in") return contains(b, a);
    if (op == "not in") return !contains(b, a);
    if (op == "is" || op == "is not") {
        bool same = a.t == b.t && (a.isObj() ? a.o == b.o : a == b);
        return op == "is" ? same : !same;
    }
    const int c = order(a, b);
    if (op == "<") return c < 0;
    if (op == ">") return c > 0;
    if (op == "<=") return c <= 0;
    if (op == ">=") return c >= 0;
    throw PyError("bad comparison " + op);
}

static int64_t normIndex(int64_t i, size_t n) {
    if (i < 0) i += int64_t(n);
    return i;
}

Value Interp::evalEx(const Ex& e, const ScopeP& scope) {
    switch (e.k) {
    case Ex::K::Const: return e.v;
    case Ex::K::Name: return lookup(e.s, scope);
    case Ex::K::Attr: return getAttr(evalEx(*e.a[0], scope), e.s);
    case Ex::K::Sub: {
        Value obj = evalEx(*e.a[0], scope);
        const Ex& idx = *e.a[1];
        if (idx.k == Ex::K::Slice) {
            auto bound = [&](const ExP& p, int64_t def) { return p ? evalEx(*p, scope).integer() : def; };
            if (obj.isStr()) {
                auto cps = utf8Decode(obj.s);
                const int64_t n = int64_t(cps.size());
                int64_t lo = std::clamp(normIndex(bound(idx.a[0], 0), cps.size()), int64_t(0), n);
                int64_t hi = std::clamp(normIndex(bound(idx.a[1], n), cps.size()), int64_t(0), n);
                if (hi < lo) hi = lo;
                return Value::str(utf8Encode(std::vector<uint32_t>(cps.begin() + lo, cps.begin() + hi)));
            }
            const auto& items = obj.items();
            const int64_t n = int64_t(items.size());
            int64_t lo = std::clamp(normIndex(bound(idx.a[0], 0), items.size()), int64_t(0), n);
            int64_t hi = std::clamp(normIndex(bound(idx.a[1], n), items.size()), int64_t(0), n);
            if (hi < lo) hi = lo;
            std::vector<Value> part(items.begin() + lo, items.begin() + hi);
            return obj.isObj() && obj.o->k == Object::K::Tuple ? makeTuple(part) : makeList(part);
        }
        return getItem(obj, evalEx(idx, scope));
    }
    case Ex::K::Call: {
        Value fn = evalEx(*e.a[0], scope);
        Args args;
        for (size_t i = 1; i < e.a.size(); ++i) args.push_back(evalEx(*e.a[i], scope));
        if (e.star)
            for (auto& v : iterate(evalEx(*e.star, scope))) args.push_back(v);
        Kwargs kw;
        for (const auto& [k, x] : e.kw) kw.emplace_back(k, evalEx(*x, scope));
        if (e.dstar) {
            Value d = evalEx(*e.dstar, scope);
            if (d.isObj())
                for (const auto& [k, v] : d.o->dict) kw.emplace_back(k.text(), v);
        }
        return call(fn, std::move(args), std::move(kw));
    }
    case Ex::K::Bin: return binary(e.s, evalEx(*e.a[0], scope), evalEx(*e.a[1], scope));
    case Ex::K::Unary: {
        Value v = evalEx(*e.a[0], scope);
        if (e.s == "-") return v.t == Value::T::Float ? Value::real(-v.f) : Value::integer(-v.integer());
        if (e.s == "~") return Value::integer(~v.integer());
        return v;
    }
    case Ex::K::Not: return Value::boolean(!evalEx(*e.a[0], scope).truthy());
    case Ex::K::And: {
        Value a = evalEx(*e.a[0], scope);
        return a.truthy() ? evalEx(*e.a[1], scope) : a;
    }
    case Ex::K::Or: {
        Value a = evalEx(*e.a[0], scope);
        return a.truthy() ? a : evalEx(*e.a[1], scope);
    }
    case Ex::K::Cmp: {
        Value left = evalEx(*e.a[0], scope);
        for (size_t i = 0; i < e.ops.size(); ++i) {
            Value right = evalEx(*e.a[i + 1], scope);
            if (!compare(e.ops[i], left, right)) return Value::boolean(false);
            left = right;
        }
        return Value::boolean(true);
    }
    case Ex::K::IfExp: return evalEx(*e.cond, scope).truthy() ? evalEx(*e.a[0], scope) : evalEx(*e.a[1], scope);
    case Ex::K::List:
    case Ex::K::Tuple: {
        std::vector<Value> items;
        for (const auto& x : e.a) items.push_back(evalEx(*x, scope));
        return e.k == Ex::K::List ? makeList(items) : makeTuple(items);
    }
    case Ex::K::Dict: {
        Value d = makeDict();
        for (size_t i = 0; i + 1 < e.a.size(); i += 2) setItem(d, evalEx(*e.a[i], scope), evalEx(*e.a[i + 1], scope));
        return d;
    }
    case Ex::K::ListComp: {
        auto inner = std::make_shared<Scope>();
        inner->parent = scope;
        std::vector<Value> out;
        for (const auto& item : iterate(evalEx(*e.iter, scope))) {
            assign(*e.target, item, inner);
            if (e.cond && !evalEx(*e.cond, inner).truthy()) continue;
            out.push_back(evalEx(*e.a[0], inner));
        }
        return makeList(out);
    }
    case Ex::K::Lambda: {
        auto def = std::make_shared<St>();
        def->k = St::K::Def;
        def->name = "<lambda>";
        def->params = e.kw;
        auto ret = std::make_shared<St>();
        ret->k = St::K::Return;
        ret->value = e.a[0];
        def->body = {ret};
        return makeUserFunction(*def, scope);
    }
    case Ex::K::Slice: throw PyError("bad slice");
    }
    return {};
}

Value Interp::makeUserFunction(const St& defIn, const ScopeP& scope) {
    auto def = std::make_shared<St>(defIn);
    std::vector<Value> defaults;
    for (const auto& [n, d] : def->params) defaults.push_back(d ? evalEx(*d, scope) : Value());
    Value fn = makeFunction(def->name, [def, defaults, scope](Interp& in, Args& args, Kwargs& kw) {
        auto local = std::make_shared<Scope>();
        local->parent = scope;
        size_t i = 0;
        for (; i < def->params.size(); ++i) {
            const std::string& name = def->params[i].first;
            if (i < args.size()) {
                local->vars[name] = args[i];
                continue;
            }
            bool found = false;
            for (auto& [k, v] : kw)
                if (k == name) { local->vars[name] = v; found = true; }
            if (!found) {
                if (!def->params[i].second) throw PyError("TypeError: " + def->name + "() missing argument " + name);
                local->vars[name] = defaults[i];
            }
        }
        if (!def->varargs.empty()) {
            std::vector<Value> rest;
            for (size_t k = def->params.size(); k < args.size(); ++k) rest.push_back(args[k]);
            local->vars[def->varargs] = makeTuple(rest);
        }
        if (!def->kwargs.empty()) {
            Value d = makeDict();
            for (auto& [k, v] : kw) {
                bool named = false;
                for (const auto& p : def->params)
                    if (p.first == k) named = true;
                if (!named) d.o->dict.emplace_back(Value::str(k), v);
            }
            local->vars[def->kwargs] = d;
        }
        // Collect `global` declarations up front, as Python does at compile time.
        std::function<void(const std::vector<StP>&)> globals = [&](const std::vector<StP>& body) {
            for (const auto& st : body) {
                if (st->k == St::K::Global) local->globals.insert(local->globals.end(), st->names.begin(), st->names.end());
                if (st->k == St::K::Def || st->k == St::K::Class) continue;
                globals(st->body);
                globals(st->orelse);
                for (const auto& b : st->bodies) globals(b);
            }
        };
        globals(def->body);
        Value result;
        if (in.execBlock(def->body, local) == Interp::Flow::Return) result = in.returnValue;
        return result;
    });
    fn.o->attrs["__user__"] = Value::boolean(true);
    return fn;
}

Value Interp::methodOf(const Value& self, const std::string& name) {
    const Value* cls = self.attr("__class__");
    std::vector<Value> todo;
    if (cls) todo.push_back(*cls);
    while (!todo.empty()) {
        Value c = todo.front();
        todo.erase(todo.begin());
        if (!c.isObj()) continue;
        auto it = c.o->attrs.find(name);
        if (it != c.o->attrs.end()) {
            Value fn = it->second;
            if (fn.isObj() && fn.o->k == Object::K::Function && fn.attr("__user__")) {
                Value self_ = self;
                return makeFunction(name, [fn, self_](Interp& in, Args& args, Kwargs& kw) {
                    Args full{self_};
                    full.insert(full.end(), args.begin(), args.end());
                    return in.call(fn, full, kw);
                });
            }
            return fn;
        }
        if (const Value* bases = c.attr("__bases__"))
            for (const auto& b : bases->items()) todo.push_back(b);
    }
    return {};
}

Value Interp::makeClass(const St& def, const ScopeP& scope) {
    auto cls = makeObj(Object::K::Class, def.name);
    std::vector<Value> bases;
    for (const auto& b : def.bases) {
        try {
            bases.push_back(evalEx(*b, scope));
        } catch (const PyError&) {
        }
    }
    cls->attrs["__bases__"] = makeTuple(bases);
    auto body = std::make_shared<Scope>();
    body->parent = scope;
    execBlock(def.body, body);
    for (auto& [k, v] : body->vars) cls->attrs[k] = v;
    Value clsValue = Value::object(cls);
    std::weak_ptr<Object> weak = cls;
    cls->fn = [weak](Interp& in, Args& args, Kwargs& kw) {
        auto c = weak.lock();
        if (!c) throw PyError("class gone");
        Value inst = makeInstance(c->cls);
        inst.o->attrs["__class__"] = Value::object(c);
        Value init = in.methodOf(inst, "__init__");
        if (!init.isNone()) in.call(init, args, kw);
        return inst;
    };
    return clsValue;
}

Value Interp::call(const Value& fn, Args args, Kwargs kwargs) {
    if (!fn.isObj() || !fn.o->fn) throw PyError("TypeError: object is not callable: " + fn.repr());
    return fn.o->fn(*this, args, kwargs);
}

Interp::Flow Interp::execBlock(const std::vector<StP>& body, const ScopeP& scope) {
    for (const auto& s : body) {
        Flow f = execSt(*s, scope);
        if (f != Flow::Normal) return f;
    }
    return Flow::Normal;
}

Interp::Flow Interp::execSt(const St& s, const ScopeP& scope) {
    switch (s.k) {
    case St::K::Expr: evalEx(*s.value, scope); return Flow::Normal;
    case St::K::Assign: {
        Value v = evalEx(*s.value, scope);
        for (const auto& t : s.targets) assign(*t, v, scope);
        return Flow::Normal;
    }
    case St::K::Aug: {
        const Ex& t = *s.targets[0];
        Value cur = evalEx(t, scope);
        Value rhs = evalEx(*s.value, scope);
        if (s.op == "+" && cur.isObj() && cur.o->k == Object::K::List) {
            for (auto& v : iterate(rhs)) cur.o->items.push_back(v);
            return Flow::Normal;
        }
        assign(t, binary(s.op, cur, rhs), scope);
        return Flow::Normal;
    }
    case St::K::If:
        for (size_t i = 0; i < s.conds.size(); ++i)
            if (evalEx(*s.conds[i], scope).truthy()) return execBlock(s.bodies[i], scope);
        return execBlock(s.orelse, scope);
    case St::K::While: {
        int guard = 0;
        while (evalEx(*s.conds[0], scope).truthy()) {
            if (++guard > 1000000) throw PyError("while loop limit");
            Flow f = execBlock(s.body, scope);
            if (f == Flow::Break) return Flow::Normal;
            if (f == Flow::Return) return f;
        }
        return execBlock(s.orelse, scope);
    }
    case St::K::For: {
        for (const auto& item : iterate(evalEx(*s.value, scope))) {
            assign(*s.targets[0], item, scope);
            Flow f = execBlock(s.body, scope);
            if (f == Flow::Break) return Flow::Normal;
            if (f == Flow::Return) return f;
        }
        return execBlock(s.orelse, scope);
    }
    case St::K::Try: {
        Flow f = Flow::Normal;
        bool failed = false;
        std::string message;
        try {
            f = execBlock(s.body, scope);
        } catch (const PyError& err) {
            failed = true;
            message = err.what();
        }
        if (failed) {
            if (s.bodies.empty()) {
                execBlock(s.finally, scope);
                throw PyError(message);
            }
            if (!s.names[0].empty()) setName(s.names[0], Value::str(message), scope);
            f = execBlock(s.bodies[0], scope);
        } else if (f == Flow::Normal) {
            f = execBlock(s.orelse, scope);
        }
        Flow fin = execBlock(s.finally, scope);
        return fin != Flow::Normal ? fin : f;
    }
    case St::K::Def: setName(s.name, makeUserFunction(s, scope), scope); return Flow::Normal;
    case St::K::Class: setName(s.name, makeClass(s, scope), scope); return Flow::Normal;
    case St::K::Return:
        returnValue = s.value ? evalEx(*s.value, scope) : Value();
        return Flow::Return;
    case St::K::Pass: return Flow::Normal;
    case St::K::Break: return Flow::Break;
    case St::K::Continue: return Flow::Continue;
    case St::K::Global:
        if (scope) scope->globals.insert(scope->globals.end(), s.names.begin(), s.names.end());
        return Flow::Normal;
    case St::K::Del: return Flow::Normal;
    case St::K::Import: {
        // Modules are provided natively; unknown ones become empty namespaces.
        size_t start = 0;
        for (const auto& as : s.names) {
            const size_t end = s.varargs.find(';', start);
            const std::string full = s.varargs.substr(start, end - start);
            start = end + 1;
            const std::string last = full.substr(full.rfind('.') + 1);
            Value v;
            if (builtins.count(last)) v = builtins[last];
            else if (store.count(last)) v = store[last];
            else v = nameSpace(last);
            if (!scope || !scope->vars.count(as)) setName(as, v, scope);
        }
        return Flow::Normal;
    }
    case St::K::Raise: throw PyError(s.value ? evalEx(*s.value, scope).text() : "raise");
    case St::K::With: {
        Value v = evalEx(*s.value, scope);
        if (!s.targets.empty()) assign(*s.targets[0], v, scope);
        return execBlock(s.body, scope);
    }
    }
    return Flow::Normal;
}

} // namespace Ddlc
