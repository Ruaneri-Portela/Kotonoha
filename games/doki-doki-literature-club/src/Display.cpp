#include "Ddlc/Runtime.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace Ddlc {

uint32_t parseColor(const std::string& s) {
    std::string h = s.size() && s[0] == '#' ? s.substr(1) : s;
    auto hex = [](char c) -> uint32_t {
        if (c >= '0' && c <= '9') return uint32_t(c - '0');
        if (c >= 'a' && c <= 'f') return uint32_t(c - 'a' + 10);
        if (c >= 'A' && c <= 'F') return uint32_t(c - 'A' + 10);
        return 0;
    };
    uint32_t r = 0, g = 0, b = 0, a = 255;
    if (h.size() == 3 || h.size() == 4) {
        r = hex(h[0]) * 17, g = hex(h[1]) * 17, b = hex(h[2]) * 17;
        if (h.size() == 4) a = hex(h[3]) * 17;
    } else if (h.size() >= 6) {
        r = hex(h[0]) * 16 + hex(h[1]), g = hex(h[2]) * 16 + hex(h[3]), b = hex(h[4]) * 16 + hex(h[5]);
        if (h.size() >= 8) a = hex(h[6]) * 16 + hex(h[7]);
    }
    return r << 24 | g << 16 | b << 8 | a;
}

void Backend::log(const std::string& line) { std::fprintf(stderr, "[ddlc] %s\n", line.c_str()); }

namespace {

struct Aff {
    double a = 1, b = 0, c = 0, d = 0, e = 1, f = 0;
};
Aff mul(const Aff& p, const Aff& q) {
    return {p.a * q.a + p.b * q.d, p.a * q.b + p.b * q.e, p.a * q.c + p.b * q.f + p.c,
            p.d * q.a + p.e * q.d, p.d * q.b + p.e * q.e, p.d * q.c + p.e * q.f + p.f};
}
void transformOp(DrawOp& op, const Aff& t) {
    Aff m{op.m[0], op.m[1], op.m[2], op.m[3], op.m[4], op.m[5]};
    m = mul(t, m);
    op.m[0] = float(m.a), op.m[1] = float(m.b), op.m[2] = float(m.c);
    op.m[3] = float(m.d), op.m[4] = float(m.e), op.m[5] = float(m.f);
}

// ---- Displayables ---------------------------------------------------------

struct ImageDisp : Disp {
    std::string file;
    explicit ImageDisp(std::string f) : file(std::move(f)) {}
    Render render(Vm& vm, double, double) override {
        Render r;
        int w = 0, h = 0;
        if (!vm.backend.imageSize(file, w, h)) return r;
        r.w = w, r.h = h;
        DrawOp op;
        op.image = file;
        op.w = float(w), op.h = float(h);
        r.ops.push_back(op);
        return r;
    }
};

struct SolidDisp : Disp {
    uint32_t color;
    explicit SolidDisp(uint32_t c) : color(c) {}
    Render render(Vm&, double, double) override {
        Render r;
        r.w = kScreenW, r.h = kScreenH;
        DrawOp op;
        op.k = DrawOp::K::Solid;
        op.color = color;
        op.w = float(r.w), op.h = float(r.h);
        r.ops.push_back(op);
        return r;
    }
};

struct NullDisp : Disp {
    double w = 0, h = 0;
    Render render(Vm&, double, double) override {
        Render r;
        r.w = w, r.h = h;
        return r;
    }
};

// An image name used before its definition (init order): resolved at render.
struct LazyDisp : Disp {
    std::vector<std::string> name;
    DispP target;
    bool tried = false;
    Render render(Vm& vm, double st, double at) override {
        if (!tried) {
            tried = true;
            target = vm.imageNamed(name);
            if (target) target = target->instance();
            else {
                std::string key;
                for (auto& n : name) key += (key.empty() ? "" : " ") + n;
                vm.backend.log("unknown image: " + key);
            }
        }
        return target ? target->render(vm, st, at) : Render{};
    }
    DispP instance() override {
        auto l = std::make_shared<LazyDisp>();
        l->name = name;
        return l;
    }
};

struct TextDisp : Disp {
    std::string text;
    TextStyle style;
    Pos xpos, ypos, xanchor, yanchor;
    Render render(Vm& vm, double, double) override {
        Render r;
        float w = 0, h = 0;
        vm.backend.textSize(text, style, w, h);
        r.w = w, r.h = h;
        DrawOp op;
        op.k = DrawOp::K::Text;
        op.text = text;
        op.style = style;
        op.w = w, op.h = h;
        r.ops.push_back(op);
        r.xpos = xpos, r.ypos = ypos, r.xanchor = xanchor, r.yanchor = yanchor;
        return r;
    }
};

struct CompositeDisp : Disp {
    double w = 0, h = 0;
    struct Part {
        double x, y;
        DispP d;
    };
    std::vector<Part> parts;
    bool live = false; // LiveComposite children keep their own placement anchors
    DispP instance() override {
        auto c = std::make_shared<CompositeDisp>(*this);
        for (auto& p : c->parts)
            if (p.d) p.d = p.d->instance();
        return c;
    }
    Render render(Vm& vm, double st, double at) override {
        Render r;
        r.w = w, r.h = h;
        for (auto& p : parts) {
            if (!p.d) continue;
            Render c = p.d->render(vm, st, at);
            double x = p.x, y = p.y;
            if (live) {
                x += -c.xanchor.at(c.w) + c.xoffset;
                y += -c.yanchor.at(c.h) + c.yoffset;
            }
            r.blit(c, x, y);
        }
        return r;
    }
};

// Children placed in the screen area by their own placement (Fixed / contains).
struct FixedDisp : Disp {
    std::vector<DispP> children;
    DispP instance() override {
        auto c = std::make_shared<FixedDisp>(*this);
        for (auto& d : c->children) d = d->instance();
        return c;
    }
    Render render(Vm& vm, double st, double at) override {
        Render r;
        r.w = kScreenW, r.h = kScreenH;
        for (auto& d : children) {
            Render c = d->render(vm, st, at);
            r.blit(c, c.xpos.at(r.w) - c.xanchor.at(c.w) + c.xoffset, c.ypos.at(r.h) - c.yanchor.at(c.h) + c.yoffset);
        }
        return r;
    }
};

struct TileDisp : Disp {
    DispP child;
    DispP instance() override {
        auto c = std::make_shared<TileDisp>(*this);
        if (c->child) c->child = c->child->instance();
        return c;
    }
    Render render(Vm& vm, double st, double at) override {
        Render r;
        r.w = kScreenW, r.h = kScreenH;
        if (!child) return r;
        Render c = child->render(vm, st, at);
        if (c.w < 1 || c.h < 1) return r;
        for (double y = 0; y < r.h; y += c.h)
            for (double x = 0; x < r.w; x += c.w) r.blit(c, x, y);
        return r;
    }
};

struct SwitchDisp : Disp {
    std::vector<std::pair<std::string, DispP>> cases;
    DispP instance() override {
        auto c = std::make_shared<SwitchDisp>(*this);
        for (auto& [k, d] : c->cases)
            if (d) d = d->instance();
        return c;
    }
    Render render(Vm& vm, double st, double at) override {
        for (auto& [cond, d] : cases) {
            bool ok = false;
            try {
                ok = vm.py.eval(cond).truthy();
            } catch (const PyError&) {
            }
            if (ok && d) return d->render(vm, st, at);
        }
        return {};
    }
};

// ---- ATL --------------------------------------------------------------------

struct AState {
    virtual ~AState() = default;
};
enum class Act { Continue, Next, Repeat };
struct Res {
    Act a = Act::Continue;
    double left = 0;
    int count = 0;
};
struct ACtx {
    Vm& vm;
    TransformDisp& tr;
    double st; // the transform's own time
};
struct AStmt {
    virtual ~AStmt() = default;
    virtual Res exec(ACtx& c, double t, std::unique_ptr<AState>& s) = 0;
};
using AStmtP = std::shared_ptr<AStmt>;

constexpr double kPi = 3.14159265358979323846;

double bounceOut(double t) {
    const double n = 7.5625, d = 2.75;
    if (t < 1 / d) return n * t * t;
    if (t < 2 / d) { t -= 1.5 / d; return n * t * t + .75; }
    if (t < 2.5 / d) { t -= 2.25 / d; return n * t * t + .9375; }
    t -= 2.625 / d;
    return n * t * t + .984375;
}

// Ren'Py warpers; easein_X / easeout_X are easings.net's easeOutX / easeInX.
double warp(const std::string& w, double t) {
    if (w == "linear" || w.empty()) return t;
    if (w == "pause") return t >= 1 ? 1 : 0;
    if (w == "ease") return .5 - std::cos(kPi * t) / 2;
    if (w == "easein") return std::cos((1 - t) * kPi / 2);
    if (w == "easeout") return 1 - std::cos(t * kPi / 2);
    auto us = w.find('_');
    if (us == std::string::npos) return t;
    const std::string kind = w.substr(0, us), curve = w.substr(us + 1);
    auto in = [&](double x) -> double { // easings.net easeIn
        if (curve == "quad") return x * x;
        if (curve == "cubic") return x * x * x;
        if (curve == "quart") return x * x * x * x;
        if (curve == "quint") return x * x * x * x * x;
        if (curve == "sine") return 1 - std::cos(x * kPi / 2);
        if (curve == "expo") return x == 0 ? 0 : std::pow(2, 10 * x - 10);
        if (curve == "circ") return 1 - std::sqrt(std::max(0.0, 1 - x * x));
        if (curve == "back") return 2.70158 * x * x * x - 1.70158 * x * x;
        if (curve == "elastic")
            return x == 0 || x == 1 ? x : -std::pow(2, 10 * x - 10) * std::sin((x * 10 - 10.75) * (2 * kPi / 3));
        if (curve == "bounce") return 1 - bounceOut(1 - x);
        return x;
    };
    auto out = [&](double x) { return 1 - in(1 - x); };
    if (kind == "easein") return out(t);
    if (kind == "easeout") return in(t);
    if (kind == "ease") return t < .5 ? in(2 * t) / 2 : 1 - in(2 - 2 * t) / 2;
    return t;
}

void lerpPos(Pos& dst, const Pos& b, const Pos& target, double w) {
    if (!b.set) {
        dst = target;
        return;
    }
    dst.px = b.px + (target.px - b.px) * w;
    dst.fr = b.fr + (target.fr - b.fr) * w;
    dst.set = true;
}

void applyProp(TransformState& cur, const TransformState& beg, const std::string& name, const Value& v, double w) {
    auto lerp = [w](double b, double target) { return b + (target - b) * w; };
    auto pair = [&](int i) { return v.items().size() > size_t(i) ? v.items()[size_t(i)] : Value(); };
    if (name == "xpos") lerpPos(cur.xpos, beg.xpos, Pos::of(v), w);
    else if (name == "ypos") lerpPos(cur.ypos, beg.ypos, Pos::of(v), w);
    else if (name == "xanchor") lerpPos(cur.xanchor, beg.xanchor, Pos::of(v), w);
    else if (name == "yanchor") lerpPos(cur.yanchor, beg.yanchor, Pos::of(v), w);
    else if (name == "pos") {
        lerpPos(cur.xpos, beg.xpos, Pos::of(pair(0)), w);
        lerpPos(cur.ypos, beg.ypos, Pos::of(pair(1)), w);
    } else if (name == "anchor") {
        lerpPos(cur.xanchor, beg.xanchor, Pos::of(pair(0)), w);
        lerpPos(cur.yanchor, beg.yanchor, Pos::of(pair(1)), w);
    } else if (name == "xcenter") {
        lerpPos(cur.xpos, beg.xpos, Pos::of(v), w);
        cur.xanchor = Pos::frac(.5);
    } else if (name == "ycenter") {
        lerpPos(cur.ypos, beg.ypos, Pos::of(v), w);
        cur.yanchor = Pos::frac(.5);
    } else if (name == "xalign") {
        lerpPos(cur.xpos, beg.xpos, Pos::frac(v.num()), w);
        lerpPos(cur.xanchor, beg.xanchor, Pos::frac(v.num()), w);
    } else if (name == "yalign") {
        lerpPos(cur.ypos, beg.ypos, Pos::frac(v.num()), w);
        lerpPos(cur.yanchor, beg.yanchor, Pos::frac(v.num()), w);
    } else if (name == "align") {
        applyProp(cur, beg, "xalign", pair(0), w);
        applyProp(cur, beg, "yalign", pair(1), w);
    } else if (name == "xoffset") cur.xoffset = lerp(beg.xoffset, v.num());
    else if (name == "yoffset") cur.yoffset = lerp(beg.yoffset, v.num());
    else if (name == "offset") {
        cur.xoffset = lerp(beg.xoffset, pair(0).num());
        cur.yoffset = lerp(beg.yoffset, pair(1).num());
    } else if (name == "zoom") cur.zoom = lerp(beg.zoom, v.num());
    else if (name == "xzoom") cur.xzoom = lerp(beg.xzoom, v.num());
    else if (name == "yzoom") cur.yzoom = lerp(beg.yzoom, v.num());
    else if (name == "alpha") cur.alpha = lerp(beg.alpha, v.num());
    else if (name == "rotate") {
        cur.rotate = v.isNone() ? 0 : lerp(beg.rotateSet ? beg.rotate : 0, v.num());
        cur.rotateSet = !v.isNone();
    } else if (name == "xtile") cur.xtile = std::max<int>(1, int(v.integer()));
    else if (name == "ytile") cur.ytile = std::max<int>(1, int(v.integer()));
    else if (name == "crop") {
        if (v.isNone()) cur.cropSet = false;
        else {
            for (int i = 0; i < 4; ++i) cur.crop[i] = lerp(beg.cropSet ? beg.crop[i] : pair(i).num(), pair(i).num());
            cur.cropSet = true;
        }
    }
}

struct InterpSt : AStmt {
    std::string warper;
    double duration = 0;
    std::vector<std::pair<std::string, Value>> props;
    struct S : AState {
        TransformState begin;
    };
    Res exec(ACtx& c, double t, std::unique_ptr<AState>& s) override {
        if (!s) {
            auto n = std::make_unique<S>();
            n->begin = c.tr.state;
            s = std::move(n);
        }
        auto& st = static_cast<S&>(*s);
        double complete = duration <= 0 ? 1 : std::clamp(t / duration, 0.0, 1.0);
        const double w = warper == "pause" ? (t >= duration ? 1 : 0) : warp(warper, complete);
        for (auto& [k, v] : props) applyProp(c.tr.state, st.begin, k, v, duration <= 0 ? 1 : w);
        if (t >= duration) return {Act::Next, t - duration};
        return {};
    }
};

struct ChildSt : AStmt {
    DispP disp;
    std::shared_ptr<Transition> trans;
    Res exec(ACtx& c, double t, std::unique_ptr<AState>& s) override {
        if (!s) {
            s = std::make_unique<AState>();
            c.tr.setChild(disp ? disp->instance() : nullptr, c.st - t, trans);
        }
        return {Act::Next, t};
    }
};

struct BlockSt : AStmt {
    std::vector<AStmtP> stmts;
    std::vector<std::pair<double, size_t>> times;
    struct S : AState {
        size_t index = 0, timeIdx = 0;
        double start = 0, loopStart = 0;
        int repeats = 0;
        std::unique_ptr<AState> child;
    };
    Res exec(ACtx& c, double t, std::unique_ptr<AState>& s) override {
        if (!s) s = std::make_unique<S>();
        auto& b = static_cast<S&>(*s);
        while (b.timeIdx < times.size() && times[b.timeIdx].first <= t) {
            // Jump past the `time` statement, also when execution is waiting on it.
            if (times[b.timeIdx].second >= b.index || b.index >= stmts.size()) {
                b.index = times[b.timeIdx].second + 1;
                b.start = times[b.timeIdx].first;
                b.child.reset();
            }
            ++b.timeIdx;
        }
        for (int guard = 0; guard < 256; ++guard) {
            if (b.index >= stmts.size()) return {Act::Next, t - b.start};
            Res r = stmts[b.index]->exec(c, t - b.start, b.child);
            if (r.a == Act::Continue) return r;
            if (r.a == Act::Next) {
                ++b.index;
                b.start = t - r.left;
                b.child.reset();
                continue;
            }
            double loopEnd = t - r.left;
            const double dur = loopEnd - b.loopStart;
            ++b.repeats;
            if (r.count > 0 && b.repeats >= r.count) {
                ++b.index;
                b.start = loopEnd;
                b.child.reset();
                continue;
            }
            if (dur <= 1e-6) {
                // A zero-length loop runs again next frame.
                b.index = 0;
                b.start = b.loopStart = loopEnd;
                b.child.reset();
                return {};
            }
            if (r.count == 0 && t - loopEnd > dur) loopEnd += std::floor((t - loopEnd) / dur) * dur;
            b.index = 0;
            b.start = b.loopStart = loopEnd;
            b.timeIdx = 0;
            while (b.timeIdx < times.size() && times[b.timeIdx].first <= b.start) ++b.timeIdx;
            b.child.reset();
        }
        return {};
    }
};

struct RepeatSt : AStmt {
    int count = 0;
    Res exec(ACtx&, double t, std::unique_ptr<AState>&) override { return {Act::Repeat, t, count}; }
};

struct TimeSt : AStmt {
    Res exec(ACtx&, double, std::unique_ptr<AState>&) override { return {}; }
};

struct ParallelSt : AStmt {
    std::vector<AStmtP> blocks;
    struct S : AState {
        std::vector<std::unique_ptr<AState>> ch;
        std::vector<char> done;
        std::vector<double> left;
    };
    Res exec(ACtx& c, double t, std::unique_ptr<AState>& s) override {
        if (!s) {
            auto n = std::make_unique<S>();
            n->ch.resize(blocks.size());
            n->done.assign(blocks.size(), 0);
            n->left.assign(blocks.size(), 0);
            s = std::move(n);
        }
        auto& p = static_cast<S&>(*s);
        bool all = true;
        for (size_t i = 0; i < blocks.size(); ++i) {
            if (p.done[i]) continue;
            Res r = blocks[i]->exec(c, t, p.ch[i]);
            if (r.a == Act::Next) {
                p.done[i] = 1;
                p.left[i] = r.left;
            } else all = false;
        }
        if (!all) return {};
        double left = t;
        for (double l : p.left) left = std::min(left, l);
        return {Act::Next, left};
    }
};

struct ChoiceSt : AStmt {
    std::vector<std::pair<double, AStmtP>> choices;
    struct S : AState {
        int pick = -1;
        std::unique_ptr<AState> child;
    };
    Res exec(ACtx& c, double t, std::unique_ptr<AState>& s) override {
        if (!s) {
            auto n = std::make_unique<S>();
            double total = 0;
            for (auto& ch : choices) total += ch.first;
            double r = c.vm.py.random() * total;
            n->pick = int(choices.size()) - 1;
            for (size_t i = 0; i < choices.size(); ++i) {
                if (r < choices[i].first) {
                    n->pick = int(i);
                    break;
                }
                r -= choices[i].first;
            }
            s = std::move(n);
        }
        auto& ch = static_cast<S&>(*s);
        if (ch.pick < 0) return {Act::Next, t};
        return choices[size_t(ch.pick)].second->exec(c, t, ch.child);
    }
};

struct OnSt : AStmt {
    std::map<std::string, AStmtP> handlers;
    struct S : AState {
        std::string name;
        double start = 0;
        size_t seen = 0;
        std::unique_ptr<AState> child;
    };
    Res exec(ACtx& c, double t, std::unique_ptr<AState>& s) override {
        if (!s) {
            auto n = std::make_unique<S>();
            if (handlers.count("start")) n->name = "start";
            s = std::move(n);
        }
        auto& o = static_cast<S&>(*s);
        auto& events = c.tr.events_;
        for (; o.seen < events.size(); ++o.seen)
            if (handlers.count(events[o.seen])) {
                o.name = events[o.seen];
                o.start = t;
                o.child.reset();
            }
        if (o.name.empty()) return {};
        Res r = handlers[o.name]->exec(c, t - o.start, o.child);
        if (r.a == Act::Next) {
            if (o.name == "hide" || o.name == "replaced") c.tr.markHideDone();
            if (o.name != "default" && handlers.count("default")) {
                o.name = "default";
                o.start = t - r.left;
            } else o.name.clear();
            o.child.reset();
        }
        return {};
    }
};

struct FunctionSt : AStmt {
    Value fn;
    Res exec(ACtx& c, double t, std::unique_ptr<AState>&) override {
        auto& st = c.tr.state;
        Value proxy = makeInstance("Transform");
        auto& a = proxy.o->attrs;
        a["xoffset"] = Value::real(st.xoffset);
        a["yoffset"] = Value::real(st.yoffset);
        a["xzoom"] = Value::real(st.xzoom);
        a["yzoom"] = Value::real(st.yzoom);
        a["zoom"] = Value::real(st.zoom);
        a["alpha"] = Value::real(st.alpha);
        a["rotate"] = st.rotateSet ? Value::real(st.rotate) : Value();
        Value r;
        try {
            r = c.vm.py.call(fn, {proxy, Value::real(t), Value::real(c.st)});
        } catch (const PyError& e) {
            c.vm.backend.log(std::string("ATL function: ") + e.what());
            return {Act::Next, t};
        }
        st.xoffset = a["xoffset"].num();
        st.yoffset = a["yoffset"].num();
        st.xzoom = a["xzoom"].num();
        st.yzoom = a["yzoom"].num();
        st.zoom = a["zoom"].num();
        st.alpha = a["alpha"].num();
        if (!a["rotate"].isNone()) {
            st.rotate = a["rotate"].num();
            st.rotateSet = true;
        }
        if (r.isNone()) return {Act::Next, 0};
        return {};
    }
};

struct NextSt : AStmt {
    Res exec(ACtx&, double t, std::unique_ptr<AState>&) override { return {Act::Next, t}; }
};

std::string tail(const std::string& cls) {
    auto p = cls.rfind('.');
    return p == std::string::npos ? cls : cls.substr(p + 1);
}

ScopeP scopeFor(Vm& vm, const AtlTemplate& t) {
    auto scope = std::make_shared<Scope>();
    for (auto& [k, v] : t.bound) scope->vars[k] = v;
    for (auto& [name, def] : t.params)
        if (!scope->vars.count(name) && !def.empty()) {
            try {
                scope->vars[name] = vm.py.eval(def);
            } catch (const PyError&) {
            }
        }
    return scope;
}

AStmtP compileAtl(Vm& vm, const Value& raw, const ScopeP& scope, int depth = 0);

AStmtP compileMulti(Vm& vm, const Value& raw, const ScopeP& scope, int depth) {
    auto eval = [&](const std::string& src) -> Value {
        try {
            return vm.py.eval(src, scope);
        } catch (const PyError& e) {
            vm.backend.log("ATL expression '" + src + "': " + e.what());
            return {};
        }
    };
    const std::string warper = raw.get("warper").text();
    const std::string durSrc = raw.get("duration").isNone() ? "0" : raw.get("duration").text();
    const Value& props = raw.get("properties").isNone() ? Value() : raw.get("properties");
    const Value exprs = raw.get("expressions");
    const bool hasProps = props.isObj() && !props.items().empty();
    const bool noWarper = raw.get("warper").isNone();
    auto block = std::make_shared<BlockSt>();
    auto interp = std::make_shared<InterpSt>();
    interp->warper = noWarper ? "instant" : warper;
    interp->duration = noWarper ? 0 : eval(durSrc).num();
    for (auto& p : props.items()) {
        auto& kv = p.items();
        if (kv.size() == 2) interp->props.emplace_back(kv[0].text(), eval(kv[1].text()));
    }
    for (auto& e : exprs.items()) {
        auto& pair = e.items();
        if (pair.empty()) continue;
        Value v = eval(pair[0].text());
        const Value withV = pair.size() > 1 && !pair[1].isNone() ? eval(pair[1].text()) : Value();
        if (v.isNum() && v.t != Value::T::Bool) {
            if (noWarper && !hasProps) {
                interp->warper = "pause";
                interp->duration = v.num();
            }
            continue;
        }
        if (v.cls() == "ATLTransform" && v.o->payload) {
            auto& t = *std::static_pointer_cast<AtlTemplate>(v.o->payload);
            for (auto& kv : t.props) interp->props.push_back(kv);
            if (!t.atl.isNone() && depth < 16) block->stmts.push_back(compileAtl(vm, t.atl, scopeFor(vm, t), depth + 1));
            continue;
        }
        auto child = std::make_shared<ChildSt>();
        child->disp = vm.toDisp(v);
        child->trans = vm.toTransition(withV);
        block->stmts.push_back(child);
    }
    if (block->stmts.empty()) return interp;
    // Expressions run first (inlined transforms, child changes), then the properties.
    if (!interp->props.empty() || interp->duration > 0) block->stmts.push_back(interp);
    if (block->stmts.size() == 1) return block->stmts[0];
    return block;
}

AStmtP compileAtl(Vm& vm, const Value& raw, const ScopeP& scope, int depth) {
    const std::string k = tail(raw.cls());
    auto eval = [&](const std::string& src) -> Value {
        try {
            return vm.py.eval(src, scope);
        } catch (const PyError& e) {
            vm.backend.log("ATL expression '" + src + "': " + e.what());
            return {};
        }
    };
    if (k == "RawBlock") {
        auto b = std::make_shared<BlockSt>();
        for (auto& s : raw.get("statements").items()) {
            if (tail(s.cls()) == "RawTime") b->times.emplace_back(eval(s.get("time").text()).num(), b->stmts.size());
            b->stmts.push_back(compileAtl(vm, s, scope, depth));
        }
        return b;
    }
    if (k == "RawMultipurpose") return compileMulti(vm, raw, scope, depth);
    if (k == "RawRepeat") {
        auto r = std::make_shared<RepeatSt>();
        if (!raw.get("repeats").isNone()) r->count = int(eval(raw.get("repeats").text()).integer());
        return r;
    }
    if (k == "RawParallel") {
        auto p = std::make_shared<ParallelSt>();
        for (auto& b : raw.get("blocks").items()) p->blocks.push_back(compileAtl(vm, b, scope, depth));
        return p;
    }
    if (k == "RawChoice") {
        auto c = std::make_shared<ChoiceSt>();
        for (auto& ch : raw.get("choices").items()) {
            auto& kv = ch.items();
            if (kv.size() == 2) c->choices.emplace_back(eval(kv[0].text()).num(), compileAtl(vm, kv[1], scope, depth));
        }
        return c;
    }
    if (k == "RawOn") {
        auto o = std::make_shared<OnSt>();
        const Value& h = raw.get("handlers");
        if (h.isObj())
            for (auto& [name, block] : h.o->dict) o->handlers[name.text()] = compileAtl(vm, block, scope, depth);
        return o;
    }
    if (k == "RawTime") return std::make_shared<TimeSt>();
    if (k == "RawFunction") {
        auto f = std::make_shared<FunctionSt>();
        f->fn = eval(raw.get("expr").text());
        if (!f->fn.isObj()) return std::make_shared<NextSt>();
        return f;
    }
    if (k == "RawContainsExpr") {
        auto c = std::make_shared<ChildSt>();
        c->disp = vm.toDisp(eval(raw.get("expression").text()));
        return c;
    }
    if (k == "RawChild") {
        auto fixed = std::make_shared<FixedDisp>();
        for (auto& b : raw.get("children").items()) {
            auto t = std::make_shared<AtlTemplate>();
            t->atl = b;
            fixed->children.push_back(std::make_shared<TransformDisp>(t));
        }
        auto c = std::make_shared<ChildSt>();
        c->disp = fixed;
        return c;
    }
    return std::make_shared<NextSt>();
}

bool atlHandles(const Value& raw, const std::string& event, int depth = 0) {
    if (!raw.isObj() || depth > 32) return false;
    const std::string k = tail(raw.cls());
    if (k == "RawOn") {
        const Value& h = raw.get("handlers");
        if (h.isObj())
            for (auto& kv : h.o->dict)
                if (kv.first.text() == event) return true;
        return false;
    }
    if (k == "RawBlock") {
        for (auto& s : raw.get("statements").items())
            if (atlHandles(s, event, depth + 1)) return true;
    }
    if (k == "RawParallel")
        for (auto& b : raw.get("blocks").items())
            if (atlHandles(b, event, depth + 1)) return true;
    return false;
}

} // namespace

// ---- Render / TransformDisp -------------------------------------------------

void Render::blit(const Render& r, double x, double y, double alpha) {
    for (auto op : r.ops) {
        op.m[2] += float(x);
        op.m[5] += float(y);
        op.alpha *= float(alpha);
        ops.push_back(std::move(op));
    }
}

struct TransformDisp::Run {
    AStmtP block;
    std::unique_ptr<AState> state;
};

TransformDisp::TransformDisp(std::shared_ptr<AtlTemplate> templ) : templ_(std::move(templ)) {}
TransformDisp::~TransformDisp() = default;

DispP TransformDisp::instance() {
    auto t = std::make_shared<TransformDisp>(templ_);
    t->defaultPlacement = defaultPlacement;
    if (child_ && !started_) t->child_ = child_->instance();
    return t;
}

bool TransformDisp::handles(const std::string& event) const {
    return templ_ && atlHandles(templ_->atl, event);
}

void TransformDisp::setChild(const DispP& child, double st, std::shared_ptr<Transition> trans) {
    oldChild_ = child_;
    oldChildBase_ = childBase_;
    child_ = child;
    childBase_ = st;
    childChangeSt_ = st;
    childTrans_ = oldChild_ ? std::move(trans) : nullptr;
    if (auto t = std::dynamic_pointer_cast<TransformDisp>(child_))
        for (auto& e : events_) t->event(e);
}

Render TransformDisp::render(Vm& vm, double st, double at) {
    if (!started_) {
        started_ = true;
        if (templ_) {
            for (auto& [k, v] : templ_->props) applyProp(state, state, k, v, 1);
            if (templ_->child && !child_) setChild(templ_->child->instance(), 0);
            if (templ_->atl.isObj()) {
                run_ = std::make_unique<Run>();
                run_->block = compileAtl(vm, templ_->atl, scopeFor(vm, *templ_));
            }
        }
    }
    if (run_ && st != lastSt) {
        lastSt = st;
        ACtx c{vm, *this, st};
        try {
            run_->block->exec(c, st, run_->state);
        } catch (const PyError& e) {
            vm.backend.log(std::string("ATL: ") + e.what());
            run_.reset();
        }
    }
    Render cr;
    if (child_) cr = child_->render(vm, st - childBase_, at);
    if (oldChild_ && childTrans_) {
        const double dur = childTrans_->duration();
        const double p = dur > 0 ? (st - childChangeSt_) / dur : 1;
        if (p < 1) {
            Render old = oldChild_->render(vm, st - oldChildBase_, at);
            Render mix;
            mix.w = std::max(old.w, cr.w), mix.h = std::max(old.h, cr.h);
            mix.xpos = cr.xpos, mix.ypos = cr.ypos, mix.xanchor = cr.xanchor, mix.yanchor = cr.yanchor;
            mix.xoffset = cr.xoffset, mix.yoffset = cr.yoffset;
            mix.blit(old, 0, 0, 1 - std::max(0.0, p));
            mix.blit(cr, 0, 0, std::max(0.0, p));
            cr = std::move(mix);
        } else {
            oldChild_.reset();
            childTrans_.reset();
        }
    }
    if (state.xtile > 1 || state.ytile > 1) {
        Render t;
        t.w = cr.w * state.xtile, t.h = cr.h * state.ytile;
        t.xpos = cr.xpos, t.ypos = cr.ypos, t.xanchor = cr.xanchor, t.yanchor = cr.yanchor;
        for (int j = 0; j < state.ytile; ++j)
            for (int i = 0; i < state.xtile; ++i) t.blit(cr, i * cr.w, j * cr.h);
        cr = std::move(t);
    }
    if (state.cropSet) {
        // crop (x, y, w, h) in child pixels: axis-aligned ops are clipped to it.
        const double cx = state.crop[0], cy = state.crop[1], cw = state.crop[2], ch = state.crop[3];
        Render t;
        t.w = cw, t.h = ch;
        t.xpos = cr.xpos, t.ypos = cr.ypos, t.xanchor = cr.xanchor, t.yanchor = cr.yanchor;
        t.xoffset = cr.xoffset, t.yoffset = cr.yoffset;
        for (auto op : cr.ops) {
            if (op.k != DrawOp::K::Text && op.m[1] == 0 && op.m[3] == 0 && op.m[0] > 0 && op.m[4] > 0) {
                const double sx = op.crop[2] > 0 ? op.crop[0] : 0, sy = op.crop[2] > 0 ? op.crop[1] : 0;
                const double sw = op.crop[2] > 0 ? op.crop[2] : op.w, sh = op.crop[2] > 0 ? op.crop[3] : op.h;
                const double x0 = std::max<double>(op.m[2], cx), x1 = std::min<double>(op.m[2] + op.m[0] * sw, cx + cw);
                const double y0 = std::max<double>(op.m[5], cy), y1 = std::min<double>(op.m[5] + op.m[4] * sh, cy + ch);
                if (x1 <= x0 || y1 <= y0) continue;
                op.crop[0] = float(sx + (x0 - op.m[2]) / op.m[0]);
                op.crop[1] = float(sy + (y0 - op.m[5]) / op.m[4]);
                op.crop[2] = float((x1 - x0) / op.m[0]);
                op.crop[3] = float((y1 - y0) / op.m[4]);
                op.m[2] = float(x0 - cx), op.m[5] = float(y0 - cy);
            } else {
                op.m[2] -= float(cx), op.m[5] -= float(cy);
            }
            t.ops.push_back(std::move(op));
        }
        cr = std::move(t);
    }
    const double zx = state.zoom * state.xzoom, zy = state.zoom * state.yzoom;
    double W = std::fabs(cr.w * zx), H = std::fabs(cr.h * zy);
    Aff a{zx, 0, zx < 0 ? W : 0, 0, zy, zy < 0 ? H : 0};
    if (state.rotateSet && std::fabs(state.rotate) > 1e-9) {
        const double r = state.rotate * kPi / 180, c = std::cos(r), s = std::sin(r);
        const double W2 = std::fabs(W * c) + std::fabs(H * s), H2 = std::fabs(W * s) + std::fabs(H * c);
        Aff toOrigin{1, 0, -W / 2, 0, 1, -H / 2}, rot{c, -s, 0, s, c, 0}, back{1, 0, W2 / 2, 0, 1, H2 / 2};
        a = mul(back, mul(rot, mul(toOrigin, a)));
        W = W2, H = H2;
    }
    Render r;
    r.w = W, r.h = H;
    r.ops.reserve(cr.ops.size());
    for (auto op : cr.ops) {
        transformOp(op, a);
        op.alpha *= float(std::clamp(state.alpha, 0.0, 1.0));
        r.ops.push_back(std::move(op));
    }
    r.xpos = state.xpos.set ? state.xpos : cr.xpos;
    r.ypos = state.ypos.set ? state.ypos : cr.ypos;
    r.xanchor = state.xanchor.set ? state.xanchor : cr.xanchor;
    r.yanchor = state.yanchor.set ? state.yanchor : cr.yanchor;
    r.xoffset = state.xoffset + cr.xoffset;
    r.yoffset = state.yoffset + cr.yoffset;
    return r;
}

double Transition::duration() const {
    if (k == K::Multiple) {
        double d = 0;
        for (auto& s : steps) d += s ? s->duration() : 0;
        return d;
    }
    return time;
}

// ---- Text -------------------------------------------------------------------

RichText parseRich(const std::string& text, double cps) {
    RichText out;
    bool italic = false, bold = false;
    std::vector<uint32_t> colors;
    double t = 0, rate = cps, base = cps;
    float space = 0;
    for (size_t i = 0; i < text.size();) {
        if (text[i] == '{') {
            if (i + 1 < text.size() && text[i + 1] == '{') {
                i += 1;
            } else {
                const size_t e = text.find('}', i);
                if (e == std::string::npos) break;
                const std::string tag = text.substr(i + 1, e - i - 1);
                const size_t eq = tag.find('=');
                const std::string name = tag.substr(0, eq), arg = eq == std::string::npos ? "" : tag.substr(eq + 1);
                if (name == "i") italic = true;
                else if (name == "/i") italic = false;
                else if (name == "b") bold = true;
                else if (name == "/b") bold = false;
                else if (name == "color") colors.push_back(parseColor(arg));
                else if (name == "/color" && !colors.empty()) colors.pop_back();
                else if (name == "cps") {
                    if (!arg.empty() && arg[0] == '*') rate = base * std::atof(arg.c_str() + 1);
                    else rate = std::atof(arg.c_str());
                } else if (name == "/cps") rate = base;
                else if (name == "fast") {
                    for (auto& g : out.glyphs) g.t = 0;
                    out.waits.clear();
                    t = 0;
                } else if (name == "w") out.waits.push_back({out.glyphs.size(), arg.empty() ? 0 : 1, std::atof(arg.c_str())});
                else if (name == "nw") out.waits.push_back({out.glyphs.size(), 2, 0});
                else if (name == "space") space += float(std::atof(arg.c_str()));
                i = e + 1;
                continue;
            }
        }
        size_t len = 1;
        const uint8_t c = uint8_t(text[i]);
        if (c >= 0xF0) len = 4;
        else if (c >= 0xE0) len = 3;
        else if (c >= 0xC0) len = 2;
        Glyph g;
        g.ch = text.substr(i, len);
        g.italic = italic, g.bold = bold;
        if (!colors.empty()) g.hasColor = true, g.color = colors.back();
        g.space = space;
        space = 0;
        g.t = t;
        if (rate > 0) t += 1.0 / rate;
        out.glyphs.push_back(std::move(g));
        i += len;
    }
    return out;
}

std::string stripTags(const std::string& text) {
    std::string out;
    for (size_t i = 0; i < text.size(); ++i) {
        if (text[i] == '{') {
            if (i + 1 < text.size() && text[i + 1] == '{') {
                out += '{';
                ++i;
                continue;
            }
            const size_t e = text.find('}', i);
            if (e == std::string::npos) break;
            i = e;
            continue;
        }
        out += text[i];
    }
    return out;
}

size_t Window::visibleAt(double now) const {
    if (from >= to || from >= rich.glyphs.size()) return to;
    const double base = rich.glyphs[from].t, el = now - t0;
    size_t n = from;
    while (n < to && n < rich.glyphs.size() && rich.glyphs[n].t - base <= el) ++n;
    return n;
}

bool Scene::hasScreen(const std::string& n) const {
    for (auto& s : screens)
        if (s.name == n) return true;
    return false;
}

// ---- Vm display helpers -----------------------------------------------------

Value Vm::dispValue(const DispP& d) {
    Value v = makeInstance("Displayable");
    v.o->payload = d;
    return v;
}

DispP Vm::imageNamed(const std::vector<std::string>& name) {
    std::string key;
    for (auto& n : name) key += (key.empty() ? "" : " ") + n;
    auto it = images.find(key);
    if (it != images.end()) return toDisp(it->second);
    if (!name.empty()) {
        auto head = images.find(name[0]);
        if (head != images.end() && head->second.cls() == "ParameterizedText") {
            std::string rest = key.substr(name[0].size());
            if (!rest.empty() && rest[0] == ' ') rest.erase(0, 1);
            if (rest.size() >= 2 && (rest[0] == '"' || rest[0] == '\'')) rest = rest.substr(1, rest.size() - 2);
            auto t = std::make_shared<TextDisp>();
            t->text = interpolate(rest);
            t->style = styleNamed(head->second.get("style").text());
            const Value* xa = head->second.attr("xalign");
            const Value* ya = head->second.attr("yalign");
            if (xa) t->xpos = t->xanchor = Pos::frac(xa->num());
            if (ya) t->ypos = t->yanchor = Pos::frac(ya->num());
            return t;
        }
    }
    return nullptr;
}

DispP Vm::toDisp(const Value& v) {
    if (v.isNone()) return nullptr;
    if (v.isStr()) {
        const std::string& s = v.s;
        if (s.empty()) return nullptr;
        if (s[0] == '#') return std::make_shared<SolidDisp>(parseColor(s));
        if (s.find('.') != std::string::npos && s.find(' ') == std::string::npos) {
            std::string file = assets.resolveImage(s);
            if (file.empty()) file = s;
            return std::make_shared<ImageDisp>(file);
        }
        std::vector<std::string> parts;
        size_t start = 0;
        while (start < s.size()) {
            size_t e = s.find(' ', start);
            if (e == std::string::npos) e = s.size();
            if (e > start) parts.push_back(s.substr(start, e - start));
            start = e + 1;
        }
        if (DispP d = imageNamed(parts)) return d;
        auto lazy = std::make_shared<LazyDisp>();
        lazy->name = std::move(parts);
        return lazy;
    }
    if (!v.isObj()) return nullptr;
    if (v.cls() == "Displayable" && v.o->payload) return std::static_pointer_cast<Disp>(v.o->payload);
    if (v.cls() == "ATLTransform" && v.o->payload)
        return std::make_shared<TransformDisp>(std::static_pointer_cast<AtlTemplate>(v.o->payload));
    return std::make_shared<NullDisp>();
}

std::shared_ptr<Transition> Vm::toTransition(const Value& v) {
    if (v.cls() == "Transition" && v.o->payload) return std::static_pointer_cast<Transition>(v.o->payload);
    return nullptr;
}

TextStyle Vm::styleNamed(const std::string& name) {
    TextStyle s;
    auto black = [&](const char* font, float size) {
        s.font = font;
        s.size = size;
        s.color = 0x000000ff;
        s.outlines.clear();
    };
    s.outlines = {{2.f, 0x000000aa}};
    if (name == "splash_text") {
        black("gui/font/Aller_Rg.ttf", 24);
        s.align = .5f;
    } else if (name == "poemgame_text") black("gui/font/Halogen.ttf", 30);
    else if (name == "yuri_text") black("gui/font/y1.ttf", 32);
    else if (name == "yuri_text_2") black("gui/font/y2.ttf", 40);
    else if (name == "yuri_text_3") black("gui/font/y3.ttf", 18);
    else if (name == "natsuki_text") black("gui/font/n1.ttf", 28);
    else if (name == "sayori_text") black("gui/font/s1.ttf", 34);
    else if (name == "monika_text") black("gui/font/m1.ttf", 34);
    else if (name == "say_label") {
        s.font = "gui/font/RifficFree-Bold.ttf";
        s.size = 24;
        s.outlines = {{3.f, 0xbb5599ff}, {1.f, 0xbb5599ff}};
    } else if (name == "navigation_button_text") {
        s.font = "gui/font/RifficFree-Bold.ttf";
        s.size = 24;
        s.outlines = {{4.f, 0xbb5599ff}, {2.f, 0xbb5599ff}};
    } else if (name == "choice_button_text" || name == "dialog_text") {
        black("gui/font/Aller_Rg.ttf", 24);
        s.align = .5f;
    }
    return s;
}

Render Vm::renderLayer(const std::vector<Shown>& items, double t) {
    Render out;
    out.w = kScreenW, out.h = kScreenH;
    for (auto& it : items) {
        if (!it.xf) continue;
        const double st = std::max(0.0, t - it.shownAt);
        Render r = it.xf->render(*this, st, st);
        const Pos xp = r.xpos.set ? r.xpos : Pos::frac(.5), yp = r.ypos.set ? r.ypos : Pos::frac(1);
        const Pos xa = r.xanchor.set ? r.xanchor : Pos::frac(.5), ya = r.yanchor.set ? r.yanchor : Pos::frac(1);
        out.blit(r, xp.at(out.w) - xa.at(r.w) + r.xoffset, yp.at(out.h) - ya.at(r.h) + r.yoffset);
    }
    return out;
}

void Vm::collectHidden() {
    auto& m = scene.master;
    auto done = [](const Shown& s) {
        if (!s.hiding) return false;
        bool any = false;
        for (DispP d = s.xf; auto t = std::dynamic_pointer_cast<TransformDisp>(d); d = t->child()) {
            if (!t->handles("hide")) continue;
            any = true;
            if (!t->hideDone()) return false;
        }
        return any || !s.xf;
    };
    m.erase(std::remove_if(m.begin(), m.end(), done), m.end());
}

// Factories used by the natives.
DispP makeImageDisp(const std::string& file) { return std::make_shared<ImageDisp>(file); }
DispP makeSolidDisp(uint32_t color) { return std::make_shared<SolidDisp>(color); }
DispP makeNullDisp(double w, double h) {
    auto n = std::make_shared<NullDisp>();
    n->w = w, n->h = h;
    return n;
}
DispP makeTextDisp(const std::string& text, const TextStyle& style, const Value& xalign, const Value& yalign) {
    auto t = std::make_shared<TextDisp>();
    t->text = text;
    t->style = style;
    if (!xalign.isNone()) t->xpos = t->xanchor = Pos::frac(xalign.num());
    if (!yalign.isNone()) t->ypos = t->yanchor = Pos::frac(yalign.num());
    return t;
}
DispP makeCompositeDisp(double w, double h, std::vector<std::tuple<double, double, DispP>> parts, bool live) {
    auto c = std::make_shared<CompositeDisp>();
    c->w = w, c->h = h, c->live = live;
    for (auto& [x, y, d] : parts) c->parts.push_back({x, y, d});
    return c;
}
DispP makeTileDisp(const DispP& child) {
    auto t = std::make_shared<TileDisp>();
    t->child = child;
    return t;
}
DispP makeSwitchDisp(std::vector<std::pair<std::string, DispP>> cases) {
    auto s = std::make_shared<SwitchDisp>();
    s->cases = std::move(cases);
    return s;
}

} // namespace Ddlc
