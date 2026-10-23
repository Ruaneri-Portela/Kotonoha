#include "SnowRadish/Vm.hpp"

#include <cctype>
#include <cstdlib>
#include <sstream>

namespace SnowRadish {

namespace {
uint32_t rd32(const std::vector<uint8_t>& d, size_t o) {
    return o + 4 <= d.size() ? d[o] | d[o + 1] << 8 | d[o + 2] << 16 | uint32_t(d[o + 3]) << 24 : 0;
}
uint16_t rd16(const std::vector<uint8_t>& d, size_t o) {
    return o + 2 <= d.size() ? uint16_t(d[o] | d[o + 1] << 8) : 0;
}
} // namespace

bool ScriptObject::parse(const std::string& objName, std::vector<uint8_t> bytes) {
    name = objName;
    data = std::move(bytes);
    if (data.size() < 12) return false;
    const uint32_t usrOffset = rd32(data, 0);
    const uint16_t n = rd16(data, 4);
    if (usrOffset + 4 > data.size()) return false;
    strings.clear();
    for (uint16_t i = 0; i < n; ++i) {
        const uint32_t off = rd32(data, 6 + 6 * i);
        const uint16_t sz = rd16(data, 10 + 6 * i);
        std::string s;
        for (uint32_t k = 0; k < sz && off + k < data.size() && data[off + k]; ++k) s.push_back(char(data[off + k]));
        strings.push_back(std::move(s));
    }
    const uint32_t usrSize = rd32(data, usrOffset);
    usr.clear();
    for (uint32_t i = 0; usrSize >= 4 && i < (usrSize - 4) / 8; ++i) {
        const size_t b = usrOffset + 4 + 8 * i;
        if (b + 8 > data.size()) break;
        usr.push_back({data[b], int16_t(rd16(data, b + 2)), int16_t(rd16(data, b + 4)), int16_t(rd16(data, b + 6))});
    }
    codeOffset = usrOffset + usrSize;
    codeWords = codeOffset <= data.size() ? (data.size() - codeOffset) / 4 : 0;
    id = rd16(data, data.size() - 4);
    return true;
}

uint32_t ScriptObject::word(size_t pc) const { return pc < codeWords ? rd32(data, codeOffset + pc * 4) : 0; }

const std::string& ScriptObject::str(size_t idx) const {
    static const std::string empty;
    return idx < strings.size() ? strings[idx] : empty;
}

Vm::Vm(Host& host) : host_(host), vars_(0x10000, 0) {}

void Vm::registerObject(const ObjectPtr& obj) {
    known_[obj->id & 0xfff] = obj;
    for (const auto& u : obj->usr) usr_[u.id] = {true, obj, u.argc, u.pc};
}

void Vm::unregisterObject(const ObjectPtr& obj) {
    for (auto& u : usr_)
        if (u.valid && u.obj == obj) u = {};
}

void Vm::start(const ObjectPtr& obj, uint32_t pc) {
    cur_ = obj;
    pc_ = pc;
    stack_.clear();
    lines_.clear();
    acc_ = 0;
    blocked_ = false;
    ended_ = false;
}

void Vm::jumpTo(const ObjectPtr& obj, uint32_t pc) {
    cur_ = obj;
    pc_ = pc;
}

void Vm::setVar(uint32_t id, uint32_t v) {
    switch ((id >> 12) & 0xf) {
    case 0: v &= 1; break;
    case 1: v = uint32_t(int32_t(int8_t(v))); break;
    case 2: v &= 0xff; break;
    case 3: v = uint32_t(int32_t(int16_t(v))); break;
    default: v &= 0xffff; break;
    }
    vars_[id & 0xffff] = v;
}

int32_t Vm::value(const Arg& a) const {
    if (a.tag() == 1) return int32_t(var(a.value()));
    return a.value();
}

ObjectPtr Vm::findObject(uint32_t id12) {
    if (cur_ && (cur_->id & 0xfff) == id12) return cur_;
    auto it = known_.find(id12);
    if (it != known_.end()) return it->second;
    ObjectPtr o = host_.loadById(id12);
    if (o) known_[id12] = o;
    return o;
}

std::string Vm::textAt(uint32_t object12, uint32_t idx) {
    ObjectPtr o = findObject(object12);
    return o ? o->str(idx) : std::string();
}

std::string Vm::text(const Arg& a) { return a.tag() == 2 ? textAt(a.object(), a.value()) : std::string(); }

uint32_t Vm::peek(size_t n) const { return n >= 1 && n <= stack_.size() ? stack_[stack_.size() - n].word : 0; }

int32_t Vm::evalExpr(const std::string& e) {
    std::vector<int32_t> st;
    std::istringstream in(e);
    std::string t;
    auto pop = [&]() {
        if (st.empty()) return int32_t(0);
        int32_t v = st.back();
        st.pop_back();
        return v;
    };
    while (in >> t) {
        if (t[0] == '$') {
            st.push_back(int32_t(var(uint32_t(std::atoi(t.c_str() + 1)))));
        } else if (uint8_t(t[0]) == 0xff && t.size() >= 3) {
            uint32_t v = peek(size_t(std::atoi(t.c_str() + 2))) & 0xffff;
            if (t[1] == 'F') v = var(v);
            st.push_back(int32_t(v));
        } else if (t == "@") {
            rng_ = rng_ * 1103515245u + 12345u;
            st.push_back(int32_t((rng_ >> 16) & 0x7fff));
        } else if (std::isdigit(uint8_t(t[0])) || (t[0] == '-' && t.size() > 1)) {
            st.push_back(std::atoi(t.c_str()));
        } else {
            const int32_t b = pop(), a = pop();
            const uint32_t ua = uint32_t(a), sh = uint32_t(b) & 31;
            int32_t r = 0;
            if (t == "*") r = a * b;
            else if (t == "/") r = b ? a / b : 0;
            else if (t == "%") r = b ? a % b : 0;
            else if (t == "+") r = a + b;
            else if (t == "-") r = a - b;
            else if (t == ">>") r = a >> sh;
            else if (t == "<<") r = int32_t(ua << sh);
            else if (t == ">>>") r = int32_t(sh ? (ua >> sh) | (ua << (32 - sh)) : ua);
            else if (t == "<<<") r = int32_t(sh ? (ua << sh) | (ua >> (32 - sh)) : ua);
            else if (t == ">") r = a > b;
            else if (t == "<") r = a < b;
            else if (t == ">=") r = a >= b;
            else if (t == "<=") r = a <= b;
            else if (t == "==") r = a == b;
            else if (t == "!=") r = a != b;
            else if (t == "&") r = a & b;
            else if (t == "^") r = a ^ b;
            else if (t == "|") r = a | b;
            else if (t == "&&") r = a && b;
            else if (t == "||") r = a || b;
            st.push_back(r);
        }
    }
    return st.empty() ? 0 : st.back();
}

bool Vm::step() {
    if (ended_ || !cur_) return false;
    const uint32_t w = cur_->word(pc_++);
    const uint32_t mode = (w >> 16) & 0xff, imm = w & 0xffff, op = w >> 24;
    uint32_t val = 0;
    int kind = 0; // 3 forward, 4 backward, 1 variable, 2 string, 5 raw word
    uint32_t argc = 0;
    switch (mode) {
    case 0: val = 0; break;
    case 1: val = 1; break;
    case 2: val = (w >> 8) & 0xff; break;
    case 3: val = imm; break;
    case 4: val = (w >> 8) & 0xff; argc = w & 0xff; break;
    case 5: val = uint32_t(evalExpr(cur_->str(imm))); kind = 5; break;
    case 0x10: val = imm; kind = 3; break;
    case 0x11: val = imm; kind = 4; break;
    case 0x20: val = imm; kind = 1; break;
    case 0x40: case 0x42: case 0x44: val = peek(imm); kind = 5; break;
    case 0x41: case 0x43: case 0x45: val = 0; kind = 5; break;
    case 0x80: val = imm; kind = 2; break;
    default: break;
    }
    const uint32_t v16 = val & 0xffff;
    const uint32_t acc16 = acc_ & 0xffff;
    const uint32_t objBits = uint32_t(cur_->id) << 20;

    auto jump = [&]() {
        if (kind == 3) pc_ += v16;
        if (kind == 4) pc_ -= v16;
    };

    switch (op) {
    case 0:
        pc_ = 0;
        acc_ = 0;
        ended_ = true;
        return false;
    case 1: jump(); break;
    case 2: if (acc16) jump(); break;
    case 3: if (!acc16) jump(); break;
    case 4: {
        if (stack_.empty()) {
            ended_ = true;
            return false;
        }
        Entry e = stack_.back();
        stack_.pop_back();
        if (((e.word >> 16) & 0xf) == 4) pc_ = e.word & 0xffff;
        if (e.obj) cur_ = e.obj;
        break;
    }
    case 5: {
        stack_.push_back({pc_ | 0x40000, cur_});
        curFramePc_ = pc_;
        std::vector<Arg> args;
        for (uint32_t i = 0; i < argc; ++i) {
            const size_t idx = stack_.size() - argc - 1 + i;
            args.push_back({idx < stack_.size() ? stack_[idx].word : 0});
        }
        host_.native(*this, int(val), args);
        if (!stack_.empty()) stack_.pop_back();
        break;
    }
    case 6: {
        UsrSlot& s = usr_[val & 0xff];
        if (!s.valid) {
            host_.log("usr " + std::to_string(val) + " missing");
            break;
        }
        stack_.push_back({pc_ | 0x40000, cur_});
        cur_ = s.obj;
        pc_ = uint32_t(s.pc);
        break;
    }
    case 9:
        if (((acc_ >> 16) & 0xf) == 2) {
            lines_.push_back({acc_ >> 20, acc16});
            if (lines_.size() > 128) lines_.erase(lines_.begin());
        }
        break;
    case 10: lines_.clear(); break;
    case 0x10:
        if (kind == 1) setVar(v16, acc16);
        break;
    case 0x11:
        switch (kind) {
        case 0: acc_ = v16 | objBits; break;
        case 1: acc_ = v16 | 0x10000 | objBits; break;
        case 2: acc_ = v16 | 0x20000 | objBits; break;
        case 5: acc_ = val; break;
        default: break;
        }
        break;
    case 0x20: stack_.push_back({acc_, nullptr}); break;
    case 0x21:
        for (uint32_t i = 0; i < v16 && !stack_.empty(); ++i) {
            acc_ = stack_.back().word;
            stack_.pop_back();
        }
        break;
    default: break;
    }
    return true;
}

size_t Vm::run(size_t maxSteps) {
    size_t n = 0;
    while (n < maxSteps && !blocked_ && step()) ++n;
    return n;
}

} // namespace SnowRadish
