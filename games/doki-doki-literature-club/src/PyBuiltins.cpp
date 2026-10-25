#include "PyAst.hpp"

#include <SDL3/SDL_timer.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstring>

namespace Ddlc {

namespace {

Value arg(Args& a, Kwargs& kw, size_t i, const char* name, Value def = {}) {
    for (auto& [k, v] : kw)
        if (k == name) return v;
    return i < a.size() ? a[i] : def;
}

Value bound(const std::string& name, std::function<Value(Args&, Kwargs&)> f) {
    return makeFunction(name, [f](Interp&, Args& a, Kwargs& k) { return f(a, k); });
}

std::string strip(const std::string& s, const std::string& chars) {
    const size_t b = s.find_first_not_of(chars);
    if (b == std::string::npos) return {};
    return s.substr(b, s.find_last_not_of(chars) - b + 1);
}

int order(Interp& in, const Value& a, const Value& b) {
    if (in.compare("<", a, b)) return -1;
    if (in.compare(">", a, b)) return 1;
    return 0;
}

Value strMethod(Interp& in, const Value& self, const std::string& name) {
    const std::string s = self.s;
    if (name == "replace")
        return bound(name, [s](Args& a, Kwargs&) {
            if (a.size() < 2 || !a[0].isStr() || a[0].s.empty()) return Value::str(s);
            std::string out = s;
            size_t pos = 0;
            while ((pos = out.find(a[0].s, pos)) != std::string::npos) {
                out.replace(pos, a[0].s.size(), a[1].text());
                pos += a[1].text().size();
            }
            return Value::str(out);
        });
    if (name == "lower" || name == "upper")
        return bound(name, [s, name](Args&, Kwargs&) {
            std::string out = s;
            for (char& c : out) c = char(name == "lower" ? std::tolower((unsigned char)c) : std::toupper((unsigned char)c));
            return Value::str(out);
        });
    if (name == "capitalize")
        return bound(name, [s](Args&, Kwargs&) {
            std::string out = s;
            for (char& c : out) c = char(std::tolower((unsigned char)c));
            if (!out.empty()) out[0] = char(std::toupper((unsigned char)out[0]));
            return Value::str(out);
        });
    if (name == "title")
        return bound(name, [s](Args&, Kwargs&) {
            std::string out = s;
            bool start = true;
            for (char& c : out) {
                c = char(start ? std::toupper((unsigned char)c) : std::tolower((unsigned char)c));
                start = !std::isalpha((unsigned char)c);
            }
            return Value::str(out);
        });
    if (name == "strip" || name == "lstrip" || name == "rstrip")
        return bound(name, [s, name](Args& a, Kwargs&) {
            const std::string chars = !a.empty() && a[0].isStr() ? a[0].s : std::string(" \t\r\n");
            if (name == "strip") return Value::str(strip(s, chars));
            if (name == "lstrip") {
                const size_t b = s.find_first_not_of(chars);
                return Value::str(b == std::string::npos ? "" : s.substr(b));
            }
            const size_t e = s.find_last_not_of(chars);
            return Value::str(e == std::string::npos ? "" : s.substr(0, e + 1));
        });
    if (name == "split")
        return bound(name, [s](Args& a, Kwargs&) {
            std::vector<Value> out;
            if (a.empty() || a[0].isNone()) {
                size_t i = 0;
                while (i < s.size()) {
                    while (i < s.size() && std::isspace((unsigned char)s[i])) ++i;
                    size_t j = i;
                    while (j < s.size() && !std::isspace((unsigned char)s[j])) ++j;
                    if (j > i) out.push_back(Value::str(s.substr(i, j - i)));
                    i = j;
                }
            } else {
                const std::string sep = a[0].s;
                size_t start = 0, pos;
                while (!sep.empty() && (pos = s.find(sep, start)) != std::string::npos) {
                    out.push_back(Value::str(s.substr(start, pos - start)));
                    start = pos + sep.size();
                }
                out.push_back(Value::str(s.substr(start)));
            }
            return makeList(out);
        });
    if (name == "join")
        return makeFunction(name, [s](Interp& in, Args& a, Kwargs&) {
            std::string out;
            bool first = true;
            for (auto& v : in.iterate(a.at(0))) {
                if (!first) out += s;
                out += v.text();
                first = false;
            }
            return Value::str(out);
        });
    if (name == "startswith" || name == "endswith")
        return bound(name, [s, name](Args& a, Kwargs&) {
            const std::string p = a.empty() ? std::string() : a[0].text();
            if (p.size() > s.size()) return Value::boolean(false);
            return Value::boolean(name == "startswith" ? s.compare(0, p.size(), p) == 0
                                                       : s.compare(s.size() - p.size(), p.size(), p) == 0);
        });
    if (name == "find" || name == "index")
        return bound(name, [s, name](Args& a, Kwargs&) {
            const size_t pos = s.find(a.empty() ? std::string() : a[0].text());
            if (pos == std::string::npos) {
                if (name == "index") throw PyError("ValueError: substring not found");
                return Value::integer(-1);
            }
            return Value::integer(int64_t(utf8Length(s.substr(0, pos))));
        });
    if (name == "count")
        return bound(name, [s](Args& a, Kwargs&) {
            const std::string p = a.empty() ? std::string() : a[0].text();
            int64_t n = 0;
            for (size_t pos = 0; !p.empty() && (pos = s.find(p, pos)) != std::string::npos; pos += p.size()) ++n;
            return Value::integer(n);
        });
    if (name == "format")
        return bound(name, [s](Args& a, Kwargs& kw) {
            std::string out;
            size_t auto_ = 0;
            for (size_t i = 0; i < s.size(); ++i) {
                if (s[i] == '{' && i + 1 < s.size() && s[i + 1] == '{') { out += '{'; ++i; continue; }
                if (s[i] == '}' && i + 1 < s.size() && s[i + 1] == '}') { out += '}'; ++i; continue; }
                if (s[i] != '{') { out += s[i]; continue; }
                const size_t e = s.find('}', i);
                std::string key = s.substr(i + 1, e - i - 1);
                key = key.substr(0, key.find(':'));
                Value v;
                if (key.empty()) v = auto_ < a.size() ? a[auto_++] : Value();
                else if (std::isdigit((unsigned char)key[0])) v = a.at(size_t(std::stoul(key)));
                else
                    for (auto& [k, x] : kw)
                        if (k == key) v = x;
                out += v.text();
                i = e;
            }
            return Value::str(out);
        });
    if (name == "encode" || name == "decode")
        return bound(name, [s](Args&, Kwargs&) { return Value::str(s); });
    if (name == "isdigit" || name == "isalpha")
        return bound(name, [s, name](Args&, Kwargs&) {
            bool ok = !s.empty();
            for (char c : s) ok &= name == "isdigit" ? std::isdigit((unsigned char)c) != 0 : std::isalpha((unsigned char)c) != 0;
            return Value::boolean(ok);
        });
    (void)in;
    throw PyError("AttributeError: str has no attribute " + name);
}

Value listMethod(Interp& in, const Value& self, const std::string& name) {
    ObjRef o = self.o;
    if (name == "append") return bound(name, [o](Args& a, Kwargs&) { o->items.push_back(a.at(0)); return Value(); });
    if (name == "extend")
        return makeFunction(name, [o](Interp& in, Args& a, Kwargs&) {
            for (auto& v : in.iterate(a.at(0))) o->items.push_back(v);
            return Value();
        });
    if (name == "insert")
        return bound(name, [o](Args& a, Kwargs&) {
            int64_t i = std::clamp<int64_t>(a.at(0).integer(), 0, int64_t(o->items.size()));
            o->items.insert(o->items.begin() + i, a.at(1));
            return Value();
        });
    if (name == "pop")
        return bound(name, [o](Args& a, Kwargs&) {
            if (o->items.empty()) throw PyError("IndexError: pop from empty list");
            int64_t i = a.empty() ? int64_t(o->items.size()) - 1 : a[0].integer();
            if (i < 0) i += int64_t(o->items.size());
            if (i < 0 || i >= int64_t(o->items.size())) throw PyError("IndexError: pop index");
            Value v = o->items[size_t(i)];
            o->items.erase(o->items.begin() + i);
            return v;
        });
    if (name == "remove")
        return bound(name, [o](Args& a, Kwargs&) {
            for (auto it = o->items.begin(); it != o->items.end(); ++it)
                if (*it == a.at(0)) {
                    o->items.erase(it);
                    return Value();
                }
            throw PyError("ValueError: list.remove(x): x not in list");
        });
    if (name == "index" || name == "count")
        return bound(name, [o, name](Args& a, Kwargs&) {
            int64_t n = 0;
            for (size_t i = 0; i < o->items.size(); ++i)
                if (o->items[i] == a.at(0)) {
                    if (name == "index") return Value::integer(int64_t(i));
                    ++n;
                }
            if (name == "index") throw PyError("ValueError: not in list");
            return Value::integer(n);
        });
    if (name == "reverse") return bound(name, [o](Args&, Kwargs&) { std::reverse(o->items.begin(), o->items.end()); return Value(); });
    if (name == "sort")
        return makeFunction(name, [o](Interp& in, Args&, Kwargs& kw) {
            Value key, reverse;
            for (auto& [k, v] : kw) {
                if (k == "key") key = v;
                if (k == "reverse") reverse = v;
            }
            std::stable_sort(o->items.begin(), o->items.end(), [&](const Value& a, const Value& b) {
                Value x = key.isNone() ? a : in.call(key, {a}), y = key.isNone() ? b : in.call(key, {b});
                return reverse.truthy() ? order(in, y, x) < 0 : order(in, x, y) < 0;
            });
            return Value();
        });
    (void)in;
    throw PyError("AttributeError: list has no attribute " + name);
}

Value dictMethod(Interp& in, const Value& self, const std::string& name) {
    ObjRef o = self.o;
    if (name == "get")
        return bound(name, [o](Args& a, Kwargs&) {
            Value* v = o->findKey(a.at(0));
            return v ? *v : a.size() > 1 ? a[1] : Value();
        });
    if (name == "has_key") return bound(name, [o](Args& a, Kwargs&) { return Value::boolean(o->findKey(a.at(0)) != nullptr); });
    if (name == "keys" || name == "values" || name == "items" || name == "iterkeys" || name == "itervalues" || name == "iteritems")
        return bound(name, [o, name](Args&, Kwargs&) {
            std::vector<Value> out;
            for (auto& [k, v] : o->dict) {
                if (name.find("keys") != std::string::npos) out.push_back(k);
                else if (name.find("values") != std::string::npos) out.push_back(v);
                else out.push_back(makeTuple({k, v}));
            }
            return makeList(out);
        });
    if (name == "setdefault")
        return bound(name, [o](Args& a, Kwargs&) {
            if (Value* v = o->findKey(a.at(0))) return *v;
            Value d = a.size() > 1 ? a[1] : Value();
            o->dict.emplace_back(a[0], d);
            return d;
        });
    if (name == "update")
        return bound(name, [o](Args& a, Kwargs& kw) {
            if (!a.empty() && a[0].isObj())
                for (auto& [k, v] : a[0].o->dict) {
                    if (Value* slot = o->findKey(k)) *slot = v;
                    else o->dict.emplace_back(k, v);
                }
            for (auto& [k, v] : kw) {
                if (Value* slot = o->findKey(Value::str(k))) *slot = v;
                else o->dict.emplace_back(Value::str(k), v);
            }
            return Value();
        });
    if (name == "pop")
        return bound(name, [o](Args& a, Kwargs&) {
            for (auto it = o->dict.begin(); it != o->dict.end(); ++it)
                if (it->first == a.at(0)) {
                    Value v = it->second;
                    o->dict.erase(it);
                    return v;
                }
            if (a.size() > 1) return a[1];
            throw PyError("KeyError");
        });
    if (name == "copy") return bound(name, [o](Args&, Kwargs&) { Value d = makeDict(); d.o->dict = o->dict; return d; });
    (void)in;
    throw PyError("AttributeError: dict has no attribute " + name);
}

Value toList(Interp& in, Args& a) { return makeList(a.empty() ? std::vector<Value>{} : in.iterate(a[0])); }

} // namespace

Value Interp::getAttr(const Value& obj, const std::string& name) {
    if (obj.isStr()) return strMethod(*this, obj, name);
    if (!obj.isObj()) throw PyError("AttributeError: " + obj.repr() + " has no attribute " + name);
    auto& o = *obj.o;
    switch (o.k) {
    case Object::K::List: return listMethod(*this, obj, name);
    case Object::K::Tuple:
        if (name == "index" || name == "count") return listMethod(*this, obj, name);
        break;
    case Object::K::Dict: return dictMethod(*this, obj, name);
    case Object::K::Instance: {
        auto it = o.attrs.find(name);
        if (it != o.attrs.end()) return it->second;
        if (o.cls == "super") return makeFunction(name, [](Interp&, Args&, Kwargs&) { return Value(); });
        Value m = methodOf(obj, name);
        if (!m.isNone()) return m;
        if (o.openAttrs) return {};
        break;
    }
    case Object::K::Class: {
        auto it = o.attrs.find(name);
        if (it != o.attrs.end()) return it->second;
        if (name == "__name__") return Value::str(o.cls);
        break;
    }
    case Object::K::Function: {
        auto it = o.attrs.find(name);
        if (it != o.attrs.end()) return it->second;
        break;
    }
    }
    throw PyError("AttributeError: " + o.cls + " has no attribute " + name);
}

void Interp::setAttr(const Value& obj, const std::string& name, const Value& v) {
    if (!obj.isObj()) throw PyError("AttributeError: cannot set " + name + " on " + obj.repr());
    obj.o->attrs[name] = v;
}

Value Interp::getItem(const Value& obj, const Value& key) {
    if (obj.isStr()) {
        auto cps = utf8Decode(obj.s);
        int64_t i = key.integer();
        if (i < 0) i += int64_t(cps.size());
        if (i < 0 || i >= int64_t(cps.size())) throw PyError("IndexError: string index out of range");
        return Value::str(utf8Encode({cps[size_t(i)]}));
    }
    if (!obj.isObj()) throw PyError("TypeError: object is not subscriptable: " + obj.repr());
    auto& o = *obj.o;
    if (o.k == Object::K::Dict) {
        if (Value* v = o.findKey(key)) return *v;
        throw PyError("KeyError: " + key.repr());
    }
    if (o.k == Object::K::List || o.k == Object::K::Tuple) {
        int64_t i = key.integer();
        if (i < 0) i += int64_t(o.items.size());
        if (i < 0 || i >= int64_t(o.items.size())) throw PyError("IndexError: list index out of range");
        return o.items[size_t(i)];
    }
    Value m = methodOf(obj, "__getitem__");
    if (!m.isNone()) return call(m, {key});
    throw PyError("TypeError: object is not subscriptable: " + o.cls);
}

void Interp::setItem(const Value& obj, const Value& key, const Value& v) {
    if (!obj.isObj()) throw PyError("TypeError: item assignment");
    auto& o = *obj.o;
    if (o.k == Object::K::Dict) {
        if (Value* slot = o.findKey(key)) *slot = v;
        else o.dict.emplace_back(key, v);
        return;
    }
    if (o.k == Object::K::List) {
        int64_t i = key.integer();
        if (i < 0) i += int64_t(o.items.size());
        if (i < 0 || i >= int64_t(o.items.size())) throw PyError("IndexError: list assignment index out of range");
        o.items[size_t(i)] = v;
        return;
    }
    throw PyError("TypeError: item assignment on " + o.cls);
}

Interp::Interp() {
    auto& b = builtins;
    auto fn = [&](const std::string& name, NativeFn f) { b[name] = makeFunction(name, std::move(f)); };
    fn("len", [](Interp&, Args& a, Kwargs&) {
        const Value& v = a.at(0);
        if (v.isStr()) return Value::integer(int64_t(utf8Length(v.s)));
        if (v.isObj()) return Value::integer(int64_t(v.o->k == Object::K::Dict ? v.o->dict.size() : v.o->items.size()));
        throw PyError("TypeError: len()");
    });
    fn("str", [](Interp&, Args& a, Kwargs&) { return Value::str(a.empty() ? std::string() : a[0].text()); });
    b["unicode"] = b["str"];
    fn("repr", [](Interp&, Args& a, Kwargs&) { return Value::str(a.at(0).repr()); });
    fn("int", [](Interp&, Args& a, Kwargs&) {
        if (a.empty()) return Value::integer(0);
        if (a[0].isStr()) return Value::integer(std::stoll(strip(a[0].s, " \t\r\n")));
        return Value::integer(a[0].t == Value::T::Float ? int64_t(std::trunc(a[0].f)) : a[0].integer());
    });
    b["long"] = b["int"];
    fn("float", [](Interp&, Args& a, Kwargs&) {
        if (a.empty()) return Value::real(0);
        if (a[0].isStr()) return Value::real(std::stod(a[0].s));
        return Value::real(a[0].num());
    });
    fn("bool", [](Interp&, Args& a, Kwargs&) { return Value::boolean(!a.empty() && a[0].truthy()); });
    fn("abs", [](Interp&, Args& a, Kwargs&) {
        const Value& v = a.at(0);
        return v.t == Value::T::Float ? Value::real(std::fabs(v.f)) : Value::integer(std::llabs(v.integer()));
    });
    fn("round", [](Interp&, Args& a, Kwargs&) {
        const double scale = std::pow(10.0, a.size() > 1 ? double(a[1].integer()) : 0.0);
        return Value::real(std::round(a.at(0).num() * scale) / scale);
    });
    auto minmax = [](bool max) {
        return [max](Interp& in, Args& a, Kwargs&) {
            std::vector<Value> items = a.size() == 1 ? in.iterate(a[0]) : a;
            if (items.empty()) throw PyError("ValueError: empty sequence");
            Value best = items[0];
            for (auto& v : items)
                if (max ? in.compare(">", v, best) : in.compare("<", v, best)) best = v;
            return best;
        };
    };
    fn("min", minmax(false));
    fn("max", minmax(true));
    fn("range", [](Interp&, Args& a, Kwargs&) {
        int64_t start = 0, stop = 0, step = 1;
        if (a.size() == 1) stop = a[0].integer();
        else if (a.size() >= 2) { start = a[0].integer(); stop = a[1].integer(); }
        if (a.size() >= 3) step = a[2].integer();
        if (step == 0) throw PyError("ValueError: range step");
        std::vector<Value> out;
        for (int64_t i = start; step > 0 ? i < stop : i > stop; i += step) out.push_back(Value::integer(i));
        return makeList(out);
    });
    b["xrange"] = b["range"];
    fn("list", [](Interp& in, Args& a, Kwargs&) { return toList(in, a); });
    fn("tuple", [](Interp& in, Args& a, Kwargs&) { return makeTuple(a.empty() ? std::vector<Value>{} : in.iterate(a[0])); });
    fn("set", [](Interp& in, Args& a, Kwargs&) { return toList(in, a); });
    fn("dict", [](Interp& in, Args& a, Kwargs& kw) {
        Value d = makeDict();
        if (!a.empty()) {
            if (a[0].isObj() && a[0].o->k == Object::K::Dict) d.o->dict = a[0].o->dict;
            else
                for (auto& pair : in.iterate(a[0])) {
                    auto kv = in.iterate(pair);
                    if (kv.size() == 2) in.setItem(d, kv[0], kv[1]);
                }
        }
        for (auto& [k, v] : kw) in.setItem(d, Value::str(k), v);
        return d;
    });
    fn("sorted", [](Interp& in, Args& a, Kwargs& kw) {
        Value l = toList(in, a);
        in.call(in.getAttr(l, "sort"), {}, kw);
        return l;
    });
    fn("reversed", [](Interp& in, Args& a, Kwargs&) {
        Value l = toList(in, a);
        std::reverse(l.o->items.begin(), l.o->items.end());
        return l;
    });
    fn("enumerate", [](Interp& in, Args& a, Kwargs&) {
        std::vector<Value> out;
        int64_t i = a.size() > 1 ? a[1].integer() : 0;
        for (auto& v : in.iterate(a.at(0))) out.push_back(makeTuple({Value::integer(i++), v}));
        return makeList(out);
    });
    fn("zip", [](Interp& in, Args& a, Kwargs&) {
        std::vector<std::vector<Value>> lists;
        size_t n = SIZE_MAX;
        for (auto& v : a) {
            lists.push_back(in.iterate(v));
            n = std::min(n, lists.back().size());
        }
        std::vector<Value> out;
        for (size_t i = 0; i < n && !lists.empty(); ++i) {
            std::vector<Value> t;
            for (auto& l : lists) t.push_back(l[i]);
            out.push_back(makeTuple(t));
        }
        return makeList(out);
    });
    fn("sum", [](Interp& in, Args& a, Kwargs&) {
        Value total = a.size() > 1 ? a[1] : Value::integer(0);
        for (auto& v : in.iterate(a.at(0))) total = in.binary("+", total, v);
        return total;
    });
    fn("any", [](Interp& in, Args& a, Kwargs&) {
        for (auto& v : in.iterate(a.at(0)))
            if (v.truthy()) return Value::boolean(true);
        return Value::boolean(false);
    });
    fn("all", [](Interp& in, Args& a, Kwargs&) {
        for (auto& v : in.iterate(a.at(0)))
            if (!v.truthy()) return Value::boolean(false);
        return Value::boolean(true);
    });
    fn("ord", [](Interp&, Args& a, Kwargs&) {
        auto cps = utf8Decode(a.at(0).text());
        return Value::integer(cps.empty() ? 0 : int64_t(cps[0]));
    });
    fn("chr", [](Interp&, Args& a, Kwargs&) { return Value::str(utf8Encode({uint32_t(a.at(0).integer())})); });
    b["unichr"] = b["chr"];
    fn("isinstance", [](Interp&, Args& a, Kwargs&) {
        const Value& v = a.at(0);
        const std::string t = a.at(1).cls();
        if (t == "str" || t == "unicode" || t == "basestring") return Value::boolean(v.isStr());
        if (t == "int" || t == "long") return Value::boolean(v.t == Value::T::Int);
        if (t == "float") return Value::boolean(v.t == Value::T::Float);
        if (t == "list") return Value::boolean(v.isObj() && v.o->k == Object::K::List);
        if (t == "tuple") return Value::boolean(v.isObj() && v.o->k == Object::K::Tuple);
        if (t == "dict") return Value::boolean(v.isObj() && v.o->k == Object::K::Dict);
        return Value::boolean(v.cls() == t);
    });
    fn("hasattr", [](Interp& in, Args& a, Kwargs&) {
        try {
            in.getAttr(a.at(0), a.at(1).text());
            return Value::boolean(a.at(0).attr(a.at(1).text()) != nullptr || !a.at(0).o->openAttrs);
        } catch (const PyError&) {
            return Value::boolean(false);
        }
    });
    fn("getattr", [](Interp& in, Args& a, Kwargs&) {
        try {
            return in.getAttr(a.at(0), a.at(1).text());
        } catch (const PyError&) {
            if (a.size() > 2) return a[2];
            throw;
        }
    });
    fn("setattr", [](Interp& in, Args& a, Kwargs&) {
        in.setAttr(a.at(0), a.at(1).text(), a.at(2));
        return Value();
    });
    fn("eval", [](Interp& in, Args& a, Kwargs&) { return in.eval(a.at(0).text()); });
    fn("exec", [](Interp& in, Args& a, Kwargs&) {
        in.exec(a.at(0).text());
        return Value();
    });
    fn("super", [](Interp&, Args&, Kwargs&) { return makeInstance("super"); });
    fn("callable", [](Interp&, Args& a, Kwargs&) { return Value::boolean(a.at(0).isObj() && a.at(0).o->fn != nullptr); });
    fn("type", [](Interp&, Args& a, Kwargs&) {
        Value c = Value::object(makeObj(Object::K::Class, a.at(0).isStr() ? "str" : a.at(0).cls()));
        return c;
    });
    fn("open", [](Interp&, Args&, Kwargs&) -> Value { throw PyError("IOError: file access is disabled"); });
    for (const char* cls : {"str", "unicode", "basestring", "int", "long", "float", "list", "tuple", "dict"}) {
        // Type objects for isinstance(); calling them keeps working through the functions above.
        Value conv = b[cls];
        auto o = makeObj(Object::K::Class, cls);
        if (conv.isObj()) o->fn = conv.o->fn;
        b[cls] = Value::object(o);
    }
    auto exc = [&](const char* name) {
        auto o = makeObj(Object::K::Class, name);
        o->fn = [name](Interp&, Args& a, Kwargs&) {
            Value e = makeInstance(name);
            e.o->attrs["message"] = a.empty() ? Value::str("") : a[0];
            return e;
        };
        b[name] = Value::object(o);
    };
    for (const char* e : {"Exception", "IOError", "OSError", "ValueError", "KeyError", "IndexError", "TypeError",
                          "AttributeError", "NameError"})
        exc(e);

    // math / random / datetime / time
    Value math = nameSpace("math");
    auto m1 = [&](const char* name, double (*f)(double)) {
        math.o->attrs[name] = makeFunction(name, [f](Interp&, Args& a, Kwargs&) { return Value::real(f(a.at(0).num())); });
    };
    m1("sin", std::sin);
    m1("cos", std::cos);
    m1("tan", std::tan);
    m1("sqrt", std::sqrt);
    m1("floor", std::floor);
    m1("ceil", std::ceil);
    m1("fabs", std::fabs);
    m1("exp", std::exp);
    m1("log", std::log);
    math.o->attrs["pow"] = makeFunction("pow", [](Interp&, Args& a, Kwargs&) { return Value::real(std::pow(a.at(0).num(), a.at(1).num())); });
    math.o->attrs["atan2"] = makeFunction("atan2", [](Interp&, Args& a, Kwargs&) { return Value::real(std::atan2(a.at(0).num(), a.at(1).num())); });
    math.o->attrs["pi"] = Value::real(std::acos(-1.0));
    b["math"] = math;

    Value random = nameSpace("random");
    random.o->attrs["random"] = makeFunction("random", [](Interp& in, Args&, Kwargs&) { return Value::real(in.random()); });
    random.o->attrs["randint"] = makeFunction("randint", [](Interp& in, Args& a, Kwargs&) {
        return Value::integer(in.randint(a.at(0).integer(), a.at(1).integer()));
    });
    random.o->attrs["uniform"] = makeFunction("uniform", [](Interp& in, Args& a, Kwargs&) {
        return Value::real(a.at(0).num() + (a.at(1).num() - a.at(0).num()) * in.random());
    });
    random.o->attrs["choice"] = makeFunction("choice", [](Interp& in, Args& a, Kwargs&) {
        auto items = in.iterate(a.at(0));
        if (items.empty()) throw PyError("IndexError: choice from empty sequence");
        return items[size_t(in.randint(0, int64_t(items.size()) - 1))];
    });
    random.o->attrs["shuffle"] = makeFunction("shuffle", [](Interp& in, Args& a, Kwargs&) {
        auto& items = a.at(0).o->items;
        for (size_t i = items.size(); i > 1; --i) std::swap(items[i - 1], items[size_t(in.randint(0, int64_t(i) - 1))]);
        return Value();
    });
    random.o->attrs["seed"] = makeFunction("seed", [](Interp& in, Args& a, Kwargs&) {
        in.rng.seed(a.empty() ? 0 : uint64_t(a[0].integer()));
        return Value();
    });
    b["random"] = random;

    Value datetime = nameSpace("datetime");
    Value dtClass = nameSpace("datetime");
    dtClass.o->attrs["now"] = makeFunction("now", [](Interp&, Args&, Kwargs&) {
        Value d = makeInstance("datetime");
        d.o->attrs["t"] = Value::real(double(SDL_GetTicks()) / 1000.0);
        return d;
    });
    datetime.o->attrs["datetime"] = dtClass;
    b["datetime"] = datetime;
    Value time = nameSpace("time");
    time.o->attrs["time"] = makeFunction("time", [](Interp&, Args&, Kwargs&) { return Value::real(double(SDL_GetTicks()) / 1000.0); });
    b["time"] = time;
    b["os"] = nameSpace("os");
    b["sys"] = nameSpace("sys");
    b["subprocess"] = nameSpace("subprocess");
}

} // namespace Ddlc
