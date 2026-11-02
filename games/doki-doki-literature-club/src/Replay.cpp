#include "Ddlc/Replay.hpp"
#include <cmath>
#include <cstring>
#include <limits>
#include <stdexcept>
namespace Ddlc {
namespace {
constexpr size_t limit = 128 * 1024 * 1024, countLimit = 1000000;
struct Writer {
  std::string data = "DDLCREPLAY1";
  void integer(uint64_t value) {
    for (int n = 0; n < 8; ++n)
      data += char(value >> (8 * n));
  }
  void count(size_t value) {
    if (value > countLimit)
      throw std::runtime_error("DDLC replay has too many entries");
    integer(value);
  }
  void text(const std::string &value) {
    if (value.size() > limit)
      throw std::runtime_error("DDLC replay string too large");
    integer(value.size());
    data += value;
    if (data.size() > limit)
      throw std::runtime_error("DDLC replay too large");
  }
  void number(double value) {
    if (!std::isfinite(value))
      throw std::runtime_error("DDLC replay has invalid number");
    uint64_t bits;
    std::memcpy(&bits, &value, 8);
    integer(bits);
  }
  void boolean(bool value) { integer(value ? 1 : 0); }
};
struct Reader {
  const std::string &data;
  size_t at = 11;
  explicit Reader(const std::string &value) : data(value) {
    if (data.size() < 11 || data.size() > limit ||
        data.compare(0, 11, "DDLCREPLAY1") != 0)
      throw std::runtime_error("Incompatible DDLC replay");
  }
  uint64_t integer() {
    if (data.size() - at < 8)
      throw std::runtime_error("Truncated DDLC replay");
    uint64_t value = 0;
    for (int n = 0; n < 8; ++n)
      value |= uint64_t(static_cast<unsigned char>(data[at++])) << (8 * n);
    return value;
  }
  size_t count() {
    auto n = integer();
    if (n > countLimit)
      throw std::runtime_error("Invalid DDLC replay count");
    return size_t(n);
  }
  std::string text() {
    auto n = integer();
    if (n > data.size() - at)
      throw std::runtime_error("Truncated DDLC replay string");
    auto value = data.substr(at, size_t(n));
    at += size_t(n);
    return value;
  }
  double number() {
    uint64_t bits = integer();
    double value;
    std::memcpy(&value, &bits, 8);
    if (!std::isfinite(value))
      throw std::runtime_error("Invalid DDLC replay number");
    return value;
  }
  bool boolean() {
    auto value = integer();
    if (value > 1)
      throw std::runtime_error("Invalid DDLC replay boolean");
    return value != 0;
  }
};
void input(Writer &w, const ReplayInput &i) {
  w.number(i.time);
  w.number(i.windowTime);
  w.number(i.mouseX);
  w.number(i.mouseY);
  w.boolean(i.skipping);
  w.count(i.positions.size());
  for (auto &[k, v] : i.positions) {
    w.text(k);
    w.number(v);
  }
  w.count(i.playing.size());
  for (auto &[k, v] : i.playing) {
    w.text(k);
    w.text(v);
  }
}
ReplayInput input(Reader &r) {
  ReplayInput i;
  i.time = r.number();
  i.windowTime = r.number();
  i.mouseX = r.number();
  i.mouseY = r.number();
  i.skipping = r.boolean();
  for (size_t n = r.count(); n--;) {
    auto k = r.text();
    auto v = r.number();
    if (!i.positions.emplace(k, v).second)
      throw std::runtime_error("Duplicate DDLC audio position");
  }
  for (size_t n = r.count(); n--;) {
    auto k = r.text();
    auto v = r.text();
    if (!i.playing.emplace(k, v).second)
      throw std::runtime_error("Duplicate DDLC audio channel");
  }
  return i;
}
void rootName(const std::string &name) {
  if (name.empty() || name.find('\0') != name.npos ||
      name.find('\\') != name.npos)
    throw std::runtime_error("Invalid DDLC root filename");
  std::filesystem::path p(name);
  if (p.is_absolute())
    throw std::runtime_error("Invalid DDLC root filename");
  for (const auto &part : p)
    if (part == "..")
      throw std::runtime_error("Invalid DDLC root filename");
}
bool same(const Value &a, const Value &b) {
  if (a.t != b.t)
    return false;
  switch (a.t) {
  case Value::T::None:
    return true;
  case Value::T::Bool:
    return a.b == b.b;
  case Value::T::Int:
    return a.i == b.i;
  case Value::T::Float:
    return a.f == b.f;
  case Value::T::Str:
    return a.s == b.s;
  case Value::T::Obj:
    return a.o == b.o;
  }
  return false;
}
} // namespace
std::string replayPosition(const Vm &vm) {
  auto *n = vm.currentNode();
  return n ? n->file + ":" + std::to_string(n->line) : std::string();
}
ReplayReply encodeReply(const Value &v, const Interaction &in) {
  ReplayReply reply;
  if (v.isObj()) {
    for (size_t n = 0; n < in.items.size(); ++n)
      if (same(v, in.items[n].value)) {
        reply.kind = ReplayReply::Kind::Item;
        reply.integer = n;
        return reply;
      }
    for (size_t n = 0; n < in.args.size(); ++n)
      if (same(v, in.args[n])) {
        reply.kind = ReplayReply::Kind::Argument;
        reply.integer = n;
        return reply;
      }
    for (auto &[key, value] : in.kw)
      if (same(v, value)) {
        reply.kind = ReplayReply::Kind::Keyword;
        reply.text = key;
        return reply;
      }
    if (v.cls() == "Return") {
      auto value = v.get("value");
      auto simple = encodeReply(value, {});
      if (simple.kind == ReplayReply::Kind::None ||
          simple.kind == ReplayReply::Kind::Bool ||
          simple.kind == ReplayReply::Kind::Int ||
          simple.kind == ReplayReply::Kind::Float ||
          simple.kind == ReplayReply::Kind::String) {
        reply = simple;
        reply.text = std::to_string(int(simple.kind)) + ":" + simple.text;
        reply.kind = ReplayReply::Kind::Return;
        return reply;
      }
    }
    throw std::runtime_error("This DDLC interaction cannot be saved");
  }
  switch (v.t) {
  case Value::T::None:
    break;
  case Value::T::Bool:
    reply.kind = ReplayReply::Kind::Bool;
    reply.integer = v.b;
    break;
  case Value::T::Int:
    reply.kind = ReplayReply::Kind::Int;
    reply.integer = v.i;
    break;
  case Value::T::Float:
    reply.kind = ReplayReply::Kind::Float;
    reply.number = v.f;
    break;
  case Value::T::Str:
    reply.kind = ReplayReply::Kind::String;
    reply.text = v.s;
    break;
  default:
    break;
  }
  return reply;
}
Value decodeReply(const ReplayReply &r, const Interaction &in) {
  switch (r.kind) {
  case ReplayReply::Kind::None:
    return {};
  case ReplayReply::Kind::Bool:
    return Value::boolean(r.integer != 0);
  case ReplayReply::Kind::Int:
    return Value::integer(r.integer);
  case ReplayReply::Kind::Float:
    return Value::real(r.number);
  case ReplayReply::Kind::String:
    return Value::str(r.text);
  case ReplayReply::Kind::Item:
    if (r.integer >= 0 && uint64_t(r.integer) < in.items.size())
      return in.items[size_t(r.integer)].value;
    break;
  case ReplayReply::Kind::Argument:
    if (r.integer >= 0 && uint64_t(r.integer) < in.args.size())
      return in.args[size_t(r.integer)];
    break;
  case ReplayReply::Kind::Keyword:
    for (auto &[key, value] : in.kw)
      if (key == r.text)
        return value;
    break;
  case ReplayReply::Kind::Return: {
    auto colon = r.text.find(':');
    if (colon != 1 || r.text[0] < '0' || r.text[0] > '4')
      break;
    auto nested = r;
    nested.kind = ReplayReply::Kind(r.text[0] - '0');
    nested.text = r.text.substr(2);
    auto result = makeInstance("Return");
    result.o->attrs["value"] = decodeReply(nested, {});
    return result;
  }
  }
  throw std::runtime_error("DDLC save no longer matches its interaction");
}
std::string encodeReplay(const ReplayData &d) {
  Writer w;
  w.text(d.fingerprint);
  w.text(d.label);
  w.text(d.persistent);
  w.count(d.initialFiles.size());
  for (auto &[name, data] : d.initialFiles) {
    rootName(name);
    w.text(name);
    w.text(data);
  }
  input(w, d.initial);
  input(w, d.current);
  w.count(d.steps.size());
  for (auto &s : d.steps) {
    input(w, s.input);
    w.count(s.files.size());
    for (auto &[name, data] : s.files) {
      rootName(name);
      w.text(name);
      w.boolean(bool(data));
      if (data)
        w.text(*data);
    }
    w.integer(uint64_t(s.reply.kind));
    w.integer(uint64_t(s.reply.integer));
    w.number(s.reply.number);
    w.text(s.reply.text);
    w.text(s.position);
    w.integer(uint64_t(s.interaction));
  }
  w.count(d.translations.size());
  for (auto &t : d.translations) {
    w.integer(uint64_t(t.route));
    w.text(t.original);
    w.text(t.translated);
  }
  w.count(d.audio.size());
  for (auto &a : d.audio) {
    w.text(a.channel);
    w.text(a.spec);
    w.text(a.queued);
    w.boolean(a.loop);
    w.boolean(a.queuedLoop);
    w.number(a.position);
    w.number(a.gain);
  }
  w.text(d.position);
  w.integer(uint64_t(d.interaction));
  return w.data;
}
ReplayData decodeReplay(const std::string &data) {
  Reader r(data);
  ReplayData d;
  d.fingerprint = r.text();
  d.label = r.text();
  d.persistent = r.text();
  for (size_t n = r.count(); n--;) {
    auto name = r.text();
    rootName(name);
    auto value = r.text();
    if (!d.initialFiles.emplace(name, value).second)
      throw std::runtime_error("Duplicate DDLC root file");
  }
  d.initial = input(r);
  d.current = input(r);
  for (size_t n = r.count(); n--;) {
    ReplayStep s;
    s.input = input(r);
    for (size_t m = r.count(); m--;) {
      auto name = r.text();
      rootName(name);
      auto value =
          r.boolean() ? std::optional<std::string>(r.text()) : std::nullopt;
      if (!s.files.emplace(name, std::move(value)).second)
        throw std::runtime_error("Duplicate DDLC root change");
    }
    auto kind = r.integer();
    if (kind > uint64_t(ReplayReply::Kind::Return))
      throw std::runtime_error("Invalid DDLC replay reply");
    s.reply.kind = ReplayReply::Kind(kind);
    s.reply.integer = int64_t(r.integer());
    s.reply.number = r.number();
    s.reply.text = r.text();
    s.position = r.text();
    s.interaction = int(r.integer());
    d.steps.push_back(std::move(s));
  }
  for (size_t n = r.count(); n--;) {
    ReplayTranslation t;
    t.route = int(r.integer());
    t.original = r.text();
    t.translated = r.text();
    d.translations.push_back(std::move(t));
  }
  for (size_t n = r.count(); n--;) {
    ReplayAudio a;
    a.channel = r.text();
    a.spec = r.text();
    a.queued = r.text();
    a.loop = r.boolean();
    a.queuedLoop = r.boolean();
    a.position = r.number();
    a.gain = r.number();
    if (a.position < 0 || a.gain < 0 || a.gain > 1)
      throw std::runtime_error("Invalid DDLC audio resume");
    d.audio.push_back(std::move(a));
  }
  d.position = r.text();
  d.interaction = int(r.integer());
  if (r.at != data.size())
    throw std::runtime_error("Trailing DDLC save bytes");
  return d;
}
} // namespace Ddlc
