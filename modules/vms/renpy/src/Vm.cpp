// The Ren'Py script executor: the init phase, the statements, the user
// statements (play/stop/window/pause/screen) and the native renpy API.
// The script runs on its own thread used as a coroutine: it only runs while
// the host waits in resume(), so the two never touch the state concurrently.
#include "Ddlc/Runtime.hpp"

#include <SDL3/SDL_thread.h>
#include <SDL3/SDL_timer.h>

#include <Kotonoha/Filesystem/Adapters/StreamAdapter.hpp>
#include <algorithm>
#include <cctype>
#include <filesystem>
#include <sstream>

namespace Ddlc {

struct Vm::ThreadHolder {
  SDL_Thread *thread = nullptr;
};

namespace {

const Value *kwFind(const Kwargs &kw, const char *name) {
  for (auto &[k, v] : kw)
    if (k == name)
      return &v;
  return nullptr;
}
Value kwGet(const Kwargs &kw, const char *name, Value def = {}) {
  const Value *v = kwFind(kw, name);
  return v ? *v : def;
}
Value argOr(const Args &a, size_t i, const Kwargs &kw, const char *name,
            Value def = {}) {
  if (i < a.size())
    return a[i];
  return kwGet(kw, name, std::move(def));
}

std::vector<std::string> splitWords(const std::string &s) {
  std::vector<std::string> out;
  std::istringstream in(s);
  std::string w;
  while (in >> w)
    out.push_back(w);
  return out;
}

std::string trim(const std::string &s) {
  size_t a = 0, b = s.size();
  while (a < b && std::isspace((unsigned char)s[a]))
    ++a;
  while (b > a && std::isspace((unsigned char)s[b - 1]))
    --b;
  return s.substr(a, b - a);
}

// Splits a user statement into tokens, keeping quoted strings and
// parenthesised groups whole.
std::vector<std::string> tokens(const std::string &s) {
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
        if (c == '\\')
          ++i;
        else if (c == quote)
          quote = 0;
      } else if (c == '"' || c == '\'')
        quote = c;
      else if (c == '(' || c == '[')
        ++depth;
      else if (c == ')' || c == ']')
        --depth;
      else if (std::isspace((unsigned char)c) && depth == 0)
        break;
      ++i;
    }
    out.push_back(s.substr(start, i - start));
  }
  return out;
}

Value atlValue(std::shared_ptr<AtlTemplate> t, Vm *vm);

// Calling a transform binds its parameters; a remaining positional argument
// (or child=) becomes its child.
Value bindTransform(const std::shared_ptr<AtlTemplate> &t, Vm *vm, Args &a,
                    Kwargs &kw) {
  auto n = std::make_shared<AtlTemplate>(*t);
  size_t pi = 0;
  for (auto &v : a) {
    while (pi < n->params.size() && n->bound.count(n->params[pi].first))
      ++pi;
    if (pi < n->params.size())
      n->bound[n->params[pi++].first] = v;
    else
      n->child = vm->toDisp(v);
  }
  for (auto &[k, v] : kw) {
    if (k == "child")
      n->child = vm->toDisp(v);
    else
      n->bound[k] = v;
  }
  return atlValue(n, vm);
}

Value atlValue(std::shared_ptr<AtlTemplate> t, Vm *vm) {
  Value v = makeInstance("ATLTransform");
  v.o->payload = t;
  v.o->fn = [t, vm](Interp &, Args &a, Kwargs &kw) {
    return bindTransform(t, vm, a, kw);
  };
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

std::shared_ptr<AtlTemplate> placement(double xpos, double xanchor, double ypos,
                                       double yanchor, const char *name) {
  auto t = std::make_shared<AtlTemplate>();
  t->name = name;
  t->props = {{"xpos", Value::real(xpos)},
              {"xanchor", Value::real(xanchor)},
              {"ypos", Value::real(ypos)},
              {"yanchor", Value::real(yanchor)}};
  return t;
}

template <class F>
void forChain(const std::shared_ptr<TransformDisp> &xf, F f) {
  for (DispP d = xf; auto t = std::dynamic_pointer_cast<TransformDisp>(d);
       d = t->child())
    f(*t);
}

int SDLCALL threadEntry(void *p);

} // namespace

struct VmNatives {
  static void run(Vm &vm) { vm.threadMain(); }
  static void install(Vm &vm);
};

namespace {
int SDLCALL threadEntry(void *p) {
  VmNatives::run(*static_cast<Vm *>(p));
  return 0;
}
} // namespace

// ---- lifecycle --------------------------------------------------------------

Vm::Vm(const Assets &assets, const Script &script, Backend &backend)
    : assets(assets), script(script), backend(backend) {
  clock = [] { return double(SDL_GetTicksNS()) / 1e9; };
}

Vm::~Vm() { stop(); }

void Vm::start(const std::string &label) {
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
  SDL_SetPointerProperty(props, SDL_PROP_THREAD_CREATE_ENTRY_FUNCTION_POINTER,
                         (void *)&threadEntry);
  SDL_SetStringProperty(props, SDL_PROP_THREAD_CREATE_NAME_STRING,
                        "ddlc-script");
  SDL_SetPointerProperty(props, SDL_PROP_THREAD_CREATE_USERDATA_POINTER, this);
  SDL_SetNumberProperty(props, SDL_PROP_THREAD_CREATE_STACKSIZE_NUMBER,
                        64 * 1024 * 1024);
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
  if (!threadAlive_)
    return;
  result_ = std::move(result);
  std::unique_lock<std::mutex> lk(mu_);
  vmTurn_ = true;
  cv_.notify_all();
  cv_.wait(lk, [&] { return !vmTurn_; });
}

void Vm::stop() {
  if (!thread_)
    return;
  {
    std::lock_guard<std::mutex> lk(mu_);
    abort_ = true;
    vmTurn_ = true;
  }
  cv_.notify_all();
  if (thread_->thread)
    SDL_WaitThread(thread_->thread, nullptr);
  thread_.reset();
  threadAlive_ = false;
  abort_ = false;
  vmTurn_ = false;
}

void Vm::threadMain() {
  Interaction last;
  last.k = Interaction::K::End;
  try {
    Node *n = script.label(startLabel_);
    if (!n)
      errors.push_back("missing label " + startLabel_);
    runFrom(n);
  } catch (const AbortSignal &) {
  } catch (const RestartSignal &) {
    last.k = Interaction::K::Restart;
  } catch (const QuitSignal &q) {
    if (q.relaunch)
      last.k = Interaction::K::Restart;
  } catch (const std::exception &e) {
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
  if (std::getenv("DDLC_DEBUG_NODES"))
    std::fprintf(stderr, "VM interact k=%d what='%s'\n", int(in.k),
                 in.k == Interaction::K::Say ? scene.window.what.c_str() : "");
  {
    std::unique_lock<std::mutex> lk(mu_);
    if (abort_)
      throw AbortSignal{};
    inter_ = std::move(in);
    vmTurn_ = false;
    cv_.notify_all();
    {
      PythonThreadRelease release;
      cv_.wait(lk, [&] { return vmTurn_; });
    }
    if (abort_)
      throw AbortSignal{};
  }
  collectHidden();
  committed = scene;
  Value r = std::move(result_);
  result_ = {};
  return r;
}

// ---- text -------------------------------------------------------------------

std::string Vm::interpolate(const std::string &text) {
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
      if (auto bang = expr.find('!'); bang != std::string::npos)
        expr.resize(bang);
      try {
        out += py.eval(expr).text();
      } catch (const PyError &err) {
        backend.log("interpolate [" + expr + "]: " + err.what());
      }
      i = e;
      continue;
    }
    out += c;
  }
  return out;
}

void Vm::restorePersistentText(const std::string &persistentText) {
  persistent.o->attrs.clear();
  std::istringstream in(persistentText);
  for (std::string line; std::getline(in, line);) {
    auto eq = line.find(" = ");
    if (eq == std::string::npos)
      continue;
    try {
      persistent.o->attrs[line.substr(0, eq)] = py.eval(line.substr(eq + 3));
    } catch (const PyError &e) {
      backend.log("persistent " + line.substr(0, eq) + ": " + e.what());
    }
  }
}

std::string Vm::savePersistentText() {
  std::string out;
  if (!persistent.isObj())
    return out;
  for (auto &[k, v] : persistent.o->attrs) {
    if (v.isNone())
      continue;
    if (v.isObj() && (v.o->fn || v.o->k == Object::K::Instance ||
                      v.o->k == Object::K::Class))
      continue;
    out += k + " = " + v.repr() + "\n";
  }
  return out;
}

// ---- files ------------------------------------------------------------------

// The game addresses game/ and its base directory (characters/, the notes it
// leaves for the player). game/ is read-only: writes and removals there are
// virtual, kept in the persistent overlay. Base-directory files are real files
// in Vm::userDirectory when it is set, and virtual otherwise.
namespace {

constexpr const char *kRootPrefix = "@root/";

bool isRootKey(const std::string &key) {
  return key.rfind(kRootPrefix, 0) == 0;
}

bool beneath(const std::filesystem::path &relative) {
  return !relative.empty() && !relative.is_absolute() &&
         *relative.begin() != ".." && relative != ".";
}

} // namespace

static std::string fileKey(Vm &vm, const std::string &name) {
  if (name.empty() || name.find('\0') != std::string::npos)
    throw PyError("IOError: invalid game file path");
  std::string normalized = name;
  std::replace(normalized.begin(), normalized.end(), '\\', '/');
  std::error_code error;
  const auto game = std::filesystem::weakly_canonical(
      std::filesystem::absolute(vm.assets.dir()), error);
  if (error)
    throw PyError("IOError: cannot resolve the game directory: " +
                  error.message());
  const auto root = game.parent_path();
  const std::filesystem::path requested(normalized);
  const auto path = std::filesystem::weakly_canonical(
      requested.is_absolute() ? requested : game / requested, error);
  if (error)
    throw PyError("IOError: cannot resolve " + name + ": " + error.message());
  if (!vm.userDirectory.empty()) {
    const auto user = std::filesystem::weakly_canonical(
        std::filesystem::absolute(vm.userDirectory), error);
    if (error)
      throw PyError("IOError: cannot resolve the base directory: " +
                    error.message());
    if (const auto relative = path.lexically_relative(user);
        beneath(relative)) {
      if (*relative.begin() == "game") {
        const auto inGame = relative.lexically_relative("game");
        if (!beneath(inGame))
          throw PyError("IOError: invalid game file path: " + name);
        return inGame.generic_string();
      }
      return kRootPrefix + relative.generic_string();
    }
  }
  const auto relative = path.lexically_relative(root);
  if (!beneath(relative))
    throw PyError("IOError: path is outside the game installation: " + name);
  const auto withinGame = path.lexically_relative(game);
  if (beneath(withinGame))
    return withinGame.generic_string();
  return kRootPrefix + relative.generic_string();
}

static std::filesystem::path userFile(const Vm &vm, const std::string &key) {
  return vm.userDirectory /
         std::filesystem::path(key.substr(std::string(kRootPrefix).size()));
}

static std::string dirKey(Vm &vm, const std::string &name, bool &isRoot) {
  if (name.empty() || name.find('\0') != std::string::npos)
    throw PyError("IOError: invalid directory path");
  std::string normalized = name;
  std::replace(normalized.begin(), normalized.end(), '\\', '/');
  while (normalized.size() > 1 && normalized.back() == '/')
    normalized.pop_back();
  std::error_code error;
  const auto game = std::filesystem::weakly_canonical(
      std::filesystem::absolute(vm.assets.dir()), error);
  if (error)
    throw PyError("IOError: cannot resolve the game directory");
  const auto root = game.parent_path();
  const std::filesystem::path requested(normalized);
  const auto path = std::filesystem::weakly_canonical(
      requested.is_absolute() ? requested : game / requested, error);
  if (error)
    throw PyError("IOError: cannot resolve " + name);
  if (!vm.userDirectory.empty()) {
    const auto user = std::filesystem::weakly_canonical(
        std::filesystem::absolute(vm.userDirectory), error);
    if (!error) {
      if (path == user) {
        isRoot = true;
        return {};
      }
      if (const auto relative = path.lexically_relative(user);
          beneath(relative)) {
        if (relative == "game") {
          isRoot = false;
          return {};
        }
        if (*relative.begin() == "game") {
          const auto inGame = relative.lexically_relative("game");
          if (beneath(inGame)) {
            isRoot = false;
            return inGame.generic_string();
          }
        }
        isRoot = true;
        return relative.generic_string();
      }
    }
  }
  if (path == root) {
    isRoot = true;
    return {};
  }
  if (path == game) {
    isRoot = false;
    return {};
  }
  const auto relative = path.lexically_relative(root);
  if (!beneath(relative))
    throw PyError("IOError: path is outside the game installation: " + name);
  const auto withinGame = path.lexically_relative(game);
  if (beneath(withinGame)) {
    isRoot = false;
    return withinGame.generic_string();
  }
  isRoot = true;
  return relative.generic_string();
}

bool Vm::isDir(const std::string &name) {
  bool isRoot = false;
  std::string rel;
  try {
    rel = dirKey(*this, name, isRoot);
  } catch (const PyError &) {
    return false;
  }
  if (rel.empty())
    return true;
  const std::string fullKey = isRoot ? (kRootPrefix + rel) : rel;
  if (std::find(virtualDirs_.begin(), virtualDirs_.end(), fullKey) !=
      virtualDirs_.end())
    return true;
  const std::string prefix = rel + "/";
  if (isRoot) {
    if (virtualRootFiles) {
      for (const auto &[k, _] : *virtualRootFiles)
        if (k.compare(0, prefix.size(), prefix) == 0)
          return true;
    }
    if (!userDirectory.empty()) {
      std::error_code ec;
      if (std::filesystem::is_directory(userDirectory / rel, ec))
        return true;
    }
    for (const auto &f : assets.overlayRootNames())
      if (f.compare(0, prefix.size(), prefix) == 0)
        return true;
    std::error_code ec;
    const auto game = std::filesystem::absolute(assets.dir());
    if (std::filesystem::is_directory(game.parent_path() / rel, ec))
      return true;
  } else {
    for (const auto &f : assets.names())
      if (f.compare(0, prefix.size(), prefix) == 0)
        return true;
    std::error_code ec;
    const auto game = std::filesystem::absolute(assets.dir());
    if (std::filesystem::is_directory(game / rel, ec))
      return true;
  }
  return false;
}

void Vm::makeDirs(const std::string &name) {
  bool isRoot = false;
  const std::string rel = dirKey(*this, name, isRoot);
  if (rel.empty())
    return;
  const std::string fullKey = isRoot ? (kRootPrefix + rel) : rel;
  if (std::find(virtualDirs_.begin(), virtualDirs_.end(), fullKey) ==
      virtualDirs_.end())
    virtualDirs_.push_back(fullKey);
  if (isRoot && !virtualRootFiles && !userDirectory.empty()) {
    std::error_code ec;
    std::filesystem::create_directories(userDirectory / rel, ec);
  }
}

std::vector<std::string> Vm::listDir(const std::string &name) {
  bool isRoot = false;
  const std::string rel = dirKey(*this, name, isRoot);
  if (!isDir(name))
    throw PyError("OSError: directory does not exist: " + name);
  const std::string prefix = rel.empty() ? std::string() : (rel + "/");
  std::vector<std::string> entries;
  auto addFromPath = [&](const std::string &p) {
    if (p.compare(0, prefix.size(), prefix) != 0 || p.size() <= prefix.size())
      return;
    const std::string rest = p.substr(prefix.size());
    const auto slash = rest.find('/');
    entries.push_back(slash == std::string::npos ? rest
                                                 : rest.substr(0, slash));
  };
  if (isRoot) {
    if (virtualRootFiles) {
      for (const auto &[k, _] : *virtualRootFiles)
        addFromPath(k);
    }
    for (const auto &f : assets.overlayRootNames())
      addFromPath(f);
    if (!userDirectory.empty()) {
      std::error_code ec;
      const auto dir = rel.empty() ? userDirectory : (userDirectory / rel);
      if (std::filesystem::is_directory(dir, ec))
        for (const auto &e : std::filesystem::directory_iterator(dir, ec))
          entries.push_back(e.path().filename().string());
    }
    std::error_code ec;
    const auto root = std::filesystem::absolute(assets.dir()).parent_path();
    const auto dir = rel.empty() ? root : (root / rel);
    if (std::filesystem::is_directory(dir, ec))
      for (const auto &e : std::filesystem::directory_iterator(dir, ec))
        entries.push_back(e.path().filename().string());
  } else {
    for (const auto &f : assets.names())
      addFromPath(f);
    std::error_code ec;
    const auto game = std::filesystem::absolute(assets.dir());
    const auto dir = rel.empty() ? game : (game / rel);
    if (std::filesystem::is_directory(dir, ec))
      for (const auto &e : std::filesystem::directory_iterator(dir, ec))
        entries.push_back(e.path().filename().string());
  }
  std::sort(entries.begin(), entries.end());
  entries.erase(std::unique(entries.begin(), entries.end()), entries.end());
  return entries;
}

void Vm::setVirtualFile(const std::string &name, const Value &value) {
  const std::string key = fileKey(*this, name);
  if (value.isNone())
    ephemeralFiles_.erase(key);
  if (isRootKey(key) && virtualRootFiles) {
    auto logical = key.substr(std::string(kRootPrefix).size());
    if (value.isNone())
      virtualRootFiles->erase(logical);
    else
      (*virtualRootFiles)[logical] = value.text();
    return;
  }

  if (isRootKey(key) && !userDirectory.empty()) {
    const auto path = userFile(*this, key);
    std::error_code error;
    if (value.isNone()) {
      if (!std::filesystem::remove(path, error) || error)
        throw PyError("OSError: cannot remove " + path.string() +
                      (error ? ": " + error.message() : std::string()));
      return;
    }
    std::filesystem::create_directories(path.parent_path(), error);
    if (error)
      throw PyError("IOError: cannot create " + path.parent_path().string() +
                    ": " + error.message());
    Kotonoha::Filesystem::OutputFile out(path,
                                         std::ios::binary | std::ios::trunc);
    const std::string data = value.text();
    if (!out || !out.write(data.data(), std::streamsize(data.size())))
      throw PyError("IOError: cannot write " + path.string());
    return;
  }
  if (!isRootKey(key) && value.isStr() && value.s.size() > 4096) {
    ephemeralFiles_[key] = value;
    return;
  }
  Value vfs = persistent.get("_ddlc_files");
  if (!vfs.isObj() || vfs.o->k != Object::K::Dict) {
    vfs = makeDict();
    py.setAttr(persistent, "_ddlc_files", vfs);
  }
  py.setItem(vfs, Value::str(key), value);
}

Value Vm::fileObject(const std::string &path, std::string content,
                     bool writable, bool append) {
  Value f = makeInstance("file");
  auto data = std::make_shared<std::string>(std::move(content));
  auto pos = std::make_shared<size_t>(append ? data->size() : 0);
  auto closed = std::make_shared<bool>(false);
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
  f.o->attrs["read"] =
      makeFunction("read", [data, pos, closed](Interp &, Args &a, Kwargs &) {
        if (*closed)
          throw PyError("ValueError: read from closed file");
        size_t n = a.empty() || a[0].integer() < 0 ? std::string::npos
                                                   : size_t(a[0].integer());
        std::string r = data->substr(std::min(*pos, data->size()), n);
        *pos += r.size();
        return Value::str(r);
      });
  Vm *vm = this;
  f.o->attrs["write"] =
      makeFunction("write", [data, pos, closed, path, writable, append,
                             vm](Interp &, Args &a, Kwargs &) {
        if (*closed)
          throw PyError("ValueError: write to closed file");
        if (!writable)
          throw PyError("IOError: file is not writable");
        if (a.empty() || !a[0].isStr())
          throw PyError("TypeError: write requires a string");
        if (append)
          *pos = data->size();
        if (*pos > data->size())
          data->resize(*pos, '\0');
        const std::string &text = a[0].s;
        if (*pos + text.size() > data->size())
          data->resize(*pos + text.size());
        data->replace(*pos, text.size(), text);
        *pos += text.size();
        vm->setVirtualFile(path, Value::str(*data));
        return Value();
      });
  f.o->attrs["close"] =
      makeFunction("close", [closed](Interp &, Args &, Kwargs &) {
        *closed = true;
        return Value();
      });
  f.o->attrs["seek"] =
      makeFunction("seek", [data, pos, closed](Interp &, Args &a, Kwargs &) {
        if (*closed)
          throw PyError("ValueError: seek on closed file");
        const int64_t offset = a.empty() ? 0 : a[0].integer();
        const int64_t whence = a.size() < 2 ? 0 : a[1].integer();
        if (whence < 0 || whence > 2)
          throw PyError("ValueError: invalid seek origin");
        const int64_t base = whence == 1   ? int64_t(*pos)
                             : whence == 2 ? int64_t(data->size())
                                           : 0;
        if (offset < -base)
          throw PyError("ValueError: negative file position");
        *pos = size_t(base + offset);
        return Value();
      });
  return f;
}

std::string Vm::readFile(const std::string &name, bool &ok) {
  const std::string key = fileKey(*this, name);
  if (isRootKey(key) && virtualRootFiles) {
    auto logical = key.substr(std::string(kRootPrefix).size());
    auto found = virtualRootFiles->find(logical);
    if (found != virtualRootFiles->end()) {
      ok = true;
      return found->second;
    }
    std::vector<uint8_t> bytes;
    ok = assets.readOverlayRoot(logical, &bytes);
    return ok ? std::string(bytes.begin(), bytes.end()) : std::string();
  }

  if (isRootKey(key) && !userDirectory.empty()) {
    const auto path = userFile(*this, key);
    std::error_code error;
    Kotonoha::Filesystem::InputFile in;
    if (std::filesystem::is_regular_file(path, error))
      in.open(path, std::ios::binary);
    if (in.is_open() && in.good()) {
      ok = true;
      return std::string(std::istreambuf_iterator<char>(in), {});
    }
    const auto logical = key.substr(std::string(kRootPrefix).size());
    std::vector<uint8_t> bytes;
    if (assets.readOverlayRoot(logical, &bytes)) {
      ok = true;
      return std::string(bytes.begin(), bytes.end());
    }
    if (logical.rfind("characters/", 0) == 0 ||
        logical == "CAN YOU HEAR ME.txt" || logical == "hxppy thxughts.png" ||
        logical == "iiiiii.txt") {
      ok = false;
      return {};
    }
  }
  if (auto ep = ephemeralFiles_.find(key); ep != ephemeralFiles_.end()) {
    ok = !ep->second.isNone();
    return ok ? ep->second.text() : std::string();
  }
  Value vfs = persistent.get("_ddlc_files");
  if (vfs.isObj())
    if (Value *v = vfs.o->findKey(Value::str(key))) {
      ok = !v->isNone();
      return ok ? v->text() : std::string();
    }
  std::vector<uint8_t> rootBytes;
  if (isRootKey(key) &&
      assets.readOverlayRoot(key.substr(std::string(kRootPrefix).size()),
                             &rootBytes)) {
    ok = true;
    return std::string(rootBytes.begin(), rootBytes.end());
  }
  if (!isRootKey(key) && assets.has(key)) {
    ok = true;
    auto bytes = assets.read(key);
    return std::string(bytes.begin(), bytes.end());
  }
  const std::filesystem::path game = std::filesystem::absolute(assets.dir());
  const std::filesystem::path p =
      isRootKey(key)
          ? game.parent_path() / key.substr(std::string(kRootPrefix).size())
          : game / key;
  Kotonoha::Filesystem::InputFile in(p, std::ios::binary);
  if (in) {
    ok = true;
    return std::string(std::istreambuf_iterator<char>(in), {});
  }
  ok = false;
  return {};
}

// ---- init -------------------------------------------------------------------

void Vm::init(const std::string &persistentText) {
  ephemeralFiles_.clear();
  virtualDirs_.clear();
  auto ns = [&](const char *name) {
    Value v = Interp::nameSpace(name);
    py.set(name, v);
    return v;
  };
  persistent = ns("persistent");
  config = ns("config");
  audio = ns("audio");
  gui = ns("gui");
  styleNs = ns("style");
  ns("build");
  ns("define");
  ns("store");

  restorePersistentText(persistentText);

  std::filesystem::path dir(assets.dir());
  std::string base = dir.parent_path().generic_string();
  if (dir.filename().empty())
    base = dir.parent_path().parent_path().generic_string();
  auto &c = config.o->attrs;
  c["basedir"] =
      Value::str(userDirectory.empty() ? base : userDirectory.generic_string());
  c["gamedir"] = Value::str(dir.generic_string());
  c["images_directory"] = Value::str("images");
  c["overlay_functions"] = makeList();
  c["developer"] = Value::boolean(false);
  c["allow_skipping"] = Value::boolean(true);
  c["skipping"] = Value::boolean(false);
  c["window_show_transition"] = transitionValue(dissolve(.2));
  c["window_hide_transition"] = transitionValue(dissolve(.2));
  c["translations"] = makeDict();
  c["font_replacement_map"] = makeDict();
  c["layers"] = makeList({Value::str("master"), Value::str("transient"),
                          Value::str("screens"), Value::str("overlay")});
  c["underlay"] = makeList();
  c["searchpath"] = makeList();
  c["window_title"] = Value::str("");
  Value keymap = makeDict();
  for (const char *k :
       {"game_menu", "hide_windows", "self_voicing", "clipboard_voicing",
        "toggle_skip", "rollback", "rollforward", "screenshot", "dismiss",
        "skip", "fast_skip"})
    py.setItem(keymap, Value::str(k), makeList());
  py.getItem(keymap, Value::str("game_menu"))
      .o->items.push_back(Value::str("mouseup_3"));
  auto &dismissKeys = py.getItem(keymap, Value::str("dismiss")).o->items;
  dismissKeys.push_back(Value::str("K_RETURN"));
  dismissKeys.push_back(Value::str("K_KP_ENTER"));
  dismissKeys.push_back(Value::str("K_SPACE"));
  c["keymap"] = keymap;
  if (!persistent.attr("_seen_images") ||
      persistent.get("_seen_images").isNone())
    persistent.o->attrs["_seen_images"] = makeDict();

  for (const char *st : {"default",        "say_dialogue",
                         "say_label",      "say_thought",
                         "nvl_dialogue",   "nvl_label",
                         "nvl_thought",    "nvl_window",
                         "menu_choice",    "menu_choice_button",
                         "button_text",    "text",
                         "window",         "frame",
                         "centered_text",  "centered_window",
                         "alive_text",     "b_nvl_window",
                         "credits_text",   "say_window",
                         "say_who_window", "say_two_window_vbox",
                         "mm_button_text", "gm_button_text",
                         "prefs_label",    "prefs_button_text"})
    if (!styleNs.attr(st))
      styleNs.o->attrs[st] = Interp::nameSpace(st);
  py.set("_dismiss_pause", Value::boolean(true));
  py.set("quick_menu", Value::boolean(true));
  py.set("main_menu", Value::boolean(false));
  py.set("_window", Value::boolean(false));
  py.set("_return", Value());

  VmNatives::install(*this);

  size_t failures = 0;
  for (Node *n : script.inits()) {
    try {
      execInit(n);
    } catch (const PyError &e) {
      ++failures;
      std::string src = n->ast.get("code").get("source").text();
      if (src.empty())
        src = n->str("varname");
      std::replace(src.begin(), src.end(), '\n', ' ');
      errors.push_back(n->file + ":" + std::to_string(n->line) + ": " +
                       e.what() + " [" + src.substr(0, 90) + "]");
    } catch (const JumpSignal &) {
    } catch (const QuitSignal &) {
    }
  }
  if (failures)
    backend.log(std::to_string(failures) + " init statements failed");

  // Ren'Py common/00images.rpy registers basename images after explicit init
  // declarations. A declared image always wins over an automatic definition.
  auto imageDirectory = config.get("images_directory");
  if (imageDirectory.truthy()) {
    auto prefix = imageDirectory.text();
    while (!prefix.empty() && prefix.back() == '/')
      prefix.pop_back();
    prefix += '/';
    for (const auto &file : assets.names()) {
      if (file.compare(0, prefix.size(), prefix) != 0)
        continue;
      auto path = std::filesystem::path(file);
      auto ext = path.extension().string();
      for (char &c : ext)
        c = char(std::tolower(static_cast<unsigned char>(c)));
      if (ext != ".png" && ext != ".jpg" && ext != ".webp")
        continue;
      auto name = path.stem().string();
      for (char &c : name)
        c = char(std::tolower(static_cast<unsigned char>(c)));
      images.try_emplace(name, Value::str(file));
    }
  }

  // Older Ren'Py games commonly keep backgrounds and effects outside the
  // configured images directory and refer to them by their filename stem.
  for (const auto &file : assets.names()) {
    const bool background = file.rfind("bgs/", 0) == 0;
    const bool effect = file.rfind("vfx/", 0) == 0;
    if (!background && !effect)
      continue;
    const auto path = std::filesystem::path(file);
    std::string ext = path.extension().string();
    for (char &c : ext)
      c = char(std::tolower(static_cast<unsigned char>(c)));
    if (ext != ".png" && ext != ".jpg" && ext != ".webp")
      continue;
    images.try_emplace((background ? "bg " : "") + path.stem().string(),
                       Value::str(file));
  }
  images.try_emplace("black", Value::str("#000000"));

  if (assets.has("ui_settings.rpyc") && assets.has("vfx/snowflake.png"))
    images.try_emplace("snow", dispValue(makeSnowDisp("vfx/snowflake.png")));

  // Katawa Shoujo's ui_settings.rpyc creates these names with ks_music().
  // Keep the archive's logical IDs here until its legacy UI init can run.
  if (assets.has("ui_settings.rpyc") && assets.has("bgm/Wiosna.ogg")) {
    static const std::pair<const char *, const char *> tracks[] = {
        {"tranquil", "Afternoon"},
        {"nurse", "Ah_Eh_I_Oh_You"},
        {"soothing", "Air_Guitar"},
        {"twinkle", "Aria_de_l'Etoile"},
        {"moonlight", "Breathlessly"},
        {"rain", "Caged_Heart"},
        {"tragic", "Cold_Iron"},
        {"comfort", "Comfort"},
        {"lilly", "Concord"},
        {"daily", "Daylight"},
        {"ease", "Ease"},
        {"another", "Everyday_Fantasy"},
        {"friendship", "Friendship"},
        {"happiness", "Fripperies"},
        {"comedy", "Generic_Happy_Music"},
        {"tension", "High_Tension"},
        {"running", "Hokabi"},
        {"innocence", "Innocence"},
        {"heart", "Letting_my_Heart_Speak"},
        {"serene", "Lullaby_of_Open_Eyes"},
        {"drama", "Moment_of_Decision"},
        {"night", "Nocturne"},
        {"kenji", "Out_of_the_Loop"},
        {"hanako", "Painful_History"},
        {"rin", "Parity"},
        {"timeskip", "Passing_of_Time"},
        {"dreamy", "Raindrops_and_Puddles"},
        {"jazz", "Red_Velvet"},
        {"romance", "Romance_in_Andante_II"},
        {"credits", "Romance_in_Andante"},
        {"musicbox", "Sarabande_from_BWV1010"},
        {"normal", "School_Days"},
        {"sadness", "Shadow_of_the_Truth"},
        {"emi", "Standing_Tall"},
        {"pearly", "Stride"},
        {"shizune", "The_Student_Council"},
        {"one", "To_Become_One"},
        {"menus", "Wiosna"},
    };
    for (const auto &[alias, file] : tracks) {
      std::string path = std::string("bgm/") + file + ".ogg";
      if (assets.has(path))
        py.set(std::string("music_") + alias, Value::str(path));
    }
  }

  // Natives replacing helpers of the init code that need real Ren'Py internals.
  Vm *vm = this;
  py.def("pause", [vm](Interp &, Args &a, Kwargs &kw) {
    Value t = argOr(a, 0, kw, "time");
    if (t.isNone() || (t.isNum() && t.num() == 0 && t.t != Value::T::Float))
      vm->pause(-1, true);
    else if (t.num() > 0)
      vm->pause(t.num(), vm->py.lookup("_dismiss_pause").truthy());
    return Value();
  });
  py.def("get_pos", [vm](Interp &, Args &a, Kwargs &kw) {
    return Value::real(vm->backend.position(
        argOr(a, 0, kw, "channel", Value::str("music")).text()));
  });
}

void Vm::execInit(Node *n) {
  auto source = [&](const char *field) {
    return n->ast.get(field).get("source").text();
  };
  switch (n->k) {
  case Node::K::Python:
  case Node::K::EarlyPython:
    py.exec(source("code"));
    break;
  case Node::K::Define:
  case Node::K::Default: {
    const std::string store = n->str("store"), var = n->str("varname");
    Value v = py.eval(source("code"));
    if (store == "store") {
      if (n->k == Node::K::Default && py.has(var))
        break;
      py.set(var, v);
      break;
    }
    std::string nsName =
        store.rfind("store.", 0) == 0 ? store.substr(6) : store;
    Value target = py.has(nsName) ? py.lookup(nsName) : Value();
    if (!target.isObj()) {
      target = Interp::nameSpace(nsName);
      py.set(nsName, target);
    }
    if (n->k == Node::K::Default) {
      const Value *cur = target.attr(var);
      if (cur && !cur->isNone())
        break;
    }
    py.setAttr(target, var, v);
    break;
  }
  case Node::K::Image: {
    std::string key;
    for (auto &part : n->ast.get("imgname").items())
      key += (key.empty() ? "" : " ") + part.text();
    if (n->ast.get("atl").isObj()) {
      auto t = std::make_shared<AtlTemplate>();
      t->atl = n->ast.get("atl");
      t->name = key;
      images[key] = atlValue(t, this);
    } else
      images[key] = py.eval(source("code"));
    break;
  }
  case Node::K::Transform: {
    auto t = std::make_shared<AtlTemplate>();
    t->atl = n->ast.get("atl");
    t->name = n->str("varname");
    Value params = n->ast.get("parameters");
    if (params.isObj())
      for (auto &p : params.get("parameters").items()) {
        const auto &kv = p.items();
        if (kv.empty())
          continue;
        t->params.emplace_back(
            kv[0].text(), kv.size() > 1 && !kv[1].isNone() ? kv[1].text() : "");
      }
    const std::string store = n->str("store");
    if (store.empty() || store == "store")
      py.set(t->name, atlValue(t, this));
    else
      py.setAttr(py.lookup(store.substr(6)), t->name, atlValue(t, this));
    break;
  }
  case Node::K::Style: {
    const std::string sname = n->str("style_name");
    if (!sname.empty()) {
      Value s = makeInstance("Style");
      s.o->attrs["name"] = Value::str(sname);
      const Value &parent = n->ast.get("parent");
      if (!parent.isNone())
        s.o->attrs["parent"] = parent;
      py.setAttr(styleNs, sname, s);
    }
    break;
  }
  default:
    break;
  }
}

// ---- natives ----------------------------------------------------------------

void VmNatives::install(Vm &vm) {
  Interp &py = vm.py;
  Vm *v = &vm;
  auto def = [&](Value ns, const char *name, NativeFn fn) {
    ns.o->attrs[name] = makeFunction(name, std::move(fn));
  };
  py.set("chapter", Value::integer(0));
  py.set("playthroughflag", Value::integer(0));
  py.set("m1", Value::integer(0));
  py.set("m2", Value::integer(0));
  py.set("m3", Value::integer(0));
  py.set("m4", Value::integer(0));
  py.set("m5", Value::integer(0));
  py.def("wrap_label", [](Interp &, Args &a, Kwargs &) {
    return a.size() > 1 ? a[1] : (a.empty() ? Value() : a[0]);
  });
  py.def("seen_scene",
         [](Interp &, Args &, Kwargs &) { return Value::boolean(false); });
  if (v->assets.has("ui_settings.rpyc")) {
    py.def("ksgallery_unlock", [v](Interp &interp, Args &args, Kwargs &) {
      Value unlocked = interp.getAttr(v->persistent, "ksgallery_unlocked");
      if (!unlocked.isObj() || unlocked.o->k != Object::K::List) {
        unlocked = makeList();
        interp.setAttr(v->persistent, "ksgallery_unlocked", unlocked);
      }
      for (const auto &item : args) {
        bool found = false;
        for (const auto &saved : unlocked.o->items)
          if (saved.text() == item.text()) {
            found = true;
            break;
          }
        if (!found)
          unlocked.o->items.push_back(Value::str(item.text()));
      }
      return Value();
    });
  }
  py.def("notextmode", [](Interp &, Args &, Kwargs &) { return Value(); });
  py.def("nvl_clear", [v](Interp &, Args &, Kwargs &) {
    v->scene.window.what.clear();
    v->scene.window.rich = {};
    return Value();
  });
  py.def("scene_register", [](Interp &, Args &, Kwargs &) { return Value(); });
  py.def("wdt_off", [](Interp &, Args &, Kwargs &) { return Value(); });
  py.def("wdt_on", [](Interp &, Args &, Kwargs &) { return Value(); });
  auto disp = [v](const Value &x) { return v->toDisp(x); };
  auto size2 = [](const Value &t, double &w, double &h) {
    const auto &it = t.items();
    w = it.size() > 0 ? it[0].num() : 0;
    h = it.size() > 1 ? it[1].num() : 0;
  };

  // Built-in transforms and transitions.
  py.set("left", atlValue(placement(0, 0, 1, 1, "left"), v));
  py.set("right", atlValue(placement(1, 1, 1, 1, "right"), v));
  py.set("center", atlValue(placement(.5, .5, 1, 1, "center"), v));
  py.set("truecenter", atlValue(placement(.5, .5, .5, .5, "truecenter"), v));
  py.set("topleft", atlValue(placement(0, 0, 0, 0, "topleft"), v));
  py.set("top", atlValue(placement(.5, .5, 0, 0, "top"), v));
  py.set("topright", atlValue(placement(1, 1, 0, 0, "topright"), v));
  py.set("offscreenleft", atlValue(placement(0, 1, 1, 1, "offscreenleft"), v));
  py.set("offscreenright",
         atlValue(placement(1, 0, 1, 1, "offscreenright"), v));
  py.set("default", atlValue(placement(.5, .5, 1, 1, "default"), v));
  py.set("dissolve", transitionValue(dissolve(.5)));

  py.def("_", [](Interp &, Args &a, Kwargs &) {
    return a.empty() ? Value() : a[0];
  });
  py.def("open", [v](Interp &, Args &a, Kwargs &kw) {
    const std::string path = argOr(a, 0, kw, "name").text();
    const std::string mode = argOr(a, 1, kw, "mode", Value::str("r")).text();
    const bool truncate = mode.find('w') != std::string::npos;
    const bool append = mode.find('a') != std::string::npos;
    const bool read = mode.find('r') != std::string::npos;
    if (int(truncate) + int(append) + int(read) != 1 ||
        mode.find_first_not_of("rwa+b") != std::string::npos)
      throw PyError("ValueError: unsupported file mode " + mode);
    const bool writable =
        truncate || append || mode.find('+') != std::string::npos;
    bool ok = false;
    std::string data = truncate ? std::string() : v->readFile(path, ok);
    if (!ok && read)
      throw PyError("IOError: " + path);
    if (truncate || (append && !ok))
      v->setVirtualFile(path, Value::str(""));
    return v->fileObject(path, std::move(data), writable, append);
  });
  Value os = py.builtins.at("os");
  os.o->openAttrs = false;
  def(os, "remove", [v](Interp &, Args &a, Kwargs &) {
    const std::string path = a.at(0).text();
    bool ok = false;
    v->readFile(path, ok);
    if (!ok)
      throw PyError("OSError: file does not exist: " + path);
    v->setVirtualFile(path, Value());
    return Value();
  });
  os.o->attrs["unlink"] = os.o->attrs["remove"];
  Value osPath = Interp::nameSpace("os.path");
  osPath.o->openAttrs = false;
  def(osPath, "join", [](Interp &, Args &a, Kwargs &) {
    if (a.empty())
      throw PyError("TypeError: os.path.join needs a path");
    std::filesystem::path result(a[0].text());
    for (size_t i = 1; i < a.size(); ++i)
      result /= a[i].text();
    return Value::str(result.generic_string());
  });
  def(osPath, "exists", [v](Interp &, Args &a, Kwargs &) {
    bool ok = false;
    v->readFile(a.at(0).text(), ok);
    return Value::boolean(ok || v->isDir(a.at(0).text()));
  });
  def(osPath, "isdir", [v](Interp &, Args &a, Kwargs &) {
    return Value::boolean(v->isDir(a.at(0).text()));
  });
  def(os, "makedirs", [v](Interp &, Args &a, Kwargs &) {
    v->makeDirs(a.at(0).text());
    return Value();
  });
  def(os, "listdir", [v](Interp &, Args &a, Kwargs &) {
    std::vector<Value> out;
    for (auto &entry : v->listDir(a.at(0).text()))
      out.push_back(Value::str(entry));
    return makeList(out);
  });
  os.o->attrs["path"] = osPath;
  if (py.builtins.count("time")) {
    Value timeMod = py.builtins["time"];
    if (timeMod.isObj())
      def(timeMod, "sleep", [](Interp &, Args &, Kwargs &) { return Value(); });
  }
  Value environment = Interp::nameSpace("os.environ");
  environment.o->openAttrs = false;
  def(environment, "get", [](Interp &, Args &a, Kwargs &) {
    const char *value = SDL_getenv(a.at(0).text().c_str());
    return value ? Value::str(value) : a.size() > 1 ? a[1] : Value();
  });
  os.o->attrs["environ"] = environment;
  Value subprocess = py.builtins.at("subprocess");
  subprocess.o->openAttrs = false;
  def(subprocess, "check_output", [](Interp &, Args &, Kwargs &) -> Value {
    throw PyError(
        "OSError: launching external processes is unavailable in this port");
  });

  Value shutil = Interp::nameSpace("shutil");
  shutil.o->openAttrs = false;
  def(shutil, "copyfile", [v](Interp &, Args &a, Kwargs &) {
    bool ok = false;
    const auto data = v->readFile(a.at(0).text(), ok);
    if (!ok)
      throw PyError("IOError: copy source does not exist");
    v->setVirtualFile(a.at(1).text(), Value::str(data));
    return a.at(1);
  });
  py.registerModule("shutil", shutil);
  py.builtins["shutil"] = shutil;

  // Characters.
  auto character = [](bool dynamic) {
    return [dynamic](Interp &, Args &a, Kwargs &kw) {
      Value c = makeInstance("Character");
      c.o->attrs["name"] = argOr(a, 0, kw, "name");
      c.o->attrs["dynamic"] = Value::boolean(dynamic);
      c.o->attrs["display_args"] = makeDict();
      c.o->attrs["what_args"] = makeDict();
      for (auto &[k, val] : kw)
        if (k != "name")
          c.o->attrs[k] = val;
      c.o->attrs["add_history"] =
          makeFunction("add_history", [](Interp &py, Args &a, Kwargs &kw) {
            return py.call(py.lookup("_kt_add_history"),
                           {argOr(a, 0, kw, "kind", Value::str("adv")),
                            argOr(a, 1, kw, "who"), argOr(a, 2, kw, "what")});
          });
      return c;
    };
  };
  py.def("Character", character(false));
  py.def("DynamicCharacter", character(true));

  // Displayables.
  Value im = Interp::nameSpace("im");
  py.set("im", im);
  auto composite = [v, disp, size2](bool live) {
    return [v, disp, size2, live](Interp &, Args &a, Kwargs &) {
      double w = 0, h = 0;
      if (!a.empty())
        size2(a[0], w, h);
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
  def(im, "Image", [v, disp](Interp &, Args &a, Kwargs &) {
    return v->dispValue(disp(a.at(0)));
  });
  def(im, "Grayscale", [v, disp](Interp &, Args &a, Kwargs &) {
    return v->dispValue(makeGrayscaleDisp(disp(a.at(0))));
  });
  def(im, "Flip", [v, disp](Interp &, Args &a, Kwargs &kw) {
    if (a.empty())
      return Value();
    auto t = std::make_shared<AtlTemplate>();
    t->name = "Flip";
    t->child = disp(a[0]);
    if (argOr(a, 1, kw, "horizontal").truthy())
      t->props.emplace_back("xzoom", Value::real(-1.0));
    if (argOr(a, 2, kw, "vertical").truthy())
      t->props.emplace_back("yzoom", Value::real(-1.0));
    return atlValue(t, v);
  });
  def(im, "Crop", [v, disp](Interp &, Args &a, Kwargs &) {
    if (a.empty())
      return Value();
    auto t = std::make_shared<AtlTemplate>();
    t->name = "Crop";
    t->child = disp(a[0]);
    if (a.size() >= 2 && a[1].isObj() && a[1].items().size() >= 4)
      t->props.emplace_back("crop", a[1]);
    else if (a.size() >= 5)
      t->props.emplace_back("crop", makeTuple({a[1], a[2], a[3], a[4]}));
    return atlValue(t, v);
  });
  def(im, "FactorScale", [v, disp](Interp &, Args &a, Kwargs &) {
    if (a.empty())
      return Value();
    auto t = std::make_shared<AtlTemplate>();
    t->name = "FactorScale";
    t->child = disp(a[0]);
    const double sx = a.size() > 1 ? a[1].num() : 1.0;
    const double sy = a.size() > 2 && !a[2].isNone() ? a[2].num() : sx;
    t->props.emplace_back("xzoom", Value::real(sx));
    t->props.emplace_back("yzoom", Value::real(sy));
    return atlValue(t, v);
  });
  def(im, "Alpha", [v, disp](Interp &, Args &a, Kwargs &kw) {
    if (a.empty())
      return Value();
    auto t = std::make_shared<AtlTemplate>();
    t->name = "Alpha";
    t->child = disp(a[0]);
    const double alpha = argOr(a, 1, kw, "alpha", Value::real(1.0)).num();
    t->props.emplace_back("alpha", Value::real(alpha));
    return atlValue(t, v);
  });
  auto passthroughIm = [v, disp](Interp &, Args &a, Kwargs &) {
    return a.empty() ? Value() : v->dispValue(disp(a[0]));
  };
  def(im, "MatrixColor", passthroughIm);
  def(im, "Tile", passthroughIm);
  def(im, "Scale", passthroughIm);
  def(im, "Rotozoom", passthroughIm);
  def(im, "Recolor", passthroughIm);
  def(im, "Sepia", passthroughIm);
  def(im, "Twocolor", passthroughIm);
  def(im, "Map", passthroughIm);
  def(im, "AlphaMask", passthroughIm);
  def(im, "Blur", passthroughIm);
  def(im, "Cache", passthroughIm);
  def(im, "ramp", [](Interp &, Args &, Kwargs &) { return Value(); });
  py.exec(
      "class _KtImMatrix(object):\n"
      "    def __mul__(self, other): return self\n"
      "    def __rmul__(self, other): return self\n"
      "class _KtImMatrixNs(object):\n"
      "    def __call__(self, *a, **kw): return _KtImMatrix()\n"
      "    def identity(self, *a, **kw): return _KtImMatrix()\n"
      "    def saturation(self, *a, **kw): return _KtImMatrix()\n"
      "    def desaturate(self, *a, **kw): return _KtImMatrix()\n"
      "    def tint(self, *a, **kw): return _KtImMatrix()\n"
      "    def colorize(self, *a, **kw): return _KtImMatrix()\n"
      "    def invert(self, *a, **kw): return _KtImMatrix()\n"
      "    def brightness(self, *a, **kw): return _KtImMatrix()\n"
      "    def contrast(self, *a, **kw): return _KtImMatrix()\n"
      "    def opacity(self, *a, **kw): return _KtImMatrix()\n"
      "    def hue(self, *a, **kw): return _KtImMatrix()\n"
      "im.matrix = _KtImMatrixNs()\n"
      "class _KtLayout(object):\n"
      "    def provides(self, *a, **kw): pass\n"
      "    def button(self, *a, **kw): return None\n"
      "    def label(self, *a, **kw): return None\n"
      "    def prompt(self, *a, **kw): return None\n"
      "    def yesno_screen(self, *a, **kw): return False\n"
      "    def invoke_yesno_prompt(self, *a, **kw): return False\n"
      "    def are_you_sure(self, *a, **kw): return True\n"
      "    def navigation(self, *a, **kw): pass\n"
      "    def __getattr__(self, k): return lambda *a, **kw: None\n"
      "layout = _KtLayout()\n"
      "theme = _KtLayout()\n"
      "anim = _KtLayout()\n"
      "last_scene_label = ''\n"
      "last_visited_label = ''\n"
      "def SnowBlossom(d, *a, **kw): return d\n"
      "def SplineMotion(*a, **kw): return lambda d=None, *a2, **kw2: d\n"
      "def Motion(*a, **kw): return lambda d=None, *a2, **kw2: d\n"
      "def Pan(*a, **kw): return lambda d=None, *a2, **kw2: d\n"
      "def MoveFactory(*a, **kw): return lambda *a2, **kw2: None\n"
      "def MoveIn(*a, **kw): return lambda *a2, **kw2: None\n"
      "def MoveOut(*a, **kw): return lambda *a2, **kw2: None\n"
      "def ZoomInOut(*a, **kw): return lambda *a2, **kw2: None\n"
      "def RevolveInOut(*a, **kw): return lambda *a2, **kw2: None\n"
      "def ComposeTransition(t=None, *a, **kw): return t\n"
      "def DynamicDisplayable(fn, *a, **kw): return None\n"
      "class _KtStyle(object):\n"
      "    def __call__(self, *a, **kw): return None\n"
      "    def __getitem__(self, k): return self\n"
      "    def __setitem__(self, k, v): pass\n"
      "class _KtStyleNs(object):\n"
      "    def __init__(self, b): self.__dict__['_b'] = b\n"
      "    def __getattr__(self, k):\n"
      "        b = self.__dict__['_b']\n"
      "        v = getattr(b, k, None)\n"
      "        if v is None:\n"
      "            v = _KtStyle()\n"
      "            setattr(b, k, v)\n"
      "        return v\n"
      "    def __setattr__(self, k, v): setattr(self.__dict__['_b'], k, v)\n"
      "style = _KtStyleNs(style)\n"
      "def _ease_time_warp(x): return x\n"
      "def _ease_in_time_warp(x): return x\n"
      "def _ease_out_time_warp(x): return x\n"
      "def _easein_time_warp(x): return x\n"
      "def _easeout_time_warp(x): return x\n"
      "NARRATOR_NAME = None\n"
      "def _kt_move_transitions(prefix, delay=0.5, *a, **kw):\n"
      "    g = globals()\n"
      "    tr = Dissolve(delay)\n"
      "    for suf in ('', 'in', 'out', 'enter', 'leave',\n"
      "                'inright', 'inleft', 'intop', 'inbottom',\n"
      "                'outright', 'outleft', 'outtop', 'outbottom'):\n"
      "        g[prefix + suf] = tr\n"
      "define.move_transitions = _kt_move_transitions\n"
      "class _KtStore(object):\n"
      "    def __getattr__(self, k):\n"
      "        g = globals()\n"
      "        if k in g: return g[k]\n"
      "        return None\n"
      "    def __setattr__(self, k, v): globals()[k] = v\n"
      "store = _KtStore()\n"
      "class _KtObject(object):\n"
      "    pass\n"
      "object = _KtObject\n"
      "class MultiPersistent(object):\n"
      "    def __init__(self, name=''): self._name = name\n"
      "    def save(self): pass\n"
      "    def __getattr__(self, k): return None\n");
  py.def("Image", [v, disp](Interp &, Args &a, Kwargs &) {
    return v->dispValue(disp(a.at(0)));
  });
  py.def("Frame", [v, disp](Interp &, Args &a, Kwargs &) {
    return v->dispValue(disp(a.at(0)));
  });
  py.def("Null", [v](Interp &, Args &a, Kwargs &kw) {
    return v->dispValue(makeNullDisp(argOr(a, 0, kw, "width").num(),
                                     argOr(a, 1, kw, "height").num()));
  });
  py.def("AnimatedMask", [v, disp](Interp &, Args &a, Kwargs &kw) {
    DispP child = a.empty() ? makeNullDisp(0, 0) : disp(a[0]);
    const double oc = argOr(a, 3, kw, "oc", Value::real(0.0)).num();
    const bool moving =
        argOr(a, 5, kw, "moving", Value::boolean(true)).truthy();
    const double speed = argOr(a, 6, kw, "speed", Value::real(1.0)).num();
    const double freq = argOr(a, 7, kw, "frequency", Value::real(1.0)).num();
    const double amount = argOr(a, 8, kw, "amount", Value::real(0.5)).num();
    return v->dispValue(
        makeAnimatedMaskDisp(child, moving, speed, freq, amount, oc));
  });
  py.def("ParticleBurst", [v](Interp &, Args &, Kwargs &) {
    return v->dispValue(makeNullDisp(0, 0));
  });
  // Dialogue history (renpy.character.HistoryEntry); the script edits it in
  // Act 2.
  py.exec("class HistoryEntry(object):\n"
          "    def __init__(self, kind='adv', who=None, what=None):\n"
          "        self.kind = kind\n"
          "        self.who = who\n"
          "        self.what = what\n"
          "_history = True\n"
          "_history_list = []\n"
          "def _kt_add_history(kind, who, what):\n"
          "    if not _history:\n"
          "        return\n"
          "    _history_list.append(HistoryEntry(kind, who, what))\n"
          "    while len(_history_list) > (config.history_length or 250):\n"
          "        _history_list.pop(0)\n");
  py.exec("class _KtSprite(object):\n"
          "    def __init__(self, d=None):\n"
          "        self.d = d\n"
          "        self.x = 0\n"
          "        self.y = 0\n"
          "        self.destroyed = False\n"
          "    def destroy(self):\n"
          "        self.destroyed = True\n"
          "class SpriteManager(object):\n"
          "    def __init__(self, update=None, event=None, predict=None, "
          "ignore_time=False, **kw):\n"
          "        self.update_fn = update\n"
          "        self.children = []\n"
          "    def create(self, d):\n"
          "        s = _KtSprite(d)\n"
          "        self.children.append(s)\n"
          "        return s\n"
          "    def redraw(self, delay=0):\n"
          "        pass\n");
  py.def("Solid", [v](Interp &, Args &a, Kwargs &kw) {
    return v->dispValue(
        makeSolidDisp(parseColor(argOr(a, 0, kw, "color").text())));
  });
  py.def("LiveTile", [v, disp](Interp &, Args &a, Kwargs &) {
    return v->dispValue(makeTileDisp(disp(a.at(0))));
  });
  py.def("ConditionSwitch", [v, disp](Interp &, Args &a, Kwargs &) {
    std::vector<std::pair<std::string, DispP>> cases;
    for (size_t i = 0; i + 1 < a.size(); i += 2)
      cases.emplace_back(a[i].text(), disp(a[i + 1]));
    return v->dispValue(makeSwitchDisp(std::move(cases)));
  });
  py.def("Text", [v](Interp &, Args &a, Kwargs &kw) {
    TextStyle st =
        v->styleNamed(kwGet(kw, "style", Value::str("default")).text());
    if (const Value *s = kwFind(kw, "size"))
      st.size = float(s->num());
    if (const Value *c = kwFind(kw, "color"))
      st.color = parseColor(c->text());
    Value xa = kwGet(kw, "xalign"), ya = kwGet(kw, "yalign");
    return v->dispValue(makeTextDisp(a.empty() ? "" : a[0].text(), st, xa, ya));
  });
  py.def("ParameterizedText", [](Interp &, Args &, Kwargs &kw) {
    Value p = makeInstance("ParameterizedText");
    p.o->attrs["style"] = Value::str("default");
    for (auto &[k, val] : kw)
      p.o->attrs[k] = val;
    return p;
  });
  py.def("At", [v, disp](Interp &, Args &a, Kwargs &) -> Value {
    if (a.empty())
      return v->dispValue(makeNullDisp(0, 0));
    DispP child = disp(a[0]);
    if (a.size() < 2)
      return v->dispValue(child);
    return v->dispValue(child);
  });
  py.def("Borders", [](Interp &, Args &a, Kwargs &) {
    Value b = makeInstance("Borders");
    b.o->items = a;
    return b;
  });
  py.def("Transform", [v, disp](Interp &, Args &a, Kwargs &kw) {
    auto t = std::make_shared<AtlTemplate>();
    t->name = "Transform";
    if (!a.empty())
      t->child = disp(a[0]);
    for (auto &[k, val] : kw) {
      if (k == "child")
        t->child = disp(val);
      else
        t->props.emplace_back(k, val);
    }
    return atlValue(t, v);
  });

  // Transitions.
  py.def("Dissolve", [](Interp &, Args &a, Kwargs &kw) {
    return transitionValue(
        dissolve(argOr(a, 0, kw, "time").num(), kwGet(kw, "alpha").truthy()));
  });
  py.def("Pause", [](Interp &, Args &a, Kwargs &kw) {
    auto t = std::make_shared<Transition>();
    t->k = Transition::K::Pause;
    t->time = argOr(a, 0, kw, "delay").num();
    return transitionValue(t);
  });
  py.def("ImageDissolve", [](Interp &, Args &a, Kwargs &kw) {
    auto t = std::make_shared<Transition>();
    t->k = Transition::K::ImageDissolve;
    t->mask = argOr(a, 0, kw, "image").text();
    t->time = argOr(a, 1, kw, "time").num();
    t->ramp = argOr(a, 2, kw, "ramplen", Value::integer(8)).num();
    t->reverse = kwGet(kw, "reverse").truthy();
    return transitionValue(t);
  });
  auto multiple = [v](const std::vector<Value> &items) {
    auto t = std::make_shared<Transition>();
    t->k = Transition::K::Multiple;
    for (size_t i = 0; i < items.size(); ++i) {
      const Value &it = items[i];
      if (i % 2 == 1) {
        t->steps.push_back(v->toTransition(it));
        continue;
      }
      Transition::SceneRef r;
      if (it.t == Value::T::Bool)
        r.kind = it.b ? 1 : 0;
      else {
        r.kind = 2;
        r.disp = v->toDisp(it);
      }
      t->scenes.push_back(r);
    }
    return t;
  };
  py.def("MultipleTransition", [multiple](Interp &, Args &a, Kwargs &) {
    return transitionValue(
        multiple(a.empty() ? std::vector<Value>{} : a[0].items()));
  });
  py.def("CropMove", [](Interp &, Args &a, Kwargs &kw) {
    return transitionValue(
        dissolve(argOr(a, 0, kw, "time", Value::real(0.5)).num()));
  });
  py.def("MoveTransition", [](Interp &, Args &a, Kwargs &kw) {
    return transitionValue(
        dissolve(argOr(a, 0, kw, "delay", Value::real(0.5)).num()));
  });
  py.def("Fade", [v, multiple](Interp &, Args &a, Kwargs &kw) {
    const double out = argOr(a, 0, kw, "out_time").num(),
                 hold = argOr(a, 1, kw, "hold_time").num(),
                 in = argOr(a, 2, kw, "in_time").num();
    Value color = v->dispValue(makeSolidDisp(
        parseColor(kwGet(kw, "color", Value::str("#000")).text())));
    auto pauseT = std::make_shared<Transition>();
    pauseT->k = Transition::K::Pause;
    pauseT->time = hold;
    return transitionValue(
        multiple({Value::boolean(false), transitionValue(dissolve(out)), color,
                  transitionValue(pauseT), color, transitionValue(dissolve(in)),
                  Value::boolean(true)}));
  });
  // A Movie displayable. The VM cannot render video itself; it keeps the
  // metadata and the presentation layer decides how to display the asset.
  auto movieValue = [](Interp &, Args &a, Kwargs &kw) {
    Value r = makeInstance("Movie");
    r.o->attrs["filename"] = argOr(a, 0, kw, "filename");
    r.o->attrs["size"] = kwGet(kw, "size");
    r.o->attrs["fps"] = kwGet(kw, "fps", Value::real(24.0));
    r.o->attrs["loop"] = kwGet(kw, "loop");
    return r;
  };
  py.def("Movie", movieValue);
  py.def("Return", [](Interp &, Args &a, Kwargs &) {
    Value r = makeInstance("Return");
    r.o->attrs["value"] = a.empty() ? Value::boolean(true) : a[0];
    return r;
  });
  // Quit(confirm=...): the host quits the application when a screen returns it.
  py.def("Quit", [](Interp &, Args &a, Kwargs &kw) {
    Value r = makeInstance("Quit");
    r.o->attrs["confirm"] = argOr(a, 0, kw, "confirm", Value::boolean(true));
    return r;
  });
  py.def("__screenargs", [](Interp &, Args &a, Kwargs &kw) {
    Value d = makeDict();
    for (auto &[k, val] : kw)
      d.o->dict.emplace_back(Value::str(k), val);
    return makeTuple({makeList(a), d});
  });

  // The renpy module.
  Value renpy = Interp::nameSpace("renpy");
  renpy.o->attrs["Movie"] = py.lookup("Movie");
  py.set("renpy", renpy);
  py.builtins["renpy"] = renpy;
  py.registerModule("renpy", renpy);
  auto sub = [&](Value parent, const char *name) {
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
  py.exec("class _KtDisplayable(object):\n"
          "    def __init__(self, *a, **kw): pass\n"
          "    def render(self, w, h, st, at): return None\n"
          "    def event(self, ev, x, y, st): return None\n"
          "    def visit(self): return []\n"
          "class _KtTransition(_KtDisplayable):\n"
          "    def __init__(self, delay=0.0, **kw): self.delay = delay\n"
          "def _kt_curry(fn):\n"
          "    def _curried(*a, **kw):\n"
          "        return lambda *a2, **kw2: fn(*(a + a2), **{**kw, **kw2})\n"
          "    return _curried\n"
          "class ADVCharacter(object):\n"
          "    def __init__(self, name=None, kind=None, **kw):\n"
          "        self.name = name\n"
          "        self.kind = kind\n"
          "        self.who_args = {}\n"
          "        self.what_args = {}\n"
          "        self.window_args = {}\n"
          "        self.show_args = {}\n"
          "        self.display_args = {}\n"
          "        self.show_function = kw.get('show_function', None)\n"
          "        for k, val in kw.items(): setattr(self, k, val)\n"
          "    def __call__(self, what, **kw):\n"
          "        return renpy.say(self, what, **kw)\n"
          "class NVLCharacter(ADVCharacter):\n"
          "    pass\n"
          "nvl = NVLCharacter(None)\n");
  renpy.o->attrs["Displayable"] = py.lookup("_KtDisplayable");
  renpy.o->attrs["curry"] = py.lookup("_kt_curry");
  def(renpy, "version", [](Interp &, Args &a, Kwargs &kw) {
    if (argOr(a, 0, kw, "tuple").truthy())
      return makeTuple({Value::integer(6), Value::integer(13),
                        Value::integer(12), Value::integer(0)});
    return Value::str("Ren'Py 6.13.12");
  });
  def(renpy, "image", [v](Interp &, Args &a, Kwargs &) {
    if (a.size() < 2)
      return Value();
    std::string key;
    if (a[0].isObj() && !a[0].items().empty()) {
      for (const auto &part : a[0].items())
        key += (key.empty() ? "" : " ") + part.text();
    } else {
      key = a[0].text();
    }
    if (!key.empty())
      v->images[key] = a[1];
    return Value();
  });
  def(renpy, "loadable", [v](Interp &, Args &a, Kwargs &) {
    return Value::boolean(!a.empty() && v->assets.has(a[0].text()));
  });
  def(renpy, "has_label", [v](Interp &, Args &a, Kwargs &) {
    return Value::boolean(!a.empty() &&
                          v->script.label(a[0].text()) != nullptr);
  });
  def(renpy, "Keymap", [](Interp &, Args &, Kwargs &) { return Value(); });
  def(renpy, "current_interact_type",
      [](Interp &, Args &, Kwargs &) { return Value::str("say"); });
  def(renpy, "game_menu", [](Interp &, Args &, Kwargs &) { return Value(); });
  if (py.has("ParameterizedText"))
    renpy.o->attrs["ParameterizedText"] = py.lookup("ParameterizedText");
  if (py.has("At"))
    renpy.o->attrs["At"] = py.lookup("At");
  if (py.builtins.count("random"))
    renpy.o->attrs["random"] = py.builtins["random"];
  Value game = sub(renpy, "game");
  Value prefs = sub(game, "preferences");
  prefs.o->attrs["fullscreen"] = Value::boolean(false);
  prefs.o->attrs["afm_enable"] = Value::boolean(false);
  py.set("_preferences", prefs);
  game.o->attrs["persistent"] = vm.persistent;
  Value loadsave = sub(renpy, "loadsave");
  Value location = sub(loadsave, "location");
  location.o->attrs["locations"] = makeList({Value::integer(0)});
  def(location, "unlink_persistent", [v](Interp &, Args &, Kwargs &) {
    v->persistent.o->attrs.clear();
    return Value();
  });
  Value rpersist = sub(renpy, "persistent");
  rpersist.o->attrs["should_save_persistent"] = Value::boolean(true);
  Value context = Interp::nameSpace("context");
  def(renpy, "context",
      [context](Interp &, Args &, Kwargs &) { return context; });
  Value statements = sub(renpy, "statements");
  def(statements, "register",
      [](Interp &, Args &, Kwargs &) { return Value(); });
  def(renpy, "atl_warper",
      [](Interp &, Args &a, Kwargs &) { return a.empty() ? Value() : a[0]; });

  auto audioSpecs = [](const Value &f) -> std::vector<std::string> {
    std::vector<std::string> out;
    if (f.isObj() && !f.items().empty()) {
      for (const auto &it : f.items())
        if (!it.text().empty())
          out.push_back(it.text());
    } else if (!f.text().empty()) {
      out.push_back(f.text());
    }
    return out;
  };
  Value music = sub(renpy, "music");
  def(music, "play", [v, audioSpecs](Interp &, Args &a, Kwargs &kw) {
    const std::string ch =
        argOr(a, 1, kw, "channel", Value::str("music")).text();
    const auto specs = audioSpecs(argOr(a, 0, kw, "filenames"));
    if (specs.empty())
      return Value();
    Value loop = kwGet(kw, "loop");
    const bool loopFlag = loop.isNone() ? ch != "sound" : loop.truthy();
    if (const Value *ic = kwFind(kw, "if_changed");
        ic && ic->truthy() && v->backend.playing(ch) == specs[0])
      return Value();
    v->backend.play(ch, specs[0], kwGet(kw, "fadein").num(),
                    specs.size() == 1 ? loopFlag : false,
                    kwGet(kw, "fadeout").num());
    for (size_t i = 1; i < specs.size(); ++i)
      v->backend.queue(ch, specs[i], i + 1 == specs.size() ? loopFlag : false);
    return Value();
  });
  def(music, "queue", [v, audioSpecs](Interp &, Args &a, Kwargs &kw) {
    const std::string ch =
        argOr(a, 1, kw, "channel", Value::str("music")).text();
    const auto specs = audioSpecs(argOr(a, 0, kw, "filenames"));
    Value loop = kwGet(kw, "loop");
    const bool loopFlag = loop.isNone() ? ch != "sound" : loop.truthy();
    for (size_t i = 0; i < specs.size(); ++i)
      v->backend.queue(ch, specs[i], i + 1 == specs.size() ? loopFlag : false);
    return Value();
  });
  def(music, "stop", [v](Interp &, Args &a, Kwargs &kw) {
    v->backend.stop(argOr(a, 0, kw, "channel", Value::str("music")).text(),
                    kwGet(kw, "fadeout").num());
    return Value();
  });
  def(music, "get_playing", [v](Interp &, Args &a, Kwargs &kw) {
    std::string p = v->backend.playing(
        argOr(a, 0, kw, "channel", Value::str("music")).text());
    return p.empty() ? Value() : Value::str(p);
  });
  def(music, "get_pos", [v](Interp &, Args &a, Kwargs &kw) {
    const std::string ch =
        argOr(a, 0, kw, "channel", Value::str("music")).text();
    if (v->backend.playing(ch).empty())
      return Value();
    return Value::real(v->backend.position(ch));
  });
  def(music, "register_channel",
      [](Interp &, Args &, Kwargs &) { return Value(); });
  def(music, "set_volume", [](Interp &, Args &, Kwargs &) { return Value(); });
  Value sound = sub(renpy, "sound");
  sound.o->attrs = music.o->attrs;
  def(sound, "play", [v, audioSpecs](Interp &, Args &a, Kwargs &kw) {
    const std::string ch =
        argOr(a, 1, kw, "channel", Value::str("sound")).text();
    const auto specs = audioSpecs(a.at(0));
    if (!specs.empty()) {
      v->backend.play(ch, specs[0], kwGet(kw, "fadein").num(), false,
                      kwGet(kw, "fadeout").num());
      for (size_t i = 1; i < specs.size(); ++i)
        v->backend.queue(ch, specs[i], false);
    }
    return Value();
  });
  def(renpy, "play", [v, audioSpecs](Interp &, Args &a, Kwargs &kw) {
    if (a.empty() || a[0].isNone())
      return Value();
    const std::string ch =
        argOr(a, 1, kw, "channel", Value::str("sound")).text();
    const auto specs = audioSpecs(a[0]);
    if (!specs.empty()) {
      v->backend.play(ch, specs[0], 0, false);
      for (size_t i = 1; i < specs.size(); ++i)
        v->backend.queue(ch, specs[i], false);
    }
    return Value();
  });

  auto nameOf = [](const Value &n) {
    if (n.isStr())
      return splitWords(n.s);
    std::vector<std::string> out;
    for (auto &p : n.items())
      out.push_back(p.text());
    return out;
  };
  def(renpy, "show", [v, nameOf](Interp &, Args &a, Kwargs &kw) {
    std::vector<std::string> name = nameOf(a.at(0));
    if (name.empty())
      return Value();
    std::vector<Value> at;
    for (auto &x : kwGet(kw, "at_list").items())
      at.push_back(x);
    Value what = kwGet(kw, "what");
    DispP d = what.isNone() ? v->imageNamed(name) : v->toDisp(what);
    if (!d) {
      v->backend.log("renpy.show: unknown image " + a[0].text());
      d = makeNullDisp(0, 0);
    }
    Value tag = kwGet(kw, "tag");
    Value z = kwGet(kw, "zorder");
    std::string behind;
    for (auto &b : kwGet(kw, "behind").items())
      behind = b.text();
    v->showDisp(tag.isNone() ? name[0] : tag.text(), name, d, at, behind,
                int(z.integer()), !z.isNone());
    return Value();
  });
  def(renpy, "hide", [v, nameOf](Interp &, Args &a, Kwargs &) {
    auto name = nameOf(a.at(0));
    if (!name.empty())
      v->hideTag(name[0]);
    return Value();
  });
  def(renpy, "scene", [v](Interp &, Args &, Kwargs &) {
    v->scene.master.clear();
    v->scene.masterXf.reset();
    return Value();
  });
  def(renpy, "showing", [v, nameOf](Interp &, Args &a, Kwargs &) {
    auto name = nameOf(a.at(0));
    for (auto &s : v->scene.master)
      if (!s.hiding && !name.empty() && s.tag == name[0])
        return Value::boolean(true);
    return Value::boolean(false);
  });
  def(renpy, "pause", [v](Interp &, Args &a, Kwargs &kw) {
    Value t = argOr(a, 0, kw, "delay");
    const bool hard = kwGet(kw, "hard").truthy();
    if (t.isNone())
      v->pause(-1, true);
    else if (t.num() > 0)
      v->pause(t.num(), !hard && v->py.lookup("_dismiss_pause").truthy());
    return Value::boolean(false);
  });
  // renpy.movie_cutscene(filename, ...): yield a MovieCutscene interaction so
  // the host can play the asset through MediaPlayer. The returned action value
  // is also stored on the interaction for hosts that inspect values directly.
  def(renpy, "movie_cutscene", [v](Interp &, Args &a, Kwargs &kw) {
    const std::string path = argOr(a, 0, kw, "filename").text();
    Value r = makeInstance("MovieCutscene");
    r.o->attrs["filename"] = Value::str(path);
    r.o->attrs["path"] = Value::str(path);
    r.o->attrs["first_frame"] = kwGet(kw, "first_frame");
    r.o->attrs["stills"] = kwGet(kw, "stills");
    r.o->attrs["end_delay"] = kwGet(kw, "end_delay");
    r.o->attrs["delay"] = kwGet(kw, "delay");
    r.o->attrs["loops"] = kwGet(kw, "loops");
    v->movieCutscene(path, r);
    return r;
  });
  def(renpy, "with_statement", [v](Interp &, Args &a, Kwargs &) {
    if (auto t = a.empty() ? nullptr : v->toTransition(a[0]))
      v->transition(t);
    else
      v->committed = v->scene;
    return Value();
  });
  def(renpy, "jump", [](Interp &, Args &a, Kwargs &) -> Value {
    throw JumpSignal{a.at(0).text(), false};
  });
  def(renpy, "call", [](Interp &, Args &a, Kwargs &) -> Value {
    throw JumpSignal{a.at(0).text(), true};
  });
  def(renpy, "quit", [](Interp &, Args &a, Kwargs &kw) -> Value {
    throw QuitSignal{argOr(a, 0, kw, "relaunch").truthy()};
  });
  def(renpy, "utter_restart",
      [](Interp &, Args &, Kwargs &) -> Value { throw RestartSignal{}; });
  def(renpy, "full_restart",
      [](Interp &, Args &, Kwargs &) -> Value { throw RestartSignal{}; });
  def(renpy, "save_persistent", [v](Interp &, Args &, Kwargs &) {
    v->backend.savePersistent(v->savePersistentText());
    return Value();
  });
  def(renpy, "say", [v](Interp &, Args &a, Kwargs &) {
    v->say(a.at(0), a.at(1).text(), Value());
    return Value();
  });
  def(renpy, "display_menu", [v](Interp &, Args &a, Kwargs &kw) {
    std::vector<Interaction::Item> items;
    std::string caption;
    const Value list = argOr(a, 0, kw, "items");
    for (auto &it : list.items()) {
      const auto &p = it.items();
      if (p.size() < 2)
        continue;
      if (p[1].isNone())
        caption = p[0].text();
      else
        items.push_back({v->interpolate(p[0].text()), p[1]});
    }
    if (!kwGet(kw, "interact", Value::boolean(true)).truthy()) {
      for (auto &item : items)
        item.enabled = false;
      v->inertMenu_ = std::move(items);
      return Value();
    }
    return v->menu(items, caption,
                   kwGet(kw, "screen", Value::str("choice")).text());
  });
  def(renpy, "call_screen", [v](Interp &, Args &a, Kwargs &kw) {
    Args rest(a.begin() + (a.empty() ? 0 : 1), a.end());
    return v->callScreen(a.at(0).text(), rest, kw);
  });
  def(renpy, "file", [v](Interp &, Args &a, Kwargs &) {
    bool ok = false;
    std::string data = v->readFile(a.at(0).text(), ok);
    if (!ok)
      throw PyError("IOError: " + a.at(0).text());
    return v->fileObject(a.at(0).text(), data, false);
  });
  def(renpy, "exists", [v](Interp &, Args &a, Kwargs &) {
    bool ok = false;
    v->readFile(a.at(0).text(), ok);
    return Value::boolean(ok);
  });
  def(renpy, "get_return_stack", [v](Interp &, Args &, Kwargs &) {
    std::vector<Value> out(v->callStack_.size(), Value::str("return"));
    return makeList(out);
  });
  def(renpy, "pop_call", [v](Interp &, Args &, Kwargs &) {
    if (!v->callStack_.empty())
      v->callStack_.pop_back();
    return Value();
  });
  def(renpy, "error", [v](Interp &, Args &a, Kwargs &) {
    v->backend.log("renpy.error: " + (a.empty() ? std::string() : a[0].text()));
    return Value();
  });
  def(renpy, "displayable", [v](Interp &, Args &a, Kwargs &) {
    return v->dispValue(v->toDisp(a.at(0)));
  });
  def(renpy, "variant", [](Interp &, Args &a, Kwargs &) {
    const std::string n = a.empty() ? "" : a[0].text();
    return Value::boolean(n == "pc" || n == "large");
  });
  // Screen effects (tear, invert) are drawn by the host from a frame capture;
  // the screenshot the script passes along is only a placeholder.
  Value display = sub(renpy, "display");
  Value rtransition = sub(display, "transition");
  rtransition.o->attrs["Transition"] = py.lookup("_KtTransition");
  Value draw = sub(display, "draw");
  def(draw, "screenshot", [](Interp &, Args &, Kwargs &) {
    Value shot = makeInstance("Screenshot");
    shot.o->attrs["get_width"] =
        makeFunction("get_width", [](Interp &, Args &, Kwargs &) {
          return Value::integer(1280);
        });
    shot.o->attrs["get_height"] =
        makeFunction("get_height", [](Interp &, Args &, Kwargs &) {
          return Value::integer(720);
        });
    shot.o->attrs["subsurface"] = makeFunction(
        "subsurface", [shot](Interp &, Args &, Kwargs &) { return shot; });
    return shot;
  });
  def(draw, "set_mouse_pos", [v](Interp &, Args &a, Kwargs &) {
    v->backend.setMousePosition(a.at(0).num(), a.at(1).num());
    return Value();
  });
  def(renpy, "get_mouse_pos", [v](Interp &, Args &, Kwargs &) {
    double x = 0, y = 0;
    v->backend.mousePosition(x, y);
    return makeTuple({Value::integer(int64_t(x)), Value::integer(int64_t(y))});
  });
  def(sub(display, "behavior"), "clear_keymap_cache",
      [](Interp &, Args &, Kwargs &) { return Value(); });
  def(renpy, "get_physical_size", [](Interp &, Args &, Kwargs &) {
    return makeTuple({Value::integer(1280), Value::integer(720)});
  });
  def(renpy, "hide_screen", [v](Interp &, Args &a, Kwargs &) {
    const std::string name = a.at(0).text();
    auto &s = v->scene.screens;
    s.erase(
        std::remove_if(s.begin(), s.end(),
                       [&](const ScreenState &x) { return x.name == name; }),
        s.end());
    return Value();
  });
  def(renpy, "show_screen", [v](Interp &, Args &a, Kwargs &kw) {
    const std::string name = a.at(0).text();
    auto &s = v->scene.screens;
    s.erase(
        std::remove_if(s.begin(), s.end(),
                       [&](const ScreenState &x) { return x.name == name; }),
        s.end());
    s.push_back({name, Args(a.begin() + 1, a.end()), kw, v->now()});
    return Value();
  });
  def(renpy, "get_screen", [v](Interp &, Args &a, Kwargs &) {
    const std::string name = a.at(0).text();
    for (auto &screen : v->scene.screens)
      if (screen.name == name)
        return Value::boolean(true);
    return Value();
  });
  def(renpy, "current_screen", [v](Interp &, Args &, Kwargs &) {
    Value screen = makeInstance("ScreenDisplayable");
    const std::string name =
        v->scene.screens.empty() ? "" : v->scene.screens.back().name;
    screen.o->attrs["screen_name"] = makeTuple({Value::str(name)});
    return screen;
  });
  def(renpy, "jump_out_of_context", [](Interp &, Args &a, Kwargs &) -> Value {
    throw JumpSignal{a.at(0).text(), false};
  });
  def(renpy, "call_in_new_context",
      [](Interp &, Args &, Kwargs &) { return Value(); });
  // _window_show / _window_hide: the window statement as functions.
  auto window = [v](bool show) {
    return [v, show](Interp &py, Args &a, Kwargs &kw) {
      v->windowAuto_ = false;
      if (v->scene.window.visible == show)
        return Value();
      const bool given =
          !a.empty() || std::any_of(kw.begin(), kw.end(),
                                    [](auto &k) { return k.first == "trans"; });
      Value trans = !given
                        ? py.getAttr(v->config, show ? "window_show_transition"
                                                     : "window_hide_transition")
                        : argOr(a, 0, kw, "trans");
      v->scene.window.visible = show;
      v->scene.window.who.clear();
      v->scene.window.what.clear();
      v->scene.window.rich = {};
      if (auto tr = v->toTransition(trans))
        v->transition(tr);
      return Value();
    };
  };
  py.def("_window_show", window(true));
  py.def("_window_hide", window(false));

  Value singleton = Interp::nameSpace("singleton");
  def(singleton, "SingleInstance", [](Interp &, Args &, Kwargs &) {
    return makeInstance("SingleInstance");
  });
  py.builtins["singleton"] = singleton;
  py.registerModule("singleton", singleton);
  def(vm.gui, "init", [](Interp &, Args &, Kwargs &) { return Value(); });
  Value build = py.lookup("build");
  for (const char *noop : {"archive", "classify", "documentation", "executable",
                           "package", "clear"})
    def(build, noop, [](Interp &, Args &, Kwargs &) { return Value(); });
  def(renpy, "list_saved_games", [v](Interp &, Args &, Kwargs &) {
    std::vector<Value> slots;
    for (const auto &name : v->backend.savedGames())
      slots.push_back(makeTuple(
          {Value::str(name), Value::str(""), Value(), Value::integer(0)}));
    return makeList(slots);
  });
  def(renpy, "unlink_save", [v](Interp &, Args &a, Kwargs &) {
    v->backend.removeSave(a.at(0).text());
    return Value();
  });
  for (const char *noop :
       {"block_rollback", "restart_interaction", "notify", "free_memory",
        "checkpoint", "predict", "take_screenshot", "clear_game_runtime"})
    def(renpy, noop, [](Interp &, Args &, Kwargs &) { return Value(); });
  def(renpy, "transition", [v](Interp &, Args &a, Kwargs &) {
    if (auto t = a.empty() ? nullptr : v->toTransition(a[0]))
      v->transition(t);
    return Value();
  });

  // ui: the immediate-mode widgets of the poem game.
  Value ui = Interp::nameSpace("ui");
  py.set("ui", ui);
  renpy.o->attrs["ui"] = ui;
  def(ui, "text", [v](Interp &, Args &a, Kwargs &kw) {
    Interaction::Item t;
    t.text = a.empty() ? "" : a[0].text();
    t.value = kwGet(kw, "style", Value::str("default"));
    t.x = float(kwGet(kw, "xpos").num());
    t.y = float(kwGet(kw, "ypos").num());
    v->uiTexts_.emplace_back(
        t.text, makeTuple({t.value, Value::real(t.x), Value::real(t.y)}));
    return Value();
  });
  def(ui, "textbutton", [v](Interp &, Args &a, Kwargs &kw) {
    Interaction::Item b;
    b.text = a.empty() ? "" : a[0].text();
    b.value = kwGet(kw, "clicked");
    b.x = float(kwGet(kw, "xpos").num());
    b.y = float(kwGet(kw, "ypos").num());
    v->uiButtons_.push_back(b);
    return Value();
  });
  def(ui, "returns",
      [](Interp &, Args &a, Kwargs &) { return a.empty() ? Value() : a[0]; });
  for (const char *noop :
       {"vbox",  "hbox",  "close",    "fixed", "saybehavior", "add",
        "null",  "jumps", "calls",    "image", "imagebutton", "window",
        "frame", "bar",   "button",   "timer", "layer",       "keymap",
        "clear", "grid",  "viewport", "side"})
    def(ui, noop, [](Interp &, Args &, Kwargs &) { return Value(); });
  def(ui, "interact", [v](Interp &, Args &, Kwargs &kw) {
    if (v->uiButtons_.empty() && v->uiTexts_.empty()) {
      v->pause(-1, true);
      return Value();
    }
    Interaction in;
    in.k = Interaction::K::Ui;
    in.screen = "poemgame";
    in.items = std::move(v->uiButtons_);
    for (auto &[text, info] : v->uiTexts_) {
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
      dt.o->attrs["now"] = makeFunction("now", [v](Interp &, Args &, Kwargs &) {
        Value d = makeInstance("datetime");
        d.o->attrs["t"] = Value::real(v->now());
        return d;
      });
  }
}

// ---- statements -------------------------------------------------------------

void Vm::runFrom(Node *n) {
  while (n) {
    ++statements;
    if (std::getenv("DDLC_DEBUG_NODES") && statements <= 120)
      std::fprintf(stderr, "RUN %d %s:%d kind=%d cls=%s\n", statements,
                   n->file.c_str(), n->line, int(n->k), n->ast.cls().c_str());
    try {
      n = exec(n);
    } catch (const JumpSignal &j) {
      Node *target = script.label(j.label);
      if (!target) {
        errors.push_back("missing label " + j.label);
        backend.log(errors.back());
        n = n->next;
        continue;
      }
      if (j.call)
        callStack_.push_back(n->next);
      n = target;
    }
  }
}

void Vm::bindLabel(Node *label, const Value &) {
  Value params = label->ast.get("parameters");
  if (!params.isObj()) {
    pendingArgs_.clear();
    pendingKw_.clear();
    return;
  }
  size_t pi = 0;
  for (auto &p : params.get("parameters").items()) {
    const auto &kv = p.items();
    if (kv.empty())
      continue;
    const std::string name = kv[0].text();
    Value val;
    bool set = false;
    if (const Value *k = kwFind(pendingKw_, name.c_str()))
      val = *k, set = true;
    else if (pi < pendingArgs_.size())
      val = pendingArgs_[pi++], set = true;
    if (!set && kv.size() > 1 && !kv[1].isNone())
      val = py.eval(kv[1].text());
    py.set(name, val);
  }
  pendingArgs_.clear();
  pendingKw_.clear();
}

Node *Vm::exec(Node *n) {
  currentNode_ = n;
  auto source = [&](const char *field) {
    return n->ast.get(field).get("source").text();
  };
  switch (n->k) {
  case Node::K::Label:
    bindLabel(n, {});
    return n->next;
  case Node::K::Say: {
    Value who = n->ast.get("who");
    Value whoV;
    if (!who.isNone()) {
      try {
        whoV = py.eval(who.text());
      } catch (const PyError &) {
        whoV = Value::str(who.text());
      }
    }
    const std::string original = n->str("what");
    say(whoV, translate ? translate(n->route, original) : original,
        n->ast.get("attributes"));
    if (!n->ast.get("with_").isNone())
      doWith(n->ast.get("with_").text());
    return n->next;
  }
  case Node::K::Show:
    show(n->ast.get("imspec"), n->ast.get("atl"), false);
    return n->next;
  case Node::K::Scene:
    show(n->ast.get("imspec"), n->ast.get("atl"), true);
    return n->next;
  case Node::K::Hide:
    hide(n->ast.get("imspec"));
    return n->next;
  case Node::K::With:
    doWith(n->str("expr"));
    return n->next;
  case Node::K::Jump: {
    std::string target = n->str("target");
    if (n->ast.get("expression").truthy()) {
      target = py.eval(target).text();
      // Ren'Py resolves expression-based local labels relative to the
      // enclosing global label, not to the script's root namespace.
      if (!target.empty() && target.front() == '.')
        target = n->str("global_label") + target;
    }
    Node *l = script.label(target);
    if (!l) {
      errors.push_back("missing label " + target);
      backend.log(errors.back());
    }
    return l;
  }
  case Node::K::Call: {
    std::string target = n->str("label");
    if (n->ast.get("expression").truthy()) {
      target = py.eval(target).text();
      if (!target.empty() && target.front() == '.')
        target = n->str("global_label") + target;
    }
    pendingArgs_.clear();
    pendingKw_.clear();
    Value args = n->ast.get("arguments");
    if (args.isObj())
      for (auto &a : args.get("arguments").items()) {
        const auto &kv = a.items();
        if (kv.size() < 2)
          continue;
        Value val = py.eval(kv[1].text());
        if (kv[0].isNone())
          pendingArgs_.push_back(val);
        else
          pendingKw_.emplace_back(kv[0].text(), val);
      }
    Node *l = script.label(target);
    if (!l) {
      errors.push_back("missing label " + target);
      backend.log(errors.back());
      pendingArgs_.clear();
      pendingKw_.clear();
      return n->next;
    }
    callStack_.push_back(n->next);
    return l;
  }
  case Node::K::Return: {
    Value e = n->ast.get("expression");
    py.set("_return", e.isNone() ? Value() : py.eval(e.text()));
    if (callStack_.empty())
      return nullptr;
    Node *r = callStack_.back();
    callStack_.pop_back();
    return r;
  }
  case Node::K::Python: {
    try {
      py.exec(source("code"));
    } catch (const PyError &e) {
      errors.push_back(n->file + ":" + std::to_string(n->line) + ": " +
                       e.what());
      backend.log(errors.back());
    }
    return n->next;
  }
  case Node::K::If: {
    const auto &entries = n->ast.get("entries").items();
    for (size_t i = 0; i < entries.size() && i < n->blocks.size(); ++i) {
      const std::string cond =
          entries[i].items().empty() ? "True" : entries[i].items()[0].text();
      bool ok = false;
      try {
        ok = py.eval(cond).truthy();
      } catch (const PyError &e) {
        errors.push_back(n->file + ":" + std::to_string(n->line) + ": " +
                         e.what());
        backend.log(errors.back());
      }
      if (ok)
        return n->blocks[i].empty() ? n->next : n->blocks[i][0];
    }
    return n->next;
  }
  case Node::K::Menu: {
    const auto &items = n->ast.get("items").items();
    std::vector<Interaction::Item> choices;
    std::string caption;
    for (size_t i = 0; i < items.size(); ++i) {
      const auto &it = items[i].items();
      if (it.size() < 3)
        continue;
      if (it[2].isNone()) {
        const std::string original = it[0].text();
        caption = translate ? translate(n->route, original) : original;
        continue;
      }
      bool ok = true;
      // Absent conditions represent unconditional menu choices. Avoid
      // evaluating the string "None" as Python and hiding such options.
      if (!it[1].isNone()) {
        try {
          ok = py.eval(it[1].text()).truthy();
        } catch (const PyError &error) {
          errors.push_back(n->file + ":" + std::to_string(n->line) +
                           ": menu condition: " + error.what());
          backend.log(errors.back());
          ok = false;
        }
      }
      if (!ok)
        continue;
      const std::string original = it[0].text();
      choices.push_back(
          {interpolate(translate ? translate(n->route, original) : original),
           Value::integer(int64_t(i))});
    }
    // When every choice is disabled, Ren'Py falls through instead of
    // presenting an empty modal menu which the player cannot dismiss.
    if (choices.empty())
      return n->next;
    Value r = menu(choices, caption, "choice");
    // A dismissed/cancelled menu is not a selection of item zero.
    if (r.isNone())
      return n->next;
    const size_t i = size_t(r.integer());
    if (i < n->blocks.size() && !n->blocks[i].empty())
      return n->blocks[i][0];
    return n->next;
  }
  case Node::K::User:
    execUser(n->str("line"));
    return n->next;
  case Node::K::ShowLayer: {
    const std::string layer = n->str("layer");
    std::vector<std::shared_ptr<AtlTemplate>> templs;
    for (const auto &a : n->ast.get("at_list").items()) {
      try {
        Value v = py.eval(a.text());
        if (v.cls() == "ATLTransform" && v.o->payload)
          templs.push_back(std::static_pointer_cast<AtlTemplate>(v.o->payload));
      } catch (const PyError &e) {
        backend.log("show layer at " + a.text() + ": " + e.what());
      }
    }
    if (n->ast.get("atl").isObj()) {
      auto t = std::make_shared<AtlTemplate>();
      t->atl = n->ast.get("atl");
      t->name = layer;
      templs.push_back(t);
    }
    if (layer.empty() || layer == "master") {
      if (templs.empty()) {
        scene.masterXf.reset();
      } else {
        DispP cur;
        for (auto &tp : templs) {
          auto x = std::make_shared<TransformDisp>(tp);
          if (cur)
            x->setChild(cur, 0);
          cur = x;
        }
        scene.masterXf = std::static_pointer_cast<TransformDisp>(cur);
        scene.masterXfAt = now();
      }
    }
    return n->next;
  }
  default:
    return n->next;
  }
}

void Vm::execUser(const std::string &line) {
  auto t = tokens(line);
  if (t.empty())
    return;
  const std::string &verb = t[0];
  if (verb == "nvl" && t.size() >= 2) {
    if (t[1] == "clear") {
      py.call(py.lookup("nvl_clear"), {});
      return;
    }
    if (t[1] == "show") {
      py.set("_nvl_shown", Value::boolean(true));
      if (t.size() >= 3) {
        try {
          if (auto tr = toTransition(py.eval(t[2])))
            transition(tr);
        } catch (const PyError &) {
        }
      }
      return;
    }
    if (t[1] == "hide") {
      py.set("_nvl_shown", Value::boolean(false));
      if (t.size() >= 3) {
        try {
          if (auto tr = toTransition(py.eval(t[2])))
            transition(tr);
        } catch (const PyError &) {
        }
      }
      return;
    }
  }
  if ((verb == "jump_in" || verb == "jump_out") && t.size() >= 2) {
    Value parsed = makeDict();
    const bool expression = t[1] == "expression";
    const std::string label = expression && t.size() >= 3 ? t[2] : t[1];
    py.setItem(parsed, Value::str("label"), Value::str(label));
    py.setItem(parsed, Value::str("is_expr"), Value::boolean(expression));
    py.call(
        py.lookup(verb == "jump_in" ? "execute_jump_in" : "execute_jump_out"),
        {parsed});
    return;
  }
  auto audioNames = [&](const std::string &tok) -> std::vector<std::string> {
    if (!tok.empty() &&
        (std::isalpha((unsigned char)tok[0]) || tok[0] == '_') &&
        tok.find_first_of("()[]+") == std::string::npos) {
      if (const Value *a = audio.attr(tok))
        return {a->text()};
    }
    try {
      Value v = py.eval(tok);
      std::vector<std::string> out;
      if (v.isObj() && !v.items().empty()) {
        for (const auto &it : v.items())
          if (!it.text().empty())
            out.push_back(it.text());
      } else if (!v.text().empty()) {
        out.push_back(v.text());
      }
      return out;
    } catch (const PyError &e) {
      backend.log("audio '" + tok + "': " + e.what());
      return {};
    }
  };
  auto option = [&](const char *name, size_t from) -> Value {
    for (size_t i = from; i + 1 < t.size(); ++i)
      if (t[i] == name)
        return py.eval(t[i + 1]);
    return {};
  };
  auto flag = [&](const char *name) {
    return std::find(t.begin(), t.end(), name) != t.end();
  };

  if ((verb == "play" || verb == "queue") && t.size() >= 3) {
    const std::string ch = t[1];
    const auto specs = audioNames(t[2]);
    if (specs.empty())
      return;
    bool loop = ch != "sound";
    if (flag("loop"))
      loop = true;
    if (flag("noloop"))
      loop = false;
    if (verb == "queue") {
      for (size_t i = 0; i < specs.size(); ++i)
        backend.queue(ch, specs[i], i + 1 == specs.size() ? loop : false);
    } else {
      if (flag("if_changed") && backend.playing(ch) == specs[0])
        return;
      backend.play(ch, specs[0], option("fadein", 3).num(),
                   specs.size() == 1 ? loop : false,
                   option("fadeout", 3).num());
      for (size_t i = 1; i < specs.size(); ++i)
        backend.queue(ch, specs[i], i + 1 == specs.size() ? loop : false);
    }
  } else if (verb == "stop" && t.size() >= 2) {
    backend.stop(t[1], option("fadeout", 2).num());
  } else if (verb == "window" && t.size() >= 2) {
    std::string mode = t[1], arg;
    if (auto p = mode.find('('); p != std::string::npos) {
      arg = mode.substr(p);
      mode.resize(p);
    } else if (t.size() >= 3)
      arg = t[2];
    if (mode == "auto") {
      windowAuto_ = true;
      return;
    }
    windowAuto_ = false;
    const bool show = mode == "show";
    if (scene.window.visible == show)
      return;
    Value trans = py.getAttr(config, show ? "window_show_transition"
                                          : "window_hide_transition");
    if (!arg.empty())
      trans = py.eval(arg);
    scene.window.visible = show;
    scene.window.who.clear();
    scene.window.what.clear();
    scene.window.rich = {};
    if (auto tr = toTransition(trans))
      transition(tr);
  } else if (verb == "pause") {
    if (t.size() >= 2)
      pause(py.eval(trim(line.substr(5))).num(),
            py.lookup("_dismiss_pause").truthy());
    else
      pause(-1, true);
  } else if ((verb == "show" || verb == "hide" || verb == "call") &&
             t.size() >= 3 && t[1] == "screen") {
    std::string rest = trim(line.substr(line.find("screen") + 6));
    std::string name = rest, argSrc;
    if (auto p = rest.find('('); p != std::string::npos) {
      name = trim(rest.substr(0, p));
      argSrc = rest.substr(p);
    } else
      name = splitWords(rest)[0];
    Args args;
    Kwargs kw;
    if (!argSrc.empty()) {
      Value packed = py.eval("__screenargs" + argSrc);
      args = packed.items()[0].items();
      for (auto &[k, val] : packed.items()[1].o->dict)
        kw.emplace_back(k.text(), val);
    }
    if (verb == "hide") {
      auto &s = scene.screens;
      s.erase(
          std::remove_if(s.begin(), s.end(),
                         [&](const ScreenState &x) { return x.name == name; }),
          s.end());
    } else if (verb == "show") {
      auto &s = scene.screens;
      s.erase(
          std::remove_if(s.begin(), s.end(),
                         [&](const ScreenState &x) { return x.name == name; }),
          s.end());
      s.push_back({name, args, kw, now()});
    } else
      py.set("_return", callScreen(name, args, kw));
  } else
    backend.log("unhandled statement: " + line);
}

// ---- say / menu / screens ---------------------------------------------------

std::string Vm::formatDialogueText(const std::string &raw) {
  std::string text = interpolate(raw);
  Value filter = py.getAttr(config, "replace_text");
  if (filter.isObj() && filter.o->fn) {
    try {
      text = py.call(filter, {Value::str(text)}).text();
    } catch (const PyError &) {
    }
  }
  return text;
}

void Vm::say(const Value &who, const std::string &what, const Value &attrs) {
  std::string name, prefix, suffix, image;
  Value callback;
  if (who.cls() == "Character") {
    const Value displayArgs = who.get("display_args");
    if (displayArgs.isObj() && displayArgs.o->k == Object::K::Dict)
      if (const Value *found = displayArgs.o->findKey(Value::str("callback")))
        callback = *found;
    Value n = who.get("name");
    if (who.get("dynamic").truthy() && !n.isNone()) {
      try {
        n = py.eval(n.text());
      } catch (const PyError &) {
      }
    }
    if (!n.isNone())
      name = n.text();
    prefix =
        who.get("what_prefix").isNone() ? "" : who.get("what_prefix").text();
    suffix =
        who.get("what_suffix").isNone() ? "" : who.get("what_suffix").text();
    image = who.get("image").isNone() ? "" : who.get("image").text();
  } else if (!who.isNone())
    name = who.text();

  // Say attributes re-show the speaker's image keeping its transform.
  if (!image.empty() && attrs.isObj() && !attrs.items().empty()) {
    std::vector<std::string> img{image};
    for (auto &a : attrs.items())
      img.push_back(a.text());
    bool showing = false;
    for (auto &s : scene.master)
      if (s.tag == image && !s.hiding)
        showing = true;
    if (showing) {
      if (DispP d = imageNamed(img))
        showDisp(image, img, d, {}, "", 0, false);
      else
        backend.log("say attributes: unknown image " + image);
    }
  }

  // Character callbacks (Monika's slow_nodismiss in Act 3) may jump away.
  auto notify = [&](const char *event) {
    if (!callback.isObj())
      return;
    try {
      py.call(callback, {Value::str(event)},
              {{"interact", Value::boolean(true)}});
    } catch (const PyError &error) {
      backend.log(std::string("character callback: ") + error.what());
    }
  };
  notify("begin");

  std::string text = formatDialogueText(prefix + what + suffix);
  name = interpolate(name);

  if (windowAuto_ && !scene.window.visible) {
    scene.window.visible = true;
    scene.window.who.clear();
    scene.window.what.clear();
    scene.window.rich = {};
    if (auto tr = toTransition(py.getAttr(config, "window_show_transition")))
      transition(tr);
  }
  Window &w = scene.window;
  w.visible = true;
  w.who = name;
  w.what = text;
  const Value sd = styleNs.get("say_dialogue");
  w.monika = sd.isObj() && sd.get("name").text() == "default_monika";
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
    in.items = inertMenu_;
    notify("show");
    interact(std::move(in));
    from = to;
  };
  const std::vector<RichText::Wait> waits = w.rich.waits;
  const size_t total = w.rich.glyphs.size();
  for (auto &wt : waits)
    segment(wt.at, wt.kind, wt.time);
  const bool endsNoWait =
      !waits.empty() && waits.back().at >= total && waits.back().kind == 2;
  const bool endsClick =
      !waits.empty() && waits.back().at >= total && waits.back().kind == 0;
  if (!endsNoWait && !endsClick)
    segment(total, 0, 0);
  inertMenu_.clear();
  notify("end");
  try {
    py.call(py.lookup("_kt_add_history"),
            {Value::str("adv"), name.empty() ? Value() : Value::str(name),
             Value::str(text)});
  } catch (const PyError &error) {
    backend.log(std::string("history: ") + error.what());
  }
  // The window stays up (empty) until the next line.
  if (endsNoWait)
    return;
  scene.window.who.clear();
  scene.window.what.clear();
  scene.window.rich = {};
  committed = scene;
}

Value Vm::menu(const std::vector<Interaction::Item> &items,
               const std::string &caption, const std::string &screen) {
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
  if (windowAuto_)
    scene.window.visible = wasVisible;
  // A missing result is not choice zero; let the caller fall through.
  if (r.isNone()) {
    committed = scene;
    return Value();
  }
  const size_t i = size_t(std::max<int64_t>(0, r.integer()));
  committed = scene;
  return i < items.size() ? items[i].value : Value();
}

Value Vm::callScreen(const std::string &name, Args args, Kwargs kw) {
  Interaction in;
  in.k = Interaction::K::Screen;
  in.screen = name;
  in.dismiss = false;
  Value msg = args.empty() ? kwGet(kw, "message") : args[0];
  in.message = msg.isNone() ? "" : interpolate(msg.text());
  in.args = std::move(args);
  in.kw = std::move(kw);
  Value r = interact(std::move(in));
  if (r.cls() == "Return")
    return r.get("value");
  return r;
}

// ---- images -----------------------------------------------------------------

void Vm::show(const Value &imspec, const Value &atl, bool sceneStmt) {
  if (sceneStmt) {
    scene.master.clear();
    scene.masterXf.reset();
    if (windowAuto_ && scene.window.visible)
      scene.window.visible = false;
  }
  if (!imspec.isObj())
    return;
  const auto &is = imspec.items();
  if (is.size() < 4)
    return;
  std::vector<std::string> name;
  for (auto &p : is[0].items())
    name.push_back(p.text());
  const Value &expr = is[1];
  std::string tag = is[2].isNone() ? "" : is[2].text();
  DispP d;
  if (!expr.isNone()) {
    try {
      d = toDisp(py.eval(expr.text()));
    } catch (const PyError &e) {
      backend.log("show expression: " + std::string(e.what()));
    }
    if (tag.empty())
      tag = expr.text();
    if (name.empty())
      name = {tag};
  } else {
    d = imageNamed(name);
    if (!d && !name.empty()) {
      // Ren'Py keeps the attributes of a showing image ("show monika at t11").
      for (auto &s : scene.master)
        if (s.tag == name[0] && !s.hiding && name.size() == 1) {
          d = imageNamed(s.name);
          name = s.name;
        }
    }
    if (tag.empty() && !name.empty())
      tag = name[0];
  }
  if (!d) {
    std::string key;
    for (auto &p : name)
      key += p + " ";
    backend.log("unknown image: " + key);
    d = makeNullDisp(0, 0);
  }
  std::vector<Value> at;
  for (auto &a : is[3].items()) {
    try {
      at.push_back(py.eval(a.text()));
    } catch (const PyError &e) {
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
    for (auto &b : is[6].items())
      behind = b.text();
  int z = 0;
  if (!zorder.isNone())
    z = int(py.eval(zorder.text()).integer());
  showDisp(tag, name, d, at, behind, z, !zorder.isNone());
}

void Vm::showDisp(const std::string &tag, const std::vector<std::string> &name,
                  const DispP &disp, const std::vector<Value> &atList,
                  const std::string &behind, int zorder, bool hasZorder) {
  auto &m = scene.master;
  auto it = std::find_if(m.begin(), m.end(),
                         [&](const Shown &s) { return s.tag == tag; });
  if (it != m.end() && it->hiding) {
    m.erase(it);
    it = m.end();
  }
  const double t = now();
  std::vector<std::shared_ptr<AtlTemplate>> templs;
  for (auto &v : atList)
    if (v.cls() == "ATLTransform" && v.o->payload)
      templs.push_back(std::static_pointer_cast<AtlTemplate>(v.o->payload));

  if (templs.empty() && it != m.end() && it->inner) {
    it->inner->setChild(disp->instance(), std::max(0.0, t - it->shownAt));
    it->name = name;
    if (hasZorder)
      it->zorder = zorder;
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
    for (auto &tp : templs) {
      auto x = std::make_shared<TransformDisp>(tp);
      x->setChild(child, 0);
      if (!s.inner)
        s.inner = x;
      child = x;
    }
    s.xf = std::static_pointer_cast<TransformDisp>(child);
  }
  const bool replacing = it != m.end();
  if (replacing && it->xf)
    s.xf->takeState(*it->xf);
  forChain(s.xf,
           [&](TransformDisp &x) { x.event(replacing ? "replace" : "show"); });

  if (replacing) {
    *it = std::move(s);
    return;
  }
  auto pos = m.end();
  if (!behind.empty()) {
    pos = std::find_if(m.begin(), m.end(),
                       [&](const Shown &x) { return x.tag == behind; });
  } else {
    pos = std::find_if(m.begin(), m.end(),
                       [&](const Shown &x) { return x.zorder > s.zorder; });
  }
  m.insert(pos, std::move(s));
}

void Vm::hide(const Value &imspec) {
  if (!imspec.isObj() || imspec.items().size() < 3)
    return;
  const auto &is = imspec.items();
  std::string tag = is[2].isNone() ? "" : is[2].text();
  if (tag.empty()) {
    if (!is[0].items().empty())
      tag = is[0].items()[0].text();
    else if (!is[1].isNone())
      tag = is[1].text();
  }
  hideTag(tag);
}

void Vm::hideTag(const std::string &tag) {
  auto &m = scene.master;
  auto it = std::find_if(m.begin(), m.end(), [&](const Shown &s) {
    return s.tag == tag && !s.hiding;
  });
  if (it == m.end())
    return;
  bool handles = false;
  forChain(it->xf,
           [&](TransformDisp &x) { handles = handles || x.handles("hide"); });
  if (!handles) {
    m.erase(it);
    return;
  }
  it->hiding = true;
  forChain(it->xf, [&](TransformDisp &x) { x.event("hide"); });
}

// ---- transitions / pauses ---------------------------------------------------

void Vm::doWith(const std::string &expr) {
  if (expr.empty() || expr == "None") {
    committed = scene;
    return;
  }
  std::shared_ptr<Transition> t;
  try {
    t = toTransition(py.eval(expr));
  } catch (const PyError &e) {
    backend.log("with " + expr + ": " + e.what());
  }
  if (t)
    transition(t);
  else
    committed = scene;
}

void Vm::transition(const std::shared_ptr<Transition> &t) {
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

void Vm::movieCutscene(const std::string &path, const Value &action) {
  if (path.empty())
    return;
  Interaction in;
  in.k = Interaction::K::MovieCutscene;
  in.movie = path;
  in.action = action;
  in.dismiss = true; // a click skips the cutscene (Ren'Py's default)
  interact(std::move(in));
}

} // namespace Ddlc
