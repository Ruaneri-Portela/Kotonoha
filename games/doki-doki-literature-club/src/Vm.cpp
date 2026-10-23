// The Ren'Py script executor: the init phase, the statements, the user
// statements (play/stop/window/pause/screen) and the native renpy API.
// The script runs on its own thread used as a coroutine: it only runs while
// the host waits in resume(), so the two never touch the state concurrently.
#include "Ddlc/Runtime.hpp"

#include <SDL3/SDL_thread.h>
#include <SDL3/SDL_timer.h>

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace Ddlc {

struct Vm::ThreadHolder {
    SDL_Thread* thread = nullptr;
};

namespace {

const Value* kwFind(const Kwargs& kw, const char* name) {
    for (auto& [k, v] : kw)
        if (k == name) return &v;
    return nullptr;
}
Value kwGet(const Kwargs& kw, const char* name, Value def = {}) {
    const Value* v = kwFind(kw, name);
    return v ? *v : def;
}
Value argOr(const Args& a, size_t i, const Kwargs& kw, const char* name, Value def = {}) {
    if (i < a.size()) return a[i];
    return kwGet(kw, name, std::move(def));
}

std::vector<std::string> splitWords(const std::string& s) {
    std::vector<std::string> out;
    std::istringstream in(s);
    std::string w;
    while (in >> w) out.push_back(w);
    return out;
}

std::string trim(const std::string& s) {
    size_t a = 0, b = s.size();
    while (a < b && std::isspace((unsigned char)s[a])) ++a;
    while (b > a && std::isspace((unsigned char)s[b - 1])) --b;
    return s.substr(a, b - a);
}

// Splits a user statement into tokens, keeping quoted strings and
// parenthesised groups whole.
std::vector<std::string> tokens(const std::string& s) {
    std::vector<std::string> out;
    size_t i = 0;
    while (i < s.size()) {
        if (std::isspace((unsigned char)s[i])) {
            ++i;
            continue;
        }
        size_t start = i;
        int depth = 0;
        char quote = 0;
        while (i < s.size()) {
            char c = s[i];
            if (quote) {
                if (c == '\\') ++i;
                else if (c == quote) quote = 0;
            } else if (c == '"' || c == '\'') quote = c;
            else if (c == '(' || c == '[') ++depth;
            else if (c == ')' || c == ']') --depth;
            else if (std::isspace((unsigned char)c) && depth == 0) break;
            ++i;
        }
        out.push_back(s.substr(start, i - start));
    }
    return out;
}

Value atlValue(std::shared_ptr<AtlTemplate> t, Vm* vm);

// Calling a transform binds its parameters; a remaining positional argument
// (or child=) becomes its child.
Value bindTransform(const std::shared_ptr<AtlTemplate>& t, Vm* vm, Args& a, Kwargs& kw) {
    auto n = std::make_shared<AtlTemplate>(*t);
    size_t pi = 0;
    for (auto& v : a) {
        while (pi < n->params.size() && n->bound.count(n->params[pi].first)) ++pi;
        if (pi < n->params.size()) n->bound[n->params[pi++].first] = v;
        else n->child = vm->toDisp(v);
    }
    for (auto& [k, v] : kw) {
        if (k == "child") n->child = vm->toDisp(v);
        else n->bound[k] = v;
    }
    return atlValue(n, vm);
}

Value atlValue(std::shared_ptr<AtlTemplate> t, Vm* vm) {
    Value v = makeInstance("ATLTransform");
    v.o->payload = t;
    v.o->fn = [t, vm](Interp&, Args& a, Kwargs& kw) { return bindTransform(t, vm, a, kw); };
    return v;
}

Value transitionValue(std::shared_ptr<Transition> t) {
    Value v = makeInstance("Transition");
    v.o->payload = std::move(t);
    return v;
}

std::shared_ptr<Transition> dissolve(double time, bool alpha = false) {
    auto t = std::make_shared<Transition>();
    t->k = Transition::K::Dissolve;
    t->time = time;
    t->alpha = alpha;
    return t;
}

std::shared_ptr<AtlTemplate> placement(double xpos, double xanchor, double ypos, double yanchor, const char* name) {
    auto t = std::make_shared<AtlTemplate>();
    t->name = name;
    t->props = {{"xpos", Value::real(xpos)}, {"xanchor", Value::real(xanchor)},
                {"ypos", Value::real(ypos)}, {"yanchor", Value::real(yanchor)}};
    return t;
}

template <class F> void forChain(const std::shared_ptr<TransformDisp>& xf, F f) {
    for (DispP d = xf; auto t = std::dynamic_pointer_cast<TransformDisp>(d); d = t->child()) f(*t);
}

int SDLCALL threadEntry(void* p);

} // namespace

struct VmNatives {
    static void run(Vm& vm) { vm.threadMain(); }
    static void install(Vm& vm);
};

namespace {
int SDLCALL threadEntry(void* p) {
    VmNatives::run(*static_cast<Vm*>(p));
    return 0;
}
} // namespace

// ---- lifecycle --------------------------------------------------------------

Vm::Vm(const Assets& assets, const Script& script, Backend& backend)
    : assets(assets), script(script), backend(backend) {
    clock = [] { return double(SDL_GetTicksNS()) / 1e9; };
}

Vm::~Vm() { stop(); }

void Vm::start(const std::string& label) {
    stop();
    startLabel_ = label;
    abort_ = false;
    inter_ = {};
    callStack_.clear();
    thread_ = std::make_unique<ThreadHolder>();
    {
        std::lock_guard<std::mutex> lk(mu_);
        vmTurn_ = true;
    }
    threadAlive_ = true;
    SDL_PropertiesID props = SDL_CreateProperties();
    SDL_SetPointerProperty(props, SDL_PROP_THREAD_CREATE_ENTRY_FUNCTION_POINTER, (void*)&threadEntry);
    SDL_SetStringProperty(props, SDL_PROP_THREAD_CREATE_NAME_STRING, "ddlc-script");
    SDL_SetPointerProperty(props, SDL_PROP_THREAD_CREATE_USERDATA_POINTER, this);
    SDL_SetNumberProperty(props, SDL_PROP_THREAD_CREATE_STACKSIZE_NUMBER, 64 * 1024 * 1024);
    thread_->thread = SDL_CreateThreadWithProperties(props);
    SDL_DestroyProperties(props);
    if (!thread_->thread) {
        threadAlive_ = false;
        inter_.k = Interaction::K::End;
        errors.push_back("could not create the script thread");
        return;
    }
    std::unique_lock<std::mutex> lk(mu_);
    cv_.wait(lk, [&] { return !vmTurn_; });
}

void Vm::resume(Value result) {
    if (!threadAlive_) return;
    result_ = std::move(result);
    std::unique_lock<std::mutex> lk(mu_);
    vmTurn_ = true;
    cv_.notify_all();
    cv_.wait(lk, [&] { return !vmTurn_; });
}

void Vm::stop() {
    if (!thread_) return;
    {
        std::lock_guard<std::mutex> lk(mu_);
        abort_ = true;
        vmTurn_ = true;
    }
    cv_.notify_all();
    if (thread_->thread) SDL_WaitThread(thread_->thread, nullptr);
    thread_.reset();
    threadAlive_ = false;
    abort_ = false;
    vmTurn_ = false;
}

void Vm::threadMain() {
    Interaction last;
    last.k = Interaction::K::End;
    try {
        Node* n = script.label(startLabel_);
        if (!n) errors.push_back("missing label " + startLabel_);
        runFrom(n);
    } catch (const AbortSignal&) {
    } catch (const RestartSignal&) {
        last.k = Interaction::K::Restart;
    } catch (const QuitSignal&) {
    } catch (const std::exception& e) {
        errors.push_back(std::string("script: ") + e.what());
        backend.log(errors.back());
    }
    std::lock_guard<std::mutex> lk(mu_);
    if (!abort_) {
        last.start = now();
        inter_ = last;
    }
    threadAlive_ = false;
    vmTurn_ = false;
    cv_.notify_all();
}

Value Vm::interact(Interaction in) {
    in.start = now();
    {
        std::unique_lock<std::mutex> lk(mu_);
        if (abort_) throw AbortSignal{};
        inter_ = std::move(in);
        vmTurn_ = false;
        cv_.notify_all();
        cv_.wait(lk, [&] { return vmTurn_; });
        if (abort_) throw AbortSignal{};
    }
    collectHidden();
    committed = scene;
    Value r = std::move(result_);
    result_ = {};
    return r;
}

// ---- text -------------------------------------------------------------------

std::string Vm::interpolate(const std::string& text) {
    std::string out;
    for (size_t i = 0; i < text.size(); ++i) {
        char c = text[i];
        if (c == '[') {
            if (i + 1 < text.size() && text[i + 1] == '[') {
                out += '[';
                ++i;
                continue;
            }
            size_t e = text.find(']', i);
            if (e == std::string::npos) {
                out += text.substr(i);
                break;
            }
            std::string expr = text.substr(i + 1, e - i - 1);
            if (auto bang = expr.find('!'); bang != std::string::npos) expr.resize(bang);
            try {
                out += py.eval(expr).text();
            } catch (const PyError& err) {
                backend.log("interpolate [" + expr + "]: " + err.what());
            }
            i = e;
            continue;
        }
        out += c;
    }
    return out;
}

std::string Vm::savePersistentText() {
    std::string out;
    if (!persistent.isObj()) return out;
    for (auto& [k, v] : persistent.o->attrs) {
        if (v.isNone()) continue;
        if (v.isObj() && (v.o->fn || v.o->k == Object::K::Instance || v.o->k == Object::K::Class)) continue;
        out += k + " = " + v.repr() + "\n";
    }
    return out;
}

// ---- files ------------------------------------------------------------------

// Game-relative key for a path the scripts build from config.basedir.
static std::string fileKey(Vm& vm, const std::string& name) {
    std::string key = name;
    std::replace(key.begin(), key.end(), '\\', '/');
    const std::string base = vm.py.getAttr(vm.config, "basedir").text();
    if (!base.empty() && key.rfind(base + "/", 0) == 0) key = key.substr(base.size() + 1);
    if (key.rfind("game/", 0) == 0) key = key.substr(5);
    return key;
}

Value Vm::fileObject(const std::string& path, std::string content, bool writable) {
    Value f = makeInstance("file");
    auto data = std::make_shared<std::string>(std::move(content));
    auto pos = std::make_shared<size_t>(0);
    std::vector<Value> lines;
    size_t s = 0;
    while (s < data->size()) {
        size_t e = data->find('\n', s);
        e = e == std::string::npos ? data->size() : e + 1;
        lines.push_back(Value::str(data->substr(s, e - s)));
        s = e;
    }
    f.o->attrs["__lines__"] = makeList(lines);
    f.o->attrs["name"] = Value::str(path);
    f.o->attrs["read"] = makeFunction("read", [data, pos](Interp&, Args& a, Kwargs&) {
        size_t n = a.empty() || a[0].integer() < 0 ? std::string::npos : size_t(a[0].integer());
        std::string r = data->substr(std::min(*pos, data->size()), n);
        *pos += r.size();
        return Value::str(r);
    });
    Vm* vm = this;
    f.o->attrs["write"] = makeFunction("write", [data, path, writable, vm](Interp&, Args& a, Kwargs&) {
        if (!a.empty()) *data += a[0].text();
        if (writable && vm->persistent.isObj()) {
            // Files the game writes (firstrun, the .chr files) live in the
            // persistent data instead of the user's game folder.
            Value vfs = vm->persistent.get("_ddlc_files");
            if (!vfs.isObj()) {
                vfs = makeDict();
                vm->py.setAttr(vm->persistent, "_ddlc_files", vfs);
            }
            vm->py.setItem(vfs, Value::str(fileKey(*vm, path)), Value::str(*data));
        }
        return Value();
    });
    f.o->attrs["close"] = makeFunction("close", [](Interp&, Args&, Kwargs&) { return Value(); });
    f.o->attrs["seek"] = makeFunction("seek", [pos](Interp&, Args& a, Kwargs&) {
        *pos = a.empty() ? 0 : size_t(std::max<int64_t>(0, a[0].integer()));
        return Value();
    });
    return f;
}

std::string Vm::readFile(const std::string& name, bool& ok) {
    ok = true;
    const std::string key = fileKey(*this, name);
    Value vfs = persistent.get("_ddlc_files");
    if (vfs.isObj())
        if (Value* v = vfs.o->findKey(Value::str(key))) return v->text();
    if (assets.has(key)) {
        auto bytes = assets.read(key);
        return std::string(bytes.begin(), bytes.end());
    }
    std::filesystem::path p = std::filesystem::path(assets.dir()) / key;
    std::ifstream in(p, std::ios::binary);
    if (in) return std::string(std::istreambuf_iterator<char>(in), {});
    ok = false;
    return {};
}

// ---- init -------------------------------------------------------------------

void Vm::init(const std::string& persistentText) {
    auto ns = [&](const char* name) {
        Value v = Interp::nameSpace(name);
        py.store[name] = v;
        return v;
    };
    persistent = ns("persistent");
    config = ns("config");
    audio = ns("audio");
    gui = ns("gui");
    styleNs = ns("style");
    ns("build");
    ns("define");

    std::istringstream in(persistentText);
    for (std::string line; std::getline(in, line);) {
        auto eq = line.find(" = ");
        if (eq == std::string::npos) continue;
        try {
            persistent.o->attrs[line.substr(0, eq)] = py.eval(line.substr(eq + 3));
        } catch (const PyError& e) {
            backend.log("persistent " + line.substr(0, eq) + ": " + e.what());
        }
    }

    std::filesystem::path dir(assets.dir());
    std::string base = dir.parent_path().generic_string();
    if (dir.filename().empty()) base = dir.parent_path().parent_path().generic_string();
    auto& c = config.o->attrs;
    c["basedir"] = Value::str(base);
    c["gamedir"] = Value::str(dir.generic_string());
    c["developer"] = Value::boolean(false);
    c["allow_skipping"] = Value::boolean(true);
    c["skipping"] = Value::boolean(false);
    c["window_show_transition"] = transitionValue(dissolve(.2));
    c["window_hide_transition"] = transitionValue(dissolve(.2));
    Value keymap = makeDict();
    for (const char* k : {"game_menu", "hide_windows", "self_voicing", "clipboard_voicing", "toggle_skip", "rollback",
                          "rollforward", "screenshot", "dismiss", "skip", "fast_skip"})
        py.setItem(keymap, Value::str(k), makeList());
    py.getItem(keymap, Value::str("game_menu")).o->items.push_back(Value::str("mouseup_3"));
    c["keymap"] = keymap;

    py.store["_dismiss_pause"] = Value::boolean(true);
    py.store["quick_menu"] = Value::boolean(true);
    py.store["main_menu"] = Value::boolean(false);
    py.store["_window"] = Value::boolean(false);
    py.store["_return"] = Value();

    VmNatives::install(*this);

    size_t failures = 0;
    for (Node* n : script.inits()) {
        try {
            execInit(n);
        } catch (const PyError& e) {
            ++failures;
            std::string src = n->ast.get("code").get("source").text();
            if (src.empty()) src = n->str("varname");
            std::replace(src.begin(), src.end(), '\n', ' ');
            errors.push_back(n->file + ":" + std::to_string(n->line) + ": " + e.what() + " [" + src.substr(0, 90) + "]");
        } catch (const JumpSignal&) {
        }
    }
    if (failures) backend.log(std::to_string(failures) + " init statements failed");

    // Natives replacing helpers of the init code that need real Ren'Py internals.
    Vm* vm = this;
    py.def("pause", [vm](Interp&, Args& a, Kwargs& kw) {
        Value t = argOr(a, 0, kw, "time");
        if (t.isNone() || (t.isNum() && t.num() == 0 && t.t != Value::T::Float)) vm->pause(-1, true);
        else if (t.num() > 0) vm->pause(t.num(), vm->py.lookup("_dismiss_pause").truthy());
        return Value();
    });
    py.def("get_pos", [vm](Interp&, Args& a, Kwargs& kw) {
        return Value::real(vm->backend.position(argOr(a, 0, kw, "channel", Value::str("music")).text()));
    });
    for (const char* noop : {"delete_character", "restore_all_characters", "restore_relevant_characters",
                             "delete_all_saves", "restore_character"})
        py.def(noop, [](Interp&, Args&, Kwargs&) { return Value(); });
}

void Vm::execInit(Node* n) {
    auto source = [&](const char* field) { return n->ast.get(field).get("source").text(); };
    switch (n->k) {
    case Node::K::Python:
    case Node::K::EarlyPython: py.exec(source("code")); break;
    case Node::K::Define:
    case Node::K::Default: {
        const std::string store = n->str("store"), var = n->str("varname");
        Value v = py.eval(source("code"));
        if (store == "store") {
            if (n->k == Node::K::Default && py.store.count(var)) break;
            py.store[var] = v;
            break;
        }
        std::string nsName = store.rfind("store.", 0) == 0 ? store.substr(6) : store;
        Value& target = py.store[nsName];
        if (!target.isObj()) target = Interp::nameSpace(nsName);
        if (n->k == Node::K::Default) {
            const Value* cur = target.attr(var);
            if (cur && !cur->isNone()) break;
        }
        py.setAttr(target, var, v);
        break;
    }
    case Node::K::Image: {
        std::string key;
        for (auto& part : n->ast.get("imgname").items()) key += (key.empty() ? "" : " ") + part.text();
        if (n->ast.get("atl").isObj()) {
            auto t = std::make_shared<AtlTemplate>();
            t->atl = n->ast.get("atl");
            t->name = key;
            images[key] = atlValue(t, this);
        } else images[key] = py.eval(source("code"));
        break;
    }
    case Node::K::Transform: {
        auto t = std::make_shared<AtlTemplate>();
        t->atl = n->ast.get("atl");
        t->name = n->str("varname");
        Value params = n->ast.get("parameters");
        if (params.isObj())
            for (auto& p : params.get("parameters").items()) {
                const auto& kv = p.items();
                if (kv.empty()) continue;
                t->params.emplace_back(kv[0].text(), kv.size() > 1 && !kv[1].isNone() ? kv[1].text() : "");
            }
        const std::string store = n->str("store");
        if (store.empty() || store == "store") py.store[t->name] = atlValue(t, this);
        else py.setAttr(py.store[store.substr(6)], t->name, atlValue(t, this));
        break;
    }
    default: break;
    }
}

// ---- natives ----------------------------------------------------------------

void VmNatives::install(Vm& vm) {
    Interp& py = vm.py;
    Vm* v = &vm;
    auto def = [&](Value ns, const char* name, NativeFn fn) { ns.o->attrs[name] = makeFunction(name, std::move(fn)); };
    auto disp = [v](const Value& x) { return v->toDisp(x); };
    auto size2 = [](const Value& t, double& w, double& h) {
        const auto& it = t.items();
        w = it.size() > 0 ? it[0].num() : 0;
        h = it.size() > 1 ? it[1].num() : 0;
    };

    // Built-in transforms and transitions.
    py.store["left"] = atlValue(placement(0, 0, 1, 1, "left"), v);
    py.store["right"] = atlValue(placement(1, 1, 1, 1, "right"), v);
    py.store["center"] = atlValue(placement(.5, .5, 1, 1, "center"), v);
    py.store["truecenter"] = atlValue(placement(.5, .5, .5, .5, "truecenter"), v);
    py.store["topleft"] = atlValue(placement(0, 0, 0, 0, "topleft"), v);
    py.store["top"] = atlValue(placement(.5, .5, 0, 0, "top"), v);
    py.store["topright"] = atlValue(placement(1, 1, 0, 0, "topright"), v);
    py.store["offscreenleft"] = atlValue(placement(0, 1, 1, 1, "offscreenleft"), v);
    py.store["offscreenright"] = atlValue(placement(1, 0, 1, 1, "offscreenright"), v);
    py.store["default"] = atlValue(placement(.5, .5, 1, 1, "default"), v);
    py.store["dissolve"] = transitionValue(dissolve(.5));

    py.def("_", [](Interp&, Args& a, Kwargs&) { return a.empty() ? Value() : a[0]; });
    py.def("globals", [](Interp&, Args&, Kwargs&) { return makeDict(); });
    py.def("open", [v](Interp&, Args& a, Kwargs& kw) {
        const std::string path = argOr(a, 0, kw, "name").text();
        const std::string mode = argOr(a, 1, kw, "mode", Value::str("r")).text();
        if (mode.find('w') != std::string::npos || mode.find('a') != std::string::npos) {
            Value f = v->fileObject(path, "", true);
            v->py.call(f.get("write"), {Value::str("")});
            return f;
        }
        bool ok = false;
        std::string data = v->readFile(path, ok);
        if (!ok) throw PyError("IOError: " + path);
        return v->fileObject(path, data, false);
    });

    // Characters.
    auto character = [](bool dynamic) {
        return [dynamic](Interp&, Args& a, Kwargs& kw) {
            Value c = makeInstance("Character");
            c.o->attrs["name"] = argOr(a, 0, kw, "name");
            c.o->attrs["dynamic"] = Value::boolean(dynamic);
            for (auto& [k, val] : kw)
                if (k != "name") c.o->attrs[k] = val;
            return c;
        };
    };
    py.def("Character", character(false));
    py.def("DynamicCharacter", character(true));

    // Displayables.
    Value im = Interp::nameSpace("im");
    py.store["im"] = im;
    auto composite = [v, disp, size2](bool live) {
        return [v, disp, size2, live](Interp&, Args& a, Kwargs&) {
            double w = 0, h = 0;
            if (!a.empty()) size2(a[0], w, h);
            std::vector<std::tuple<double, double, DispP>> parts;
            for (size_t i = 1; i + 1 < a.size(); i += 2) {
                double x = 0, y = 0;
                size2(a[i], x, y);
                parts.emplace_back(x, y, disp(a[i + 1]));
            }
            return v->dispValue(makeCompositeDisp(w, h, std::move(parts), live));
        };
    };
    def(im, "Composite", composite(false));
    py.def("LiveComposite", composite(true));
    def(im, "Image", [v, disp](Interp&, Args& a, Kwargs&) { return v->dispValue(disp(a.at(0))); });
    def(im, "Grayscale", [v, disp](Interp&, Args& a, Kwargs&) { return v->dispValue(disp(a.at(0))); });
    def(im, "Flip", [v, disp](Interp&, Args& a, Kwargs&) { return v->dispValue(disp(a.at(0))); });
    py.def("Image", [v, disp](Interp&, Args& a, Kwargs&) { return v->dispValue(disp(a.at(0))); });
    py.def("Frame", [v, disp](Interp&, Args& a, Kwargs&) { return v->dispValue(disp(a.at(0))); });
    py.def("Null", [v](Interp&, Args& a, Kwargs& kw) {
        return v->dispValue(makeNullDisp(argOr(a, 0, kw, "width").num(), argOr(a, 1, kw, "height").num()));
    });
    py.def("AnimatedMask", [v](Interp&, Args&, Kwargs&) { return v->dispValue(makeNullDisp(0, 0)); });
    py.def("ParticleBurst", [v](Interp&, Args&, Kwargs&) { return v->dispValue(makeNullDisp(0, 0)); });
    // Particles are decorative: sprites are tracked but never drawn.
    py.exec("class _KtSprite(object):\n"
            "    x = 0\n"
            "    y = 0\n"
            "    def destroy(self):\n"
            "        pass\n"
            "class SpriteManager(object):\n"
            "    def __init__(self, update=None, event=None, predict=None, ignore_time=False, **kw):\n"
            "        self.children = []\n"
            "    def create(self, d):\n"
            "        s = _KtSprite()\n"
            "        self.children.append(s)\n"
            "        return s\n"
            "    def redraw(self, delay=0):\n"
            "        pass\n");
    py.def("Solid", [v](Interp&, Args& a, Kwargs& kw) {
        return v->dispValue(makeSolidDisp(parseColor(argOr(a, 0, kw, "color").text())));
    });
    py.def("LiveTile", [v, disp](Interp&, Args& a, Kwargs&) { return v->dispValue(makeTileDisp(disp(a.at(0)))); });
    py.def("ConditionSwitch", [v, disp](Interp&, Args& a, Kwargs&) {
        std::vector<std::pair<std::string, DispP>> cases;
        for (size_t i = 0; i + 1 < a.size(); i += 2) cases.emplace_back(a[i].text(), disp(a[i + 1]));
        return v->dispValue(makeSwitchDisp(std::move(cases)));
    });
    py.def("Text", [v](Interp&, Args& a, Kwargs& kw) {
        TextStyle st = v->styleNamed(kwGet(kw, "style", Value::str("default")).text());
        if (const Value* s = kwFind(kw, "size")) st.size = float(s->num());
        if (const Value* c = kwFind(kw, "color")) st.color = parseColor(c->text());
        Value xa = kwGet(kw, "xalign"), ya = kwGet(kw, "yalign");
        return v->dispValue(makeTextDisp(a.empty() ? "" : a[0].text(), st, xa, ya));
    });
    py.def("ParameterizedText", [](Interp&, Args&, Kwargs& kw) {
        Value p = makeInstance("ParameterizedText");
        p.o->attrs["style"] = Value::str("default");
        for (auto& [k, val] : kw) p.o->attrs[k] = val;
        return p;
    });
    py.def("Borders", [](Interp&, Args& a, Kwargs&) {
        Value b = makeInstance("Borders");
        b.o->items = a;
        return b;
    });
    py.def("Transform", [v, disp](Interp&, Args& a, Kwargs& kw) {
        auto t = std::make_shared<AtlTemplate>();
        t->name = "Transform";
        if (!a.empty()) t->child = disp(a[0]);
        for (auto& [k, val] : kw) {
            if (k == "child") t->child = disp(val);
            else t->props.emplace_back(k, val);
        }
        return atlValue(t, v);
    });

    // Transitions.
    py.def("Dissolve", [](Interp&, Args& a, Kwargs& kw) {
        return transitionValue(dissolve(argOr(a, 0, kw, "time").num(), kwGet(kw, "alpha").truthy()));
    });
    py.def("Pause", [](Interp&, Args& a, Kwargs& kw) {
        auto t = std::make_shared<Transition>();
        t->k = Transition::K::Pause;
        t->time = argOr(a, 0, kw, "delay").num();
        return transitionValue(t);
    });
    py.def("ImageDissolve", [](Interp&, Args& a, Kwargs& kw) {
        auto t = std::make_shared<Transition>();
        t->k = Transition::K::ImageDissolve;
        t->mask = argOr(a, 0, kw, "image").text();
        t->time = argOr(a, 1, kw, "time").num();
        t->ramp = argOr(a, 2, kw, "ramplen", Value::integer(8)).num();
        t->reverse = kwGet(kw, "reverse").truthy();
        return transitionValue(t);
    });
    auto multiple = [v](const std::vector<Value>& items) {
        auto t = std::make_shared<Transition>();
        t->k = Transition::K::Multiple;
        for (size_t i = 0; i < items.size(); ++i) {
            const Value& it = items[i];
            if (i % 2 == 1) {
                t->steps.push_back(v->toTransition(it));
                continue;
            }
            Transition::SceneRef r;
            if (it.t == Value::T::Bool) r.kind = it.b ? 1 : 0;
            else {
                r.kind = 2;
                r.disp = v->toDisp(it);
            }
            t->scenes.push_back(r);
        }
        return t;
    };
    py.def("MultipleTransition", [multiple](Interp&, Args& a, Kwargs&) {
        return transitionValue(multiple(a.empty() ? std::vector<Value>{} : a[0].items()));
    });
    py.def("Fade", [v, multiple](Interp&, Args& a, Kwargs& kw) {
        const double out = argOr(a, 0, kw, "out_time").num(), hold = argOr(a, 1, kw, "hold_time").num(),
                     in = argOr(a, 2, kw, "in_time").num();
        Value color = v->dispValue(makeSolidDisp(parseColor(kwGet(kw, "color", Value::str("#000")).text())));
        auto pauseT = std::make_shared<Transition>();
        pauseT->k = Transition::K::Pause;
        pauseT->time = hold;
        return transitionValue(multiple({Value::boolean(false), transitionValue(dissolve(out)), color,
                                         transitionValue(pauseT), color, transitionValue(dissolve(in)),
                                         Value::boolean(true)}));
    });
    py.def("Return", [](Interp&, Args& a, Kwargs&) {
        Value r = makeInstance("Return");
        r.o->attrs["value"] = a.empty() ? Value::boolean(true) : a[0];
        return r;
    });
    py.def("__screenargs", [](Interp&, Args& a, Kwargs& kw) {
        Value d = makeDict();
        for (auto& [k, val] : kw) d.o->dict.emplace_back(Value::str(k), val);
        return makeTuple({makeList(a), d});
    });

    // The renpy module.
    Value renpy = Interp::nameSpace("renpy");
    py.store["renpy"] = renpy;
    py.builtins["renpy"] = renpy;
    auto sub = [&](Value parent, const char* name) {
        Value n = Interp::nameSpace(name);
        parent.o->attrs[name] = n;
        return n;
    };
#if defined(__APPLE__)
    const bool mac = true;
#else
    const bool mac = false;
#endif
    renpy.o->attrs["windows"] = Value::boolean(false);
    renpy.o->attrs["macintosh"] = Value::boolean(mac);
    renpy.o->attrs["linux"] = Value::boolean(!mac);
    renpy.o->attrs["android"] = Value::boolean(false);
    renpy.o->attrs["ios"] = Value::boolean(false);
    renpy.o->attrs["mobile"] = Value::boolean(false);
    renpy.o->attrs["Displayable"] = Value::object(makeObj(Object::K::Class, "Displayable"));
    if (py.builtins.count("random")) renpy.o->attrs["random"] = py.builtins["random"];
    Value game = sub(renpy, "game");
    Value prefs = sub(game, "preferences");
    prefs.o->attrs["fullscreen"] = Value::boolean(false);
    prefs.o->attrs["afm_enable"] = Value::boolean(false);
    game.o->attrs["persistent"] = vm.persistent;
    Value loadsave = sub(renpy, "loadsave");
    Value location = sub(loadsave, "location");
    location.o->attrs["locations"] = makeList({Value::integer(0)});
    def(location, "unlink_persistent", [v](Interp&, Args&, Kwargs&) {
        v->persistent.o->attrs.clear();
        return Value();
    });
    Value rpersist = sub(renpy, "persistent");
    rpersist.o->attrs["should_save_persistent"] = Value::boolean(true);
    Value context = Interp::nameSpace("context");
    def(renpy, "context", [context](Interp&, Args&, Kwargs&) { return context; });

    auto audioSpec = [](const Value& f) -> std::string {
        Value x = f.isObj() && !f.items().empty() ? f.items()[0] : f;
        return x.text();
    };
    Value music = sub(renpy, "music");
    def(music, "play", [v, audioSpec](Interp&, Args& a, Kwargs& kw) {
        const std::string ch = argOr(a, 1, kw, "channel", Value::str("music")).text();
        Value loop = kwGet(kw, "loop");
        if (const Value* ic = kwFind(kw, "if_changed"); ic && ic->truthy() && v->backend.playing(ch) == audioSpec(a.at(0)))
            return Value();
        v->backend.play(ch, audioSpec(argOr(a, 0, kw, "filenames")), kwGet(kw, "fadein").num(),
                        loop.isNone() ? ch != "sound" : loop.truthy());
        return Value();
    });
    def(music, "queue", [v, audioSpec](Interp&, Args& a, Kwargs& kw) {
        const std::string ch = argOr(a, 1, kw, "channel", Value::str("music")).text();
        Value loop = kwGet(kw, "loop");
        v->backend.queue(ch, audioSpec(argOr(a, 0, kw, "filenames")), loop.isNone() ? ch != "sound" : loop.truthy());
        return Value();
    });
    def(music, "stop", [v](Interp&, Args& a, Kwargs& kw) {
        v->backend.stop(argOr(a, 0, kw, "channel", Value::str("music")).text(), kwGet(kw, "fadeout").num());
        return Value();
    });
    def(music, "get_playing", [v](Interp&, Args& a, Kwargs& kw) {
        std::string p = v->backend.playing(argOr(a, 0, kw, "channel", Value::str("music")).text());
        return p.empty() ? Value() : Value::str(p);
    });
    def(music, "get_pos", [v](Interp&, Args& a, Kwargs& kw) {
        const std::string ch = argOr(a, 0, kw, "channel", Value::str("music")).text();
        if (v->backend.playing(ch).empty()) return Value();
        return Value::real(v->backend.position(ch));
    });
    def(music, "register_channel", [](Interp&, Args&, Kwargs&) { return Value(); });
    def(music, "set_volume", [](Interp&, Args&, Kwargs&) { return Value(); });
    Value sound = sub(renpy, "sound");
    sound.o->attrs = music.o->attrs;
    def(sound, "play", [v, audioSpec](Interp&, Args& a, Kwargs& kw) {
        v->backend.play(argOr(a, 1, kw, "channel", Value::str("sound")).text(), audioSpec(a.at(0)), 0, false);
        return Value();
    });
    def(renpy, "play", [v, audioSpec](Interp&, Args& a, Kwargs& kw) {
        if (a.empty() || a[0].isNone()) return Value();
        v->backend.play(argOr(a, 1, kw, "channel", Value::str("sound")).text(), audioSpec(a[0]), 0, false);
        return Value();
    });

    auto nameOf = [](const Value& n) {
        if (n.isStr()) return splitWords(n.s);
        std::vector<std::string> out;
        for (auto& p : n.items()) out.push_back(p.text());
        return out;
    };
    def(renpy, "show", [v, nameOf](Interp&, Args& a, Kwargs& kw) {
        std::vector<std::string> name = nameOf(a.at(0));
        if (name.empty()) return Value();
        std::vector<Value> at;
        for (auto& x : kwGet(kw, "at_list").items()) at.push_back(x);
        Value what = kwGet(kw, "what");
        DispP d = what.isNone() ? v->imageNamed(name) : v->toDisp(what);
        if (!d) {
            v->backend.log("renpy.show: unknown image " + a[0].text());
            d = makeNullDisp(0, 0);
        }
        Value tag = kwGet(kw, "tag");
        Value z = kwGet(kw, "zorder");
        std::string behind;
        for (auto& b : kwGet(kw, "behind").items()) behind = b.text();
        v->showDisp(tag.isNone() ? name[0] : tag.text(), name, d, at, behind, int(z.integer()), !z.isNone());
        return Value();
    });
    def(renpy, "hide", [v, nameOf](Interp&, Args& a, Kwargs&) {
        auto name = nameOf(a.at(0));
        if (!name.empty()) v->hideTag(name[0]);
        return Value();
    });
    def(renpy, "scene", [v](Interp&, Args&, Kwargs&) {
        v->scene.master.clear();
        return Value();
    });
    def(renpy, "showing", [v, nameOf](Interp&, Args& a, Kwargs&) {
        auto name = nameOf(a.at(0));
        for (auto& s : v->scene.master)
            if (!s.hiding && !name.empty() && s.tag == name[0]) return Value::boolean(true);
        return Value::boolean(false);
    });
    def(renpy, "pause", [v](Interp&, Args& a, Kwargs& kw) {
        Value t = argOr(a, 0, kw, "delay");
        const bool hard = kwGet(kw, "hard").truthy();
        if (t.isNone()) v->pause(-1, true);
        else if (t.num() > 0) v->pause(t.num(), !hard && v->py.lookup("_dismiss_pause").truthy());
        return Value::boolean(false);
    });
    def(renpy, "with_statement", [v](Interp&, Args& a, Kwargs&) {
        if (auto t = a.empty() ? nullptr : v->toTransition(a[0])) v->transition(t);
        else v->committed = v->scene;
        return Value();
    });
    def(renpy, "jump", [](Interp&, Args& a, Kwargs&) -> Value { throw JumpSignal{a.at(0).text(), false}; });
    def(renpy, "call", [](Interp&, Args& a, Kwargs&) -> Value { throw JumpSignal{a.at(0).text(), true}; });
    def(renpy, "quit", [](Interp&, Args&, Kwargs&) -> Value { throw QuitSignal{}; });
    def(renpy, "utter_restart", [](Interp&, Args&, Kwargs&) -> Value { throw RestartSignal{}; });
    def(renpy, "full_restart", [](Interp&, Args&, Kwargs&) -> Value { throw RestartSignal{}; });
    def(renpy, "save_persistent", [v](Interp&, Args&, Kwargs&) {
        v->backend.savePersistent(v->savePersistentText());
        return Value();
    });
    def(renpy, "say", [v](Interp&, Args& a, Kwargs&) {
        v->say(a.at(0), a.at(1).text(), Value());
        return Value();
    });
    def(renpy, "display_menu", [v](Interp&, Args& a, Kwargs& kw) {
        std::vector<Interaction::Item> items;
        std::string caption;
        for (auto& it : a.at(0).items()) {
            const auto& p = it.items();
            if (p.size() < 2) continue;
            if (p[1].isNone()) caption = p[0].text();
            else items.push_back({v->interpolate(p[0].text()), p[1]});
        }
        return v->menu(items, caption, kwGet(kw, "screen", Value::str("choice")).text());
    });
    def(renpy, "call_screen", [v](Interp&, Args& a, Kwargs& kw) {
        Args rest(a.begin() + (a.empty() ? 0 : 1), a.end());
        return v->callScreen(a.at(0).text(), rest, kw);
    });
    def(renpy, "file", [v](Interp&, Args& a, Kwargs&) {
        bool ok = false;
        std::string data = v->readFile(a.at(0).text(), ok);
        if (!ok) throw PyError("IOError: " + a.at(0).text());
        return v->fileObject(a.at(0).text(), data, false);
    });
    def(renpy, "exists", [v](Interp&, Args& a, Kwargs&) {
        bool ok = false;
        v->readFile(a.at(0).text(), ok);
        return Value::boolean(ok);
    });
    def(renpy, "get_return_stack", [v](Interp&, Args&, Kwargs&) {
        std::vector<Value> out(v->callStack_.size(), Value::str("return"));
        return makeList(out);
    });
    def(renpy, "pop_call", [v](Interp&, Args&, Kwargs&) {
        if (!v->callStack_.empty()) v->callStack_.pop_back();
        return Value();
    });
    def(renpy, "error", [v](Interp&, Args& a, Kwargs&) {
        v->backend.log("renpy.error: " + (a.empty() ? std::string() : a[0].text()));
        return Value();
    });
    def(renpy, "displayable", [v](Interp&, Args& a, Kwargs&) { return v->dispValue(v->toDisp(a.at(0))); });
    def(renpy, "variant", [](Interp&, Args& a, Kwargs&) {
        const std::string n = a.empty() ? "" : a[0].text();
        return Value::boolean(n == "pc" || n == "large");
    });
    Value singleton = Interp::nameSpace("singleton");
    def(singleton, "SingleInstance", [](Interp&, Args&, Kwargs&) { return makeInstance("SingleInstance"); });
    py.builtins["singleton"] = singleton;
    def(vm.gui, "init", [](Interp&, Args&, Kwargs&) { return Value(); });
    Value build = py.store["build"];
    for (const char* noop : {"archive", "classify", "documentation", "executable", "package", "clear"})
        def(build, noop, [](Interp&, Args&, Kwargs&) { return Value(); });
    def(renpy, "list_saved_games", [](Interp&, Args&, Kwargs&) { return makeList(); });
    for (const char* noop : {"block_rollback", "unlink_save", "restart_interaction", "notify", "free_memory",
                             "checkpoint", "predict", "take_screenshot", "clear_game_runtime"})
        def(renpy, noop, [](Interp&, Args&, Kwargs&) { return Value(); });
    def(renpy, "transition", [v](Interp&, Args& a, Kwargs&) {
        if (auto t = a.empty() ? nullptr : v->toTransition(a[0])) v->transition(t);
        return Value();
    });

    // ui: the immediate-mode widgets of the poem game.
    Value ui = Interp::nameSpace("ui");
    py.store["ui"] = ui;
    renpy.o->attrs["ui"] = ui;
    def(ui, "text", [v](Interp&, Args& a, Kwargs& kw) {
        Interaction::Item t;
        t.text = a.empty() ? "" : a[0].text();
        t.value = kwGet(kw, "style", Value::str("default"));
        t.x = float(kwGet(kw, "xpos").num());
        t.y = float(kwGet(kw, "ypos").num());
        v->uiTexts_.emplace_back(t.text, makeTuple({t.value, Value::real(t.x), Value::real(t.y)}));
        return Value();
    });
    def(ui, "textbutton", [v](Interp&, Args& a, Kwargs& kw) {
        Interaction::Item b;
        b.text = a.empty() ? "" : a[0].text();
        b.value = kwGet(kw, "clicked");
        b.x = float(kwGet(kw, "xpos").num());
        b.y = float(kwGet(kw, "ypos").num());
        v->uiButtons_.push_back(b);
        return Value();
    });
    def(ui, "returns", [](Interp&, Args& a, Kwargs&) { return a.empty() ? Value() : a[0]; });
    for (const char* noop : {"vbox", "hbox", "close", "fixed", "saybehavior", "add", "null"})
        def(ui, noop, [](Interp&, Args&, Kwargs&) { return Value(); });
    def(ui, "interact", [v](Interp&, Args&, Kwargs& kw) {
        if (v->uiButtons_.empty() && v->uiTexts_.empty()) {
            v->pause(-1, true);
            return Value();
        }
        Interaction in;
        in.k = Interaction::K::Ui;
        in.screen = "poemgame";
        in.items = std::move(v->uiButtons_);
        for (auto& [text, info] : v->uiTexts_) {
            Interaction::Item t;
            t.text = text;
            t.value = info.items()[0];
            t.x = float(info.items()[1].num());
            t.y = float(info.items()[2].num());
            in.texts.push_back(t);
        }
        v->uiButtons_.clear();
        v->uiTexts_.clear();
        Value r = v->interact(std::move(in));
        return r;
    });

    // datetime follows the VM clock (headless hosts fake it).
    if (py.builtins.count("datetime")) {
        Value dt = py.builtins["datetime"].get("datetime");
        if (dt.isObj())
            dt.o->attrs["now"] = makeFunction("now", [v](Interp&, Args&, Kwargs&) {
                Value d = makeInstance("datetime");
                d.o->attrs["t"] = Value::real(v->now());
                return d;
            });
    }
}

// ---- statements -------------------------------------------------------------

void Vm::runFrom(Node* n) {
    while (n) {
        ++statements;
        try {
            n = exec(n);
        } catch (const JumpSignal& j) {
            Node* target = script.label(j.label);
            if (!target) {
                errors.push_back("missing label " + j.label);
                backend.log(errors.back());
                n = n->next;
                continue;
            }
            if (j.call) callStack_.push_back(n->next);
            n = target;
        }
    }
}

void Vm::bindLabel(Node* label, const Value&) {
    Value params = label->ast.get("parameters");
    if (!params.isObj()) {
        pendingArgs_.clear();
        pendingKw_.clear();
        return;
    }
    size_t pi = 0;
    for (auto& p : params.get("parameters").items()) {
        const auto& kv = p.items();
        if (kv.empty()) continue;
        const std::string name = kv[0].text();
        Value val;
        bool set = false;
        if (const Value* k = kwFind(pendingKw_, name.c_str())) val = *k, set = true;
        else if (pi < pendingArgs_.size()) val = pendingArgs_[pi++], set = true;
        if (!set && kv.size() > 1 && !kv[1].isNone()) val = py.eval(kv[1].text());
        py.store[name] = val;
    }
    pendingArgs_.clear();
    pendingKw_.clear();
}

Node* Vm::exec(Node* n) {
    auto source = [&](const char* field) { return n->ast.get(field).get("source").text(); };
    switch (n->k) {
    case Node::K::Label: bindLabel(n, {}); return n->next;
    case Node::K::Say: {
        Value who = n->ast.get("who");
        Value whoV;
        if (!who.isNone()) {
            try {
                whoV = py.eval(who.text());
            } catch (const PyError&) {
                whoV = Value::str(who.text());
            }
        }
        say(whoV, n->str("what"), n->ast.get("attributes"));
        if (!n->ast.get("with_").isNone()) doWith(n->ast.get("with_").text());
        return n->next;
    }
    case Node::K::Show: show(n->ast.get("imspec"), n->ast.get("atl"), false); return n->next;
    case Node::K::Scene: show(n->ast.get("imspec"), n->ast.get("atl"), true); return n->next;
    case Node::K::Hide: hide(n->ast.get("imspec")); return n->next;
    case Node::K::With: doWith(n->str("expr")); return n->next;
    case Node::K::Jump: {
        std::string target = n->str("target");
        if (n->ast.get("expression").truthy()) target = py.eval(target).text();
        Node* l = script.label(target);
        if (!l) {
            errors.push_back("missing label " + target);
            backend.log(errors.back());
        }
        return l;
    }
    case Node::K::Call: {
        std::string target = n->str("label");
        if (n->ast.get("expression").truthy()) target = py.eval(target).text();
        pendingArgs_.clear();
        pendingKw_.clear();
        Value args = n->ast.get("arguments");
        if (args.isObj())
            for (auto& a : args.get("arguments").items()) {
                const auto& kv = a.items();
                if (kv.size() < 2) continue;
                Value val = py.eval(kv[1].text());
                if (kv[0].isNone()) pendingArgs_.push_back(val);
                else pendingKw_.emplace_back(kv[0].text(), val);
            }
        Node* l = script.label(target);
        if (!l) {
            errors.push_back("missing label " + target);
            backend.log(errors.back());
            return n->next;
        }
        callStack_.push_back(n->next);
        return l;
    }
    case Node::K::Return: {
        Value e = n->ast.get("expression");
        py.store["_return"] = e.isNone() ? Value() : py.eval(e.text());
        if (callStack_.empty()) return nullptr;
        Node* r = callStack_.back();
        callStack_.pop_back();
        return r;
    }
    case Node::K::Python: {
        try {
            py.exec(source("code"));
        } catch (const PyError& e) {
            errors.push_back(n->file + ":" + std::to_string(n->line) + ": " + e.what());
            backend.log(errors.back());
        }
        return n->next;
    }
    case Node::K::If: {
        const auto& entries = n->ast.get("entries").items();
        for (size_t i = 0; i < entries.size() && i < n->blocks.size(); ++i) {
            const std::string cond = entries[i].items().empty() ? "True" : entries[i].items()[0].text();
            bool ok = false;
            try {
                ok = py.eval(cond).truthy();
            } catch (const PyError& e) {
                errors.push_back(n->file + ":" + std::to_string(n->line) + ": " + e.what());
                backend.log(errors.back());
            }
            if (ok) return n->blocks[i].empty() ? n->next : n->blocks[i][0];
        }
        return n->next;
    }
    case Node::K::Menu: {
        const auto& items = n->ast.get("items").items();
        std::vector<Interaction::Item> choices;
        std::vector<size_t> index;
        std::string caption;
        for (size_t i = 0; i < items.size(); ++i) {
            const auto& it = items[i].items();
            if (it.size() < 3) continue;
            if (it[2].isNone()) {
                caption = it[0].text();
                continue;
            }
            bool ok = true;
            try {
                ok = py.eval(it[1].text()).truthy();
            } catch (const PyError&) {
            }
            if (!ok) continue;
            choices.push_back({interpolate(it[0].text()), Value::integer(int64_t(i))});
            index.push_back(i);
        }
        Value r = menu(choices, caption, "choice");
        const size_t i = size_t(r.integer());
        if (i < n->blocks.size() && !n->blocks[i].empty()) return n->blocks[i][0];
        return n->next;
    }
    case Node::K::User: execUser(n->str("line")); return n->next;
    default: return n->next;
    }
}

void Vm::execUser(const std::string& line) {
    auto t = tokens(line);
    if (t.empty()) return;
    const std::string& verb = t[0];
    auto audioName = [&](const std::string& tok) -> std::string {
        if (!tok.empty() && (std::isalpha((unsigned char)tok[0]) || tok[0] == '_') &&
            tok.find_first_of("()[]+") == std::string::npos) {
            if (const Value* a = audio.attr(tok)) return a->text();
        }
        try {
            Value v = py.eval(tok);
            if (v.isObj() && !v.items().empty()) v = v.items()[0];
            return v.text();
        } catch (const PyError& e) {
            backend.log("audio '" + tok + "': " + e.what());
            return {};
        }
    };
    auto option = [&](const char* name, size_t from) -> Value {
        for (size_t i = from; i + 1 < t.size(); ++i)
            if (t[i] == name) return py.eval(t[i + 1]);
        return {};
    };
    auto flag = [&](const char* name) { return std::find(t.begin(), t.end(), name) != t.end(); };

    if ((verb == "play" || verb == "queue") && t.size() >= 3) {
        const std::string ch = t[1];
        const std::string spec = audioName(t[2]);
        if (spec.empty()) return;
        bool loop = ch != "sound";
        if (flag("loop")) loop = true;
        if (flag("noloop")) loop = false;
        if (verb == "queue") backend.queue(ch, spec, loop);
        else {
            if (flag("if_changed") && backend.playing(ch) == spec) return;
            backend.play(ch, spec, option("fadein", 3).num(), loop);
        }
    } else if (verb == "stop" && t.size() >= 2) {
        backend.stop(t[1], option("fadeout", 2).num());
    } else if (verb == "window" && t.size() >= 2) {
        std::string mode = t[1], arg;
        if (auto p = mode.find('('); p != std::string::npos) {
            arg = mode.substr(p);
            mode.resize(p);
        } else if (t.size() >= 3) arg = t[2];
        if (mode == "auto") {
            windowAuto_ = true;
            return;
        }
        windowAuto_ = false;
        const bool show = mode == "show";
        if (scene.window.visible == show) return;
        Value trans = py.getAttr(config, show ? "window_show_transition" : "window_hide_transition");
        if (!arg.empty()) trans = py.eval(arg);
        scene.window.visible = show;
        scene.window.who.clear();
        scene.window.what.clear();
        scene.window.rich = {};
        if (auto tr = toTransition(trans)) transition(tr);
    } else if (verb == "pause") {
        if (t.size() >= 2) pause(py.eval(trim(line.substr(5))).num(), py.lookup("_dismiss_pause").truthy());
        else pause(-1, true);
    } else if ((verb == "show" || verb == "hide" || verb == "call") && t.size() >= 3 && t[1] == "screen") {
        std::string rest = trim(line.substr(line.find("screen") + 6));
        std::string name = rest, argSrc;
        if (auto p = rest.find('('); p != std::string::npos) {
            name = trim(rest.substr(0, p));
            argSrc = rest.substr(p);
        } else name = splitWords(rest)[0];
        Args args;
        Kwargs kw;
        if (!argSrc.empty()) {
            Value packed = py.eval("__screenargs" + argSrc);
            args = packed.items()[0].items();
            for (auto& [k, val] : packed.items()[1].o->dict) kw.emplace_back(k.text(), val);
        }
        if (verb == "hide") {
            auto& s = scene.screens;
            s.erase(std::remove_if(s.begin(), s.end(), [&](const ScreenState& x) { return x.name == name; }), s.end());
        } else if (verb == "show") {
            auto& s = scene.screens;
            s.erase(std::remove_if(s.begin(), s.end(), [&](const ScreenState& x) { return x.name == name; }), s.end());
            s.push_back({name, args, kw, now()});
        } else py.store["_return"] = callScreen(name, args, kw);
    } else backend.log("unhandled statement: " + line);
}

// ---- say / menu / screens ---------------------------------------------------

void Vm::say(const Value& who, const std::string& what, const Value& attrs) {
    std::string name, prefix, suffix, image;
    if (who.cls() == "Character") {
        Value n = who.get("name");
        if (who.get("dynamic").truthy() && !n.isNone()) {
            try {
                n = py.eval(n.text());
            } catch (const PyError&) {
            }
        }
        if (!n.isNone()) name = n.text();
        prefix = who.get("what_prefix").isNone() ? "" : who.get("what_prefix").text();
        suffix = who.get("what_suffix").isNone() ? "" : who.get("what_suffix").text();
        image = who.get("image").isNone() ? "" : who.get("image").text();
    } else if (!who.isNone()) name = who.text();

    // Say attributes re-show the speaker's image keeping its transform.
    if (!image.empty() && attrs.isObj() && !attrs.items().empty()) {
        std::vector<std::string> img{image};
        for (auto& a : attrs.items()) img.push_back(a.text());
        bool showing = false;
        for (auto& s : scene.master)
            if (s.tag == image && !s.hiding) showing = true;
        if (showing) {
            if (DispP d = imageNamed(img)) showDisp(image, img, d, {}, "", 0, false);
            else backend.log("say attributes: unknown image " + image);
        }
    }

    std::string text = interpolate(prefix + what + suffix);
    Value filter = py.getAttr(config, "replace_text");
    if (filter.isObj() && filter.o->fn) {
        try {
            text = py.call(filter, {Value::str(text)}).text();
        } catch (const PyError&) {
        }
    }
    name = interpolate(name);

    if (windowAuto_ && !scene.window.visible) {
        scene.window.visible = true;
        scene.window.who.clear();
        scene.window.what.clear();
        scene.window.rich = {};
        if (auto tr = toTransition(py.getAttr(config, "window_show_transition"))) transition(tr);
    }
    Window& w = scene.window;
    w.visible = true;
    w.who = name;
    w.what = text;
    w.rich = parseRich(text, textCps);
    size_t from = 0;
    auto segment = [&](size_t to, int kind, double time) {
        scene.window.from = from;
        scene.window.to = to;
        scene.window.t0 = now();
        Interaction in;
        in.k = Interaction::K::Say;
        in.sayWait = kind;
        in.sayTime = time;
        interact(std::move(in));
        from = to;
    };
    const std::vector<RichText::Wait> waits = w.rich.waits;
    const size_t total = w.rich.glyphs.size();
    for (auto& wt : waits) segment(wt.at, wt.kind, wt.time);
    const bool endsNoWait = !waits.empty() && waits.back().at >= total && waits.back().kind == 2;
    const bool endsClick = !waits.empty() && waits.back().at >= total && waits.back().kind == 0;
    if (!endsNoWait && !endsClick) segment(total, 0, 0);
    // The window stays up (empty) until the next line.
    if (endsNoWait) return;
    scene.window.who.clear();
    scene.window.what.clear();
    scene.window.rich = {};
    committed = scene;
}

Value Vm::menu(const std::vector<Interaction::Item>& items, const std::string& caption, const std::string& screen) {
    const bool wasVisible = scene.window.visible;
    if (!caption.empty()) {
        std::string text = interpolate(caption);
        scene.window.visible = true;
        scene.window.who.clear();
        scene.window.what = text;
        scene.window.rich = parseRich(text, 1e9);
        scene.window.from = 0;
        scene.window.to = scene.window.rich.glyphs.size();
        scene.window.t0 = now();
    }
    Interaction in;
    in.k = Interaction::K::Menu;
    in.items = items;
    in.screen = screen;
    in.dismiss = false;
    Value r = interact(std::move(in));
    scene.window.what.clear();
    scene.window.rich = {};
    // The caption only borrows the window; later pauses must not show it empty.
    if (windowAuto_) scene.window.visible = wasVisible;
    const size_t i = size_t(std::max<int64_t>(0, r.integer()));
    committed = scene;
    return i < items.size() ? items[i].value : Value();
}

Value Vm::callScreen(const std::string& name, Args args, Kwargs kw) {
    Interaction in;
    in.k = Interaction::K::Screen;
    in.screen = name;
    in.dismiss = false;
    Value msg = args.empty() ? kwGet(kw, "message") : args[0];
    in.message = msg.isNone() ? "" : interpolate(msg.text());
    in.args = std::move(args);
    in.kw = std::move(kw);
    Value r = interact(std::move(in));
    if (r.cls() == "Return") return r.get("value");
    return r;
}

// ---- images -----------------------------------------------------------------

void Vm::show(const Value& imspec, const Value& atl, bool sceneStmt) {
    if (sceneStmt) {
        scene.master.clear();
        if (windowAuto_ && scene.window.visible) scene.window.visible = false;
    }
    if (!imspec.isObj()) return;
    const auto& is = imspec.items();
    if (is.size() < 4) return;
    std::vector<std::string> name;
    for (auto& p : is[0].items()) name.push_back(p.text());
    const Value& expr = is[1];
    std::string tag = is[2].isNone() ? "" : is[2].text();
    DispP d;
    if (!expr.isNone()) {
        try {
            d = toDisp(py.eval(expr.text()));
        } catch (const PyError& e) {
            backend.log("show expression: " + std::string(e.what()));
        }
        if (tag.empty()) tag = expr.text();
        if (name.empty()) name = {tag};
    } else {
        d = imageNamed(name);
        if (!d && !name.empty()) {
            // Ren'Py keeps the attributes of a showing image ("show monika at t11").
            for (auto& s : scene.master)
                if (s.tag == name[0] && !s.hiding && name.size() == 1) {
                    d = imageNamed(s.name);
                    name = s.name;
                }
        }
        if (tag.empty() && !name.empty()) tag = name[0];
    }
    if (!d) {
        std::string key;
        for (auto& p : name) key += p + " ";
        backend.log("unknown image: " + key);
        d = makeNullDisp(0, 0);
    }
    std::vector<Value> at;
    for (auto& a : is[3].items()) {
        try {
            at.push_back(py.eval(a.text()));
        } catch (const PyError& e) {
            backend.log("at " + a.text() + ": " + e.what());
        }
    }
    if (atl.isObj()) {
        auto t = std::make_shared<AtlTemplate>();
        t->atl = atl;
        t->name = tag;
        at.push_back(atlValue(t, this));
    }
    const Value zorder = is.size() > 5 ? is[5] : Value();
    std::string behind;
    if (is.size() > 6)
        for (auto& b : is[6].items()) behind = b.text();
    int z = 0;
    if (!zorder.isNone()) z = int(py.eval(zorder.text()).integer());
    showDisp(tag, name, d, at, behind, z, !zorder.isNone());
}

void Vm::showDisp(const std::string& tag, const std::vector<std::string>& name, const DispP& disp,
                  const std::vector<Value>& atList, const std::string& behind, int zorder, bool hasZorder) {
    auto& m = scene.master;
    auto it = std::find_if(m.begin(), m.end(), [&](const Shown& s) { return s.tag == tag; });
    if (it != m.end() && it->hiding) {
        m.erase(it);
        it = m.end();
    }
    const double t = now();
    std::vector<std::shared_ptr<AtlTemplate>> templs;
    for (auto& v : atList)
        if (v.cls() == "ATLTransform" && v.o->payload) templs.push_back(std::static_pointer_cast<AtlTemplate>(v.o->payload));

    if (templs.empty() && it != m.end() && it->inner) {
        it->inner->setChild(disp->instance(), std::max(0.0, t - it->shownAt));
        it->name = name;
        if (hasZorder) it->zorder = zorder;
        return;
    }

    Shown s;
    s.tag = tag;
    s.name = name;
    s.shownAt = t;
    s.zorder = hasZorder ? zorder : (it != m.end() ? it->zorder : 0);
    if (templs.empty()) {
        s.xf = std::make_shared<TransformDisp>(nullptr);
        s.xf->defaultPlacement = true;
        s.xf->setChild(disp->instance(), 0);
        s.inner = s.xf;
    } else {
        DispP child = disp->instance();
        for (auto& tp : templs) {
            auto x = std::make_shared<TransformDisp>(tp);
            x->setChild(child, 0);
            if (!s.inner) s.inner = x;
            child = x;
        }
        s.xf = std::static_pointer_cast<TransformDisp>(child);
    }
    const bool replacing = it != m.end();
    if (replacing && it->xf) s.xf->takeState(*it->xf);
    forChain(s.xf, [&](TransformDisp& x) { x.event(replacing ? "replace" : "show"); });

    if (replacing) {
        *it = std::move(s);
        return;
    }
    auto pos = m.end();
    if (!behind.empty()) {
        pos = std::find_if(m.begin(), m.end(), [&](const Shown& x) { return x.tag == behind; });
    } else {
        pos = std::find_if(m.begin(), m.end(), [&](const Shown& x) { return x.zorder > s.zorder; });
    }
    m.insert(pos, std::move(s));
}

void Vm::hide(const Value& imspec) {
    if (!imspec.isObj() || imspec.items().size() < 3) return;
    const auto& is = imspec.items();
    std::string tag = is[2].isNone() ? "" : is[2].text();
    if (tag.empty()) {
        if (!is[0].items().empty()) tag = is[0].items()[0].text();
        else if (!is[1].isNone()) tag = is[1].text();
    }
    hideTag(tag);
}

void Vm::hideTag(const std::string& tag) {
    auto& m = scene.master;
    auto it = std::find_if(m.begin(), m.end(), [&](const Shown& s) { return s.tag == tag && !s.hiding; });
    if (it == m.end()) return;
    bool handles = false;
    forChain(it->xf, [&](TransformDisp& x) { handles = handles || x.handles("hide"); });
    if (!handles) {
        m.erase(it);
        return;
    }
    it->hiding = true;
    forChain(it->xf, [&](TransformDisp& x) { x.event("hide"); });
}

// ---- transitions / pauses ---------------------------------------------------

void Vm::doWith(const std::string& expr) {
    if (expr.empty() || expr == "None") {
        committed = scene;
        return;
    }
    std::shared_ptr<Transition> t;
    try {
        t = toTransition(py.eval(expr));
    } catch (const PyError& e) {
        backend.log("with " + expr + ": " + e.what());
    }
    if (t) transition(t);
    else committed = scene;
}

void Vm::transition(const std::shared_ptr<Transition>& t) {
    Interaction in;
    in.k = Interaction::K::Transition;
    in.trans = t;
    in.from = committed;
    const double d = t->duration();
    in.deadline = d;
    in.dismiss = py.lookup("_dismiss_pause").truthy();
    if (d <= 0) {
        committed = scene;
        return;
    }
    interact(std::move(in));
}

void Vm::pause(double seconds, bool dismiss) {
    Interaction in;
    in.k = Interaction::K::Pause;
    in.deadline = seconds;
    in.dismiss = dismiss || seconds < 0;
    interact(std::move(in));
}

} // namespace Ddlc
