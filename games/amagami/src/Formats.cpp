#include <Amagami/Formats.hpp>
#include <Kotonoha/localization/Encoding.hpp>
#include <algorithm>
#include <array>
#include <cstring>
#include <stdexcept>

namespace Amagami {
namespace {
[[noreturn]] void Invalid(const std::string &why) {
  throw std::runtime_error("Amagami: " + why);
}
class Reader {
  const Bytes &b;

public:
  size_t p = 0;
  explicit Reader(const Bytes &bytes) : b(bytes) {}
  void require(size_t n) const {
    if (p > b.size() || n > b.size() - p)
      Invalid("truncated data at " + std::to_string(p));
  }
  uint8_t u8() {
    require(1);
    return b[p++];
  }
  uint16_t u16() {
    uint16_t v = u8();
    return v | uint16_t(u8()) << 8;
  }
  uint32_t u32() {
    uint32_t v = u16();
    return v | uint32_t(u16()) << 16;
  }
  Bytes take(size_t n) {
    require(n);
    Bytes v(b.begin() + p, b.begin() + p + n);
    p += n;
    return v;
  }
  std::string text() {
    auto s = take(u16());
    return Kotonoha::ShiftJisToUtf8(std::string(s.begin(), s.end()));
  }
  std::string terminated() {
    size_t start = p;
    while (u8()) {
    }
    return Kotonoha::ShiftJisToUtf8(
        std::string(b.begin() + start, b.begin() + p - 1));
  }
};
void range(uint64_t offset, uint64_t size, uint64_t total, uint64_t floor) {
  if (offset < floor || offset > total || size > total - offset)
    Invalid("archive entry outside payload");
}
Value readValue(Reader &r, unsigned depth = 0) {
  if (depth > 32)
    Invalid("SCF constant nesting exceeds limit");
  Value v;
  v.type = r.u8();
  switch (v.type) {
  case Value::Nil:
    break;
  case Value::False:
  case Value::True:
    break; // singleton literal, no payload
  case Value::Integer:
  case Value::Float:
    v.bits = r.u32();
    break;
  case Value::String:
  case Value::Class:
  case Value::Symbol:
    v.text = r.text();
    break;
  case Value::Array: {
    auto n = r.u16();
    r.require(n);
    for (unsigned i = 0; i < n; ++i)
      v.elements.push_back(readValue(r, depth + 1));
    break;
  }
  default:
    Invalid("unknown SCF constant type " + std::to_string(v.type));
  }
  return v;
}
void variables(Reader &r, std::vector<Variable> &out) {
  auto n = r.u16();
  for (unsigned i = 0; i < n; ++i) {
    Variable v;
    v.slot = r.u8();
    v.name = r.text();
    out.push_back(std::move(v));
  }
}
void methods(Reader &r, bool classMethod, std::vector<Method> &out) {
  auto n = r.u16();
  for (unsigned i = 0; i < n; ++i) {
    Method m;
    m.classMethod = classMethod;
    m.name = r.text();
    m.arguments = r.u16();
    m.code = r.take(r.u16());
    out.push_back(std::move(m));
  }
}
void texts(const Value &v, std::vector<std::string> &out) {
  if (v.type == Value::String && !v.text.empty())
    out.push_back(v.text);
  for (const auto &child : v.elements)
    texts(child, out);
}
} // namespace
std::vector<ArchiveEntry> ParseArc(const Bytes &header, uint64_t fileSize) {
  Reader r(header);
  const uint32_t count = r.u32(), start = r.u32();
  if (uint64_t(count) * 20 + 8 != start || start > fileSize)
    Invalid("invalid ARC index size");
  r.require(size_t(count) * 20);
  // Two u16 hash bucket tables, then records in hash order (not payload order).
  r.take(size_t(count) * 4);
  std::vector<ArchiveEntry> entries;
  entries.reserve(count);
  for (uint32_t i = 0; i < count; ++i) {
    ArchiveEntry e;
    e.flags = r.u32();
    e.hash = r.u32();
    e.offset = r.u32();
    e.size = r.u32();
    range(e.offset, e.size, fileSize, start);
    entries.push_back(e);
  }
  return entries;
}
std::vector<ArchiveEntry> ParseAfs(const Bytes &header, uint64_t fileSize) {
  Reader r(header);
  if (r.take(4) != Bytes({'A', 'F', 'S', 0}))
    Invalid("invalid AFS magic");
  uint32_t n = r.u32();
  r.require(uint64_t(n) * 8);
  std::vector<ArchiveEntry> entries;
  entries.reserve(n);
  for (uint32_t i = 0; i < n; ++i) {
    ArchiveEntry e;
    e.offset = r.u32();
    e.size = r.u32();
    range(e.offset, e.size, fileSize, uint64_t(n) * 8 + 8);
    entries.push_back(e);
  }
  return entries;
}
Bytes Unpack(const Bytes &packed, size_t limit) {
  Reader r(packed);
  const uint32_t size = r.u32();
  if (size > limit)
    Invalid("LZ output exceeds limit");
  Bytes out;
  out.reserve(size);
  std::array<uint8_t, 2048> window{};
  size_t write = 0x7de;
  // SLPS_259.18 @ 0x117a68: 11-bit ring offset, 5-bit length + 3.
  while (out.size() < size) {
    uint8_t flags = r.u8();
    for (unsigned bit = 0; bit < 8 && out.size() < size; ++bit) {
      if (flags & (1 << bit)) {
        auto c = r.u8();
        out.push_back(c);
        window[write] = c;
        write = (write + 1) & 2047;
      } else {
        unsigned a = r.u8(), b = r.u8();
        size_t pos = a | ((b & 0xe0) << 3), n = (b & 31) + 3;
        n = std::min(n, size - out.size());
        for (size_t j = 0; j < n; ++j) {
          auto c = window[(pos + j) & 2047];
          out.push_back(c);
          window[write] = c;
          write = (write + 1) & 2047;
        }
      }
    }
  }
  return out;
}
Script ParseScf(const Bytes &bytes) {
  Reader r(bytes);
  if (r.take(4) != Bytes({'S', 'C', 'F', 0x1a}))
    Invalid("invalid SCF magic");
  Script s;
  s.flags = r.u8();
  if (r.u8() != 4)
    Invalid("unsupported SCF version");
  s.name = r.text();
  s.parent = r.text();
  variables(r, s.variables);
  variables(r, s.classVariables);
  methods(r, false, s.methods);
  methods(r, true, s.methods);
  auto n = r.u16();
  for (unsigned i = 0; i < n; ++i)
    s.constants.push_back(readValue(r));
  if (r.p != bytes.size())
    Invalid("trailing SCF data in " + s.name);
  return s;
}
std::vector<std::string> Script::texts() const {
  std::vector<std::string> result;
  for (const auto &v : constants)
    Amagami::texts(v, result);
  return result;
}
std::vector<ClassFile> ParseClassImage(const Bytes &unpacked) {
  Reader r(unpacked);
  if (r.u32() != unpacked.size())
    Invalid("invalid class image size");
  const auto n = r.u32();
  r.require(uint64_t(n) * 8);
  std::vector<uint32_t> names, offsets;
  for (uint32_t i = 0; i < n; ++i)
    names.push_back(r.u32());
  for (uint32_t i = 0; i < n; ++i)
    offsets.push_back(r.u32());
  std::vector<ClassFile> out;
  for (uint32_t i = 0; i < n; ++i) {
    const uint64_t end = i + 1 < n ? offsets[i + 1] : unpacked.size();
    if (end < offsets[i])
      Invalid("unordered class image offsets");
    range(offsets[i], end - offsets[i], unpacked.size(), uint64_t(n) * 8 + 8);
    if (names[i] < uint64_t(n) * 8 + 8 || names[i] >= offsets.front())
      Invalid("class name outside name table");
    r.p = names[i];
    auto name = r.terminated();
    if (r.p > offsets.front())
      Invalid("unterminated class name");
    r.p = offsets[i];
    out.push_back({std::move(name), r.take(end - offsets[i])});
  }
  return out;
}
} // namespace Amagami
