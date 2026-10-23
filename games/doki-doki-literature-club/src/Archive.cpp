#include "Ddlc/Archive.hpp"

#include <zlib.h>

#include <algorithm>
#include <cctype>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <unordered_map>

namespace Ddlc {

namespace {

std::string lower(std::string s) {
    for (char& c : s) c = char(std::tolower((unsigned char)c));
    return s;
}

class Unpickler {
public:
    Unpickler(const uint8_t* d, size_t n) : d_(d), n_(n) {}

    Value run() {
        while (p_ < n_) {
            const uint8_t op = d_[p_++];
            switch (op) {
            case 0x80: p_ += 1; break; // PROTO
            case '.': return pop();    // STOP
            case '(': marks_.push_back(stack_.size()); break;
            case '0': pop(); break;
            case '1': popMark(); break;
            case '2': stack_.push_back(stack_.back()); break;
            case 'N': stack_.emplace_back(); break;
            case 0x88: stack_.push_back(Value::boolean(true)); break;
            case 0x89: stack_.push_back(Value::boolean(false)); break;
            case 'J': stack_.push_back(Value::integer(int32_t(le(4)))); break;
            case 'K': stack_.push_back(Value::integer(int64_t(le(1)))); break;
            case 'M': stack_.push_back(Value::integer(int64_t(le(2)))); break;
            case 0x8a: stack_.push_back(Value::integer(longN(size_t(le(1))))); break;
            case 0x8b: stack_.push_back(Value::integer(longN(size_t(le(4))))); break;
            case 'I': {
                const std::string l = line();
                if (l == "00") stack_.push_back(Value::boolean(false));
                else if (l == "01") stack_.push_back(Value::boolean(true));
                else stack_.push_back(Value::integer(std::stoll(l)));
                break;
            }
            case 'L': {
                std::string l = line();
                if (!l.empty() && l.back() == 'L') l.pop_back();
                stack_.push_back(Value::integer(std::stoll(l)));
                break;
            }
            case 'G': {
                uint64_t bits = 0;
                need(8);
                for (int k = 0; k < 8; ++k) bits = bits << 8 | d_[p_ + size_t(k)];
                p_ += 8;
                double v;
                std::memcpy(&v, &bits, 8);
                stack_.push_back(Value::real(v));
                break;
            }
            case 'F': stack_.push_back(Value::real(std::stod(line()))); break;
            case 'U': stack_.push_back(Value::str(bytes(size_t(le(1))))); break;
            case 'T': stack_.push_back(Value::str(bytes(size_t(le(4))))); break;
            case 'X': stack_.push_back(Value::str(bytes(size_t(le(4))))); break;
            case 0x8c: stack_.push_back(Value::str(bytes(size_t(le(1))))); break;
            case 'S': {
                std::string l = line();
                if (l.size() >= 2) l = l.substr(1, l.size() - 2);
                stack_.push_back(Value::str(l));
                break;
            }
            case 'V': stack_.push_back(Value::str(line())); break;
            case ')': stack_.push_back(makeTuple()); break;
            case 't': stack_.push_back(makeTuple(popMark())); break;
            case 0x85: case 0x86: case 0x87: {
                const size_t k = size_t(op - 0x84);
                std::vector<Value> v(stack_.end() - long(k), stack_.end());
                stack_.resize(stack_.size() - k);
                stack_.push_back(makeTuple(std::move(v)));
                break;
            }
            case ']': stack_.push_back(makeList()); break;
            case 'l': stack_.push_back(makeList(popMark())); break;
            case 'a': {
                Value v = pop();
                listOf(stack_.back()).push_back(std::move(v));
                break;
            }
            case 'e': {
                auto v = popMark();
                auto& l = listOf(stack_.back());
                l.insert(l.end(), v.begin(), v.end());
                break;
            }
            case '}': stack_.push_back(makeDict()); break;
            case 'd': {
                auto v = popMark();
                Value dict = makeDict();
                for (size_t k = 0; k + 1 < v.size(); k += 2) dict.o->dict.emplace_back(v[k], v[k + 1]);
                stack_.push_back(dict);
                break;
            }
            case 's': {
                Value v = pop(), k = pop();
                setItem(stack_.back(), k, v);
                break;
            }
            case 'u': {
                auto v = popMark();
                for (size_t k = 0; k + 1 < v.size(); k += 2) setItem(stack_.back(), v[k], v[k + 1]);
                break;
            }
            case 'c': {
                const std::string mod = line(), name = line();
                auto o = makeObj(Object::K::Class, mod + "." + name);
                stack_.push_back(Value::object(o));
                break;
            }
            case 'R': {
                Value args = pop(), fn = pop();
                stack_.push_back(construct(fn.cls(), args.items(), true));
                break;
            }
            case 0x81: {
                Value args = pop(), fn = pop();
                stack_.push_back(construct(fn.cls(), args.items(), false));
                break;
            }
            case 'b': {
                Value state = pop();
                build(stack_.back(), state);
                break;
            }
            case 'q': memo_[uint32_t(le(1))] = stack_.back(); break;
            case 'r': memo_[uint32_t(le(4))] = stack_.back(); break;
            case 'p': memo_[uint32_t(std::stoul(line()))] = stack_.back(); break;
            case 'h': stack_.push_back(memo_.at(uint32_t(le(1)))); break;
            case 'j': stack_.push_back(memo_.at(uint32_t(le(4)))); break;
            case 'g': stack_.push_back(memo_.at(uint32_t(std::stoul(line())))); break;
            default: throw PyError("unpickle: unsupported opcode " + std::to_string(op));
            }
        }
        throw PyError("unpickle: truncated data");
    }

private:
    void need(size_t k) {
        if (p_ + k > n_) throw PyError("unpickle: truncated data");
    }
    uint64_t le(size_t k) {
        need(k);
        uint64_t v = 0;
        for (size_t j = 0; j < k; ++j) v |= uint64_t(d_[p_ + j]) << (8 * j);
        p_ += k;
        return v;
    }
    int64_t longN(size_t k) {
        need(k);
        int64_t v = 0;
        for (size_t j = 0; j < k && j < 8; ++j) v |= int64_t(d_[p_ + j]) << (8 * j);
        if (k > 0 && k < 8 && (d_[p_ + k - 1] & 0x80)) v -= int64_t(1) << (8 * k);
        p_ += k;
        return v;
    }
    std::string bytes(size_t k) {
        need(k);
        std::string s(reinterpret_cast<const char*>(d_ + p_), k);
        p_ += k;
        return s;
    }
    std::string line() {
        size_t e = p_;
        while (e < n_ && d_[e] != '\n') ++e;
        std::string s(reinterpret_cast<const char*>(d_ + p_), e - p_);
        p_ = e + 1;
        return s;
    }
    Value pop() {
        if (stack_.empty()) throw PyError("unpickle: stack underflow");
        Value v = std::move(stack_.back());
        stack_.pop_back();
        return v;
    }
    std::vector<Value> popMark() {
        if (marks_.empty()) throw PyError("unpickle: no mark");
        const size_t m = marks_.back();
        marks_.pop_back();
        std::vector<Value> v(stack_.begin() + long(m), stack_.end());
        stack_.resize(m);
        return v;
    }
    static std::vector<Value>& listOf(Value& v) {
        if (!v.isObj()) throw PyError("unpickle: append to non-list");
        return v.o->items;
    }
    static void setItem(Value& target, const Value& k, const Value& v) {
        if (!target.isObj()) throw PyError("unpickle: setitem on non-object");
        auto& o = *target.o;
        if (o.k == Object::K::Dict) {
            if (Value* slot = o.findKey(k)) *slot = v;
            else o.dict.emplace_back(k, v);
        } else {
            o.attrs[k.text()] = v;
        }
    }
    static Value construct(const std::string& cls, const std::vector<Value>& args, bool reduce) {
        const auto dot = cls.rfind('.');
        const std::string name = dot == std::string::npos ? cls : cls.substr(dot + 1);
        if (name == "PyExpr") return Value::str(args.empty() ? std::string() : args[0].text());
        if (name == "set" || name == "frozenset") return makeList(args.empty() ? std::vector<Value>{} : args[0].items());
        if (name == "OrderedDict" || name == "dict") {
            Value d = makeDict();
            if (!args.empty())
                for (const auto& pair : args[0].items())
                    if (pair.items().size() == 2) d.o->dict.emplace_back(pair.items()[0], pair.items()[1]);
            return d;
        }
        if (name == "list") return makeList(args.empty() ? std::vector<Value>{} : args[0].items());
        if (reduce && name == "_reconstructor" && !args.empty()) return makeInstance(args[0].cls());
        Value v = makeInstance(cls);
        v.o->items = args;
        return v;
    }
    static void mergeState(Object& o, const Value& part) {
        if (!part.isObj() || part.o->k != Object::K::Dict) return;
        for (const auto& [k, v] : part.o->dict) o.attrs[k.text()] = v;
    }
    static void build(Value& target, const Value& state) {
        if (!target.isObj()) return; // PyExpr strings carry no state worth keeping
        auto& o = *target.o;
        if (o.k == Object::K::Dict) {
            mergeState(o, state); // never happens with Ren'Py data
            return;
        }
        if (state.isObj() && state.o->k == Object::K::Tuple) {
            const auto& t = state.o->items;
            bool dicts = true;
            for (const auto& part : t)
                if (!part.isNone() && !(part.isObj() && part.o->k == Object::K::Dict)) dicts = false;
            if (dicts) {
                for (const auto& part : t) mergeState(o, part);
            } else if (t.size() >= 2) {
                // PyCode: (version, source, location, mode)
                o.attrs["source"] = t[1];
                if (t.size() >= 4) o.attrs["mode"] = t[3];
            }
            return;
        }
        mergeState(o, state);
    }

    const uint8_t* d_;
    size_t n_, p_ = 0;
    std::vector<Value> stack_;
    std::vector<size_t> marks_;
    std::unordered_map<uint32_t, Value> memo_;
};

} // namespace

Value unpickle(const uint8_t* data, size_t size) { return Unpickler(data, size).run(); }

bool inflate(const uint8_t* data, size_t size, std::vector<uint8_t>& out) {
    z_stream z{};
    if (inflateInit(&z) != Z_OK) return false;
    z.next_in = const_cast<Bytef*>(data);
    z.avail_in = uInt(size);
    out.clear();
    std::vector<uint8_t> buf(1 << 16);
    int r = Z_OK;
    while (r == Z_OK) {
        z.next_out = buf.data();
        z.avail_out = uInt(buf.size());
        r = ::inflate(&z, Z_NO_FLUSH);
        out.insert(out.end(), buf.data(), buf.data() + (buf.size() - z.avail_out));
    }
    inflateEnd(&z);
    return r == Z_STREAM_END;
}

Archive::~Archive() {
    if (file_) std::fclose(file_);
}

bool Archive::open(const std::string& path) {
    file_ = std::fopen(path.c_str(), "rb");
    if (!file_) return false;
    char header[64] = {};
    if (!std::fgets(header, sizeof header, file_)) return false;
    unsigned long long offset = 0;
    unsigned int key = 0;
    if (std::sscanf(header, "RPA-3.0 %llx %x", &offset, &key) != 2) return false;
    std::fseek(file_, 0, SEEK_END);
    const long end = std::ftell(file_);
    if (end < 0 || offset >= uint64_t(end)) return false;
    std::vector<uint8_t> packed(size_t(uint64_t(end) - offset)), raw;
    std::fseek(file_, long(offset), SEEK_SET);
    if (std::fread(packed.data(), 1, packed.size(), file_) != packed.size()) return false;
    if (!inflate(packed.data(), packed.size(), raw)) return false;
    const Value index = unpickle(raw);
    if (!index.isObj()) return false;
    for (const auto& [name, parts] : index.o->dict) {
        const auto& list = parts.items();
        if (list.empty()) continue;
        const auto& t = list[0].items();
        if (t.size() < 2) continue;
        Entry e;
        e.offset = uint64_t(t[0].integer() ^ int64_t(key));
        e.length = uint64_t(t[1].integer() ^ int64_t(key));
        if (t.size() > 2 && t[2].isStr()) e.prefix = t[2].s;
        index_[lower(name.text())] = e;
    }
    return true;
}

bool Archive::has(const std::string& name) const { return index_.count(lower(name)) != 0; }

std::vector<uint8_t> Archive::read(const std::string& name) const {
    auto it = index_.find(lower(name));
    if (it == index_.end() || !file_) return {};
    const Entry& e = it->second;
    std::vector<uint8_t> out(e.prefix.begin(), e.prefix.end());
    const size_t body = size_t(e.length) - std::min<size_t>(e.prefix.size(), size_t(e.length));
    const size_t at = out.size();
    out.resize(at + body);
    std::fseek(file_, long(e.offset), SEEK_SET);
    if (std::fread(out.data() + at, 1, body, file_) != body) return {};
    return out;
}

std::vector<std::string> Archive::names() const {
    std::vector<std::string> v;
    for (const auto& [k, _] : index_) v.push_back(k);
    return v;
}

bool Assets::open(const std::string& gameDir) {
    dir_ = gameDir;
    std::error_code ec;
    std::vector<std::filesystem::path> rpas;
    for (const auto& entry : std::filesystem::directory_iterator(gameDir, ec))
        if (lower(entry.path().extension().string()) == ".rpa") rpas.push_back(entry.path());
    std::sort(rpas.begin(), rpas.end());
    for (const auto& p : rpas) {
        auto a = std::make_unique<Archive>();
        if (a->open(p.string())) archives_.push_back(std::move(a));
    }
    return !archives_.empty();
}

bool Assets::has(const std::string& name) const {
    for (const auto& a : archives_)
        if (a->has(name)) return true;
    std::error_code ec;
    return std::filesystem::is_regular_file(std::filesystem::path(dir_) / name, ec);
}

std::vector<uint8_t> Assets::read(const std::string& name) const {
    for (const auto& a : archives_)
        if (a->has(name)) return a->read(name);
    std::ifstream f(std::filesystem::path(dir_) / name, std::ios::binary);
    if (!f) return {};
    return std::vector<uint8_t>((std::istreambuf_iterator<char>(f)), {});
}

std::string Assets::resolveImage(const std::string& name) const {
    if (has(name)) return name;
    if (has("images/" + name)) return "images/" + name;
    return {};
}

std::vector<std::string> Assets::names() const {
    std::vector<std::string> v;
    for (const auto& a : archives_) {
        auto n = a->names();
        v.insert(v.end(), n.begin(), n.end());
    }
    return v;
}

bool loadRpyc(const std::vector<uint8_t>& file, Value& out) {
    if (file.size() < 10 || std::memcmp(file.data(), "RENPY RPC2", 10) != 0) return false;
    size_t pos = 10;
    auto u32 = [&](size_t at) {
        return uint32_t(file[at]) | uint32_t(file[at + 1]) << 8 | uint32_t(file[at + 2]) << 16 |
               uint32_t(file[at + 3]) << 24;
    };
    while (pos + 12 <= file.size()) {
        const uint32_t slot = u32(pos), start = u32(pos + 4), len = u32(pos + 8);
        pos += 12;
        if (slot == 0) break;
        if (slot != 1 || size_t(start) + len > file.size()) continue;
        std::vector<uint8_t> raw;
        if (!inflate(file.data() + start, len, raw)) return false;
        out = unpickle(raw);
        return true;
    }
    return false;
}

} // namespace Ddlc
