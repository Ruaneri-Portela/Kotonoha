#include <ZeroNoTsukaima/Scene.hpp>

#include <SDL3/SDL_stdinc.h>

#include <cstring>

namespace ZeroNoTsukaima {

#include "Cp932Table.inc"

std::string ShiftJisToUtf8(const std::string &s) {
  std::string out;
  auto put = [&](uint32_t u) {
    if (u < 0x80) out.push_back(char(u));
    else if (u < 0x800) {
      out.push_back(char(0xC0 | u >> 6));
      out.push_back(char(0x80 | (u & 0x3F)));
    } else {
      out.push_back(char(0xE0 | u >> 12));
      out.push_back(char(0x80 | ((u >> 6) & 0x3F)));
      out.push_back(char(0x80 | (u & 0x3F)));
    }
  };
  for (size_t i = 0; i < s.size(); ++i) {
    const uint8_t c = uint8_t(s[i]);
    if (c < 0x80) put(c);
    else if (c >= 0xA1 && c <= 0xDF) put(0xFF61 + (c - 0xA1));
    else if (c >= 0x81 && c <= 0xFC && i + 1 < s.size()) {
      const uint8_t t = uint8_t(s[++i]);
      const unsigned short u = t >= 0x40 && t <= 0xFC ? kCp932Table[c - 0x81][t - 0x40] : 0;
      put(u ? u : 0x3F);
    } else put(0x3F);
  }
  return out;
}

namespace {

struct Reader {
  const std::vector<uint8_t> &b;
  size_t p = 0;
  bool ok = true;

  explicit Reader(const std::vector<uint8_t> &d) : b(d) {}

  uint32_t U32() {
    if (p + 4 > b.size()) { ok = false; return 0; }
    uint32_t v = b[p] | b[p + 1] << 8 | b[p + 2] << 16 | uint32_t(b[p + 3]) << 24;
    p += 4;
    return v;
  }
  int32_t I32() { return int32_t(U32()); }
  void Part() {
    if (p + 4 > b.size() || std::memcmp(&b[p], "TRAP", 4) != 0) { ok = false; return; }
    p += 4;
  }
  NutLiteral Obj() {
    NutLiteral lit;
    const uint32_t t = U32();
    if (t == 0x08000010) {
      const int32_t n = I32();
      if (n < 0 || p + size_t(n) > b.size()) { ok = false; return lit; }
      lit.kind = NutLiteral::String;
      lit.text = ShiftJisToUtf8(std::string(reinterpret_cast<const char *>(&b[p]), size_t(n)));
      p += size_t(n);
    } else if (t == 0x05000002) {
      lit.kind = NutLiteral::Integer;
      lit.value = I32();
    } else if (t == 0x05000004) {
      I32();
    } else if (t == 0x01000008) {
      lit.value = I32();
    } else if (t != 0x01000001) {
      ok = false;
    }
    return lit;
  }
};

void Proto(Reader &r, NutFunction *fn) {
  r.Part();
  r.Obj();
  r.Obj();
  r.Part();
  const int32_t nl = r.I32(), np = r.I32(), no = r.I32(), nlv = r.I32(), nli = r.I32(), ni = r.I32(), nf = r.I32();
  if (!r.ok || nl < 0 || np < 0 || no < 0 || nlv < 0 || nli < 0 || ni < 0 || nf < 0) { r.ok = false; return; }
  r.Part();
  for (int i = 0; i < nl && r.ok; ++i) {
    NutLiteral l = r.Obj();
    if (fn) fn->literals.push_back(std::move(l));
  }
  r.Part();
  for (int i = 0; i < np && r.ok; ++i) r.Obj();
  r.Part();
  for (int i = 0; i < no && r.ok; ++i) { r.U32(); r.Obj(); r.Obj(); }
  r.Part();
  for (int i = 0; i < nlv && r.ok; ++i) { r.Obj(); r.U32(); r.U32(); r.U32(); }
  r.Part();
  r.p += size_t(nli) * 8;
  r.Part();
  if (!r.ok || r.p + size_t(ni) * 8 > r.b.size()) { r.ok = false; return; }
  for (int i = 0; i < ni; ++i) {
    const uint8_t *q = &r.b[r.p + size_t(i) * 8];
    NutInstruction in;
    in.arg1 = int32_t(q[0] | q[1] << 8 | q[2] << 16 | uint32_t(q[3]) << 24);
    in.op = q[4]; in.arg0 = q[5]; in.arg2 = q[6]; in.arg3 = q[7];
    if (fn) fn->code.push_back(in);
  }
  r.p += size_t(ni) * 8;
  r.Part();
  for (int i = 0; i < nf && r.ok; ++i) Proto(r, nullptr);
}

} // namespace

bool ParseNut(const std::vector<uint8_t> &data, NutFunction *out) {
  if (data.size() < 12 || data[0] != 0xFA || data[1] != 0xFA || std::memcmp(&data[2], "RIQS", 4) != 0) return false;
  Reader r(data);
  r.p = 6;
  r.I32();
  Proto(r, out);
  return r.ok;
}

bool ParseSceneTags(const std::vector<uint8_t> &unpacked, std::vector<SceneTag> *out) {
  NutFunction fn;
  if (!ParseNut(unpacked, &fn)) return false;
  auto text = [&](int32_t idx) -> std::string {
    if (idx < 0 || size_t(idx) >= fn.literals.size()) return {};
    const NutLiteral &l = fn.literals[size_t(idx)];
    return l.kind == NutLiteral::Integer ? std::to_string(l.value) : l.text;
  };
  std::map<int, std::string> regs;
  SceneTag cur;
  for (const NutInstruction &in : fn.code) {
    switch (in.op) {
    case 30: cur = SceneTag(); break;
    case 1: regs[in.arg0] = text(in.arg1); break;
    case 2: regs[in.arg0] = std::to_string(in.arg1); break;
    case 4:
      regs[in.arg0] = text(in.arg1);
      regs[in.arg2] = text(in.arg3);
      break;
    case 60: cur.attrs[regs[3]] = regs[4]; break;
    case 32:
      if (auto it = cur.attrs.find("tagname"); it != cur.attrs.end()) cur.name = it->second;
      out->push_back(cur);
      break;
    default: break;
    }
  }
  return true;
}

std::shared_ptr<NutValue> EvalNutTable(const std::vector<uint8_t> &unpacked) {
  NutFunction fn;
  if (!ParseNut(unpacked, &fn)) return nullptr;
  using Ptr = std::shared_ptr<NutValue>;
  auto literal = [&](int32_t i) {
    auto v = std::make_shared<NutValue>();
    if (i < 0 || size_t(i) >= fn.literals.size()) return v;
    const NutLiteral &l = fn.literals[size_t(i)];
    if (l.kind == NutLiteral::Integer) {
      v->kind = NutValue::Integer;
      v->value = l.value;
    } else {
      v->kind = NutValue::String;
      v->text = l.text;
    }
    return v;
  };
  auto make = [](NutValue::Kind k) {
    auto v = std::make_shared<NutValue>();
    v->kind = k;
    return v;
  };
  std::map<int, Ptr> regs;
  auto reg = [&](int i) -> Ptr {
    auto it = regs.find(i);
    return it == regs.end() ? nullptr : it->second;
  };
  // Opcodes of this Squirrel build: 1 LOAD, 2 LOADINT, 4 DLOAD, 20 LOADNULLS, 30 NEWTABLE,
  // 31 NEWARRAY, 32 APPEND, 45 NEG, 60 NEWSLOT (table a1, key a2, value a3).
  for (const NutInstruction &in : fn.code) {
    switch (in.op) {
    case 1: regs[in.arg0] = literal(in.arg1); break;
    case 2: {
      auto v = make(NutValue::Integer);
      v->value = in.arg1;
      regs[in.arg0] = v;
      break;
    }
    case 4:
      regs[in.arg0] = literal(in.arg1);
      regs[in.arg2] = literal(in.arg3);
      break;
    case 20:
      for (int k = 0; k < in.arg1; ++k) regs[in.arg0 + k] = nullptr;
      break;
    case 30: regs[in.arg0] = make(NutValue::Table); break;
    case 31: regs[in.arg0] = make(NutValue::Array); break;
    case 32:
      if (Ptr a = reg(in.arg0); a && a->kind == NutValue::Array) a->items.push_back(reg(in.arg1));
      break;
    case 45:
      if (Ptr v = reg(in.arg1)) {
        auto n = std::make_shared<NutValue>(*v);
        n->value = -n->value;
        regs[in.arg0] = n;
      }
      break;
    case 60:
      if (Ptr t = reg(in.arg1); t && t->kind == NutValue::Table) {
        Ptr k = reg(in.arg2);
        t->fields.emplace_back(k ? k->Text() : std::string(), reg(in.arg3));
      }
      break;
    default: break;
    }
  }
  Ptr root = reg(1);
  return root && root->kind == NutValue::Table ? root : nullptr;
}

bool ParseSceneNames(const std::vector<uint8_t> &unpacked, std::vector<std::string> *out) {
  NutFunction fn;
  if (!ParseNut(unpacked, &fn)) return false;
  for (const NutLiteral &l : fn.literals)
    if (l.kind == NutLiteral::String && l.text != "next") out->push_back(l.text);
  return !out->empty();
}

} // namespace ZeroNoTsukaima
