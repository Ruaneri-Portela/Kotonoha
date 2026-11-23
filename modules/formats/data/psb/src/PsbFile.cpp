#include <Kirikiri/PsbFile.hpp>

#include <zlib.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstring>
#include <limits>
#include <map>
#include <set>
#include <sstream>
#include <stdexcept>
#include <utility>

namespace Kotonoha::Providers {

namespace {

const std::uint8_t kMdfMagic[4] = {'m', 'd', 'f', 0};
const std::uint8_t kPsbMagic[4] = {'P', 'S', 'B', 0};

std::uint16_t readU16LE(const std::uint8_t *p) noexcept {
  return static_cast<std::uint16_t>(p[0]) |
         (static_cast<std::uint16_t>(p[1]) << 8);
}

std::uint32_t readU32LE(const std::uint8_t *p) noexcept {
  return static_cast<std::uint32_t>(p[0]) |
         (static_cast<std::uint32_t>(p[1]) << 8) |
         (static_cast<std::uint32_t>(p[2]) << 16) |
         (static_cast<std::uint32_t>(p[3]) << 24);
}

std::uint64_t readUnsignedLE(const std::uint8_t *p, std::size_t n) noexcept {
  std::uint64_t v = 0;
  for (std::size_t i = 0; i < n; ++i)
    v |= static_cast<std::uint64_t>(p[i]) << (8 * i);
  return v;
}

std::int64_t readSignedLE(const std::uint8_t *p, std::size_t n) noexcept {
  if (n == 0)
    return 0;
  std::uint64_t u = readUnsignedLE(p, n);
  if (n < 8 && (p[n - 1] & 0x80u) != 0)
    u |= (~std::uint64_t(0)) << (8 * n);
  std::int64_t s = 0;
  std::memcpy(&s, &u, sizeof(s));
  return s;
}

void appendU16LE(std::vector<std::uint8_t> &out, std::uint16_t v) {
  out.push_back(static_cast<std::uint8_t>(v & 0xffu));
  out.push_back(static_cast<std::uint8_t>((v >> 8) & 0xffu));
}

void appendU32LE(std::vector<std::uint8_t> &out, std::uint32_t v) {
  for (int i = 0; i < 4; ++i)
    out.push_back(static_cast<std::uint8_t>((v >> (8 * i)) & 0xffu));
}

void appendUnsignedLE(std::vector<std::uint8_t> &out, std::uint64_t v,
                      std::size_t bytes) {
  for (std::size_t i = 0; i < bytes; ++i)
    out.push_back(static_cast<std::uint8_t>((v >> (8 * i)) & 0xffu));
}

std::size_t minUnsignedBytes(std::uint64_t v) noexcept {
  if (v <= 0xffu)
    return 1;
  if (v <= 0xffffu)
    return 2;
  if (v <= 0xffffffu)
    return 3;
  if (v <= 0xffffffffu)
    return 4;
  return 8;
}

void appendUintArray(std::vector<std::uint8_t> &out,
                     const std::vector<std::uint64_t> &values) {
  const std::size_t countBytes = minUnsignedBytes(values.size());
  std::uint64_t maxVal = 0;
  for (std::uint64_t v : values)
    if (v > maxVal)
      maxVal = v;
  const std::size_t elemBytes = minUnsignedBytes(maxVal);
  out.push_back(static_cast<std::uint8_t>(0x0cu + countBytes));
  appendUnsignedLE(out, values.size(), countBytes);
  out.push_back(static_cast<std::uint8_t>(0x0cu + elemBytes));
  for (std::uint64_t v : values)
    appendUnsignedLE(out, v, elemBytes);
}

std::string jsonEscape(const std::string &s) {
  std::string out;
  out.reserve(s.size() + 2);
  out.push_back('"');
  for (unsigned char c : s) {
    switch (c) {
    case '"':
      out += "\\\"";
      break;
    case '\\':
      out += "\\\\";
      break;
    case '\b':
      out += "\\b";
      break;
    case '\f':
      out += "\\f";
      break;
    case '\n':
      out += "\\n";
      break;
    case '\r':
      out += "\\r";
      break;
    case '\t':
      out += "\\t";
      break;
    default:
      if (c < 0x20) {
        char buf[8];
        std::snprintf(buf, sizeof(buf), "\\u%04x", unsigned(c));
        out += buf;
      } else {
        out.push_back(static_cast<char>(c));
      }
      break;
    }
  }
  out.push_back('"');
  return out;
}

struct JsonParser {
  const std::string &src;
  std::size_t pos = 0;

  void skipWs() {
    while (pos < src.size() && (src[pos] == ' ' || src[pos] == '\t' ||
                                src[pos] == '\r' || src[pos] == '\n'))
      ++pos;
  }

  std::string parseString() {
    skipWs();
    if (pos >= src.size() || src[pos] != '"')
      throw std::runtime_error("PsbValue::fromJson expected string");
    ++pos;
    std::string out;
    while (pos < src.size() && src[pos] != '"') {
      char c = src[pos++];
      if (c == '\\') {
        if (pos >= src.size())
          throw std::runtime_error("PsbValue::fromJson truncated escape");
        char e = src[pos++];
        switch (e) {
        case '"':
        case '\\':
        case '/':
          out.push_back(e);
          break;
        case 'b':
          out.push_back('\b');
          break;
        case 'f':
          out.push_back('\f');
          break;
        case 'n':
          out.push_back('\n');
          break;
        case 'r':
          out.push_back('\r');
          break;
        case 't':
          out.push_back('\t');
          break;
        case 'u': {
          if (pos + 4 > src.size())
            throw std::runtime_error("PsbValue::fromJson truncated \\u escape");
          unsigned code = 0;
          for (int i = 0; i < 4; ++i) {
            char h = src[pos++];
            code <<= 4;
            if (h >= '0' && h <= '9')
              code |= unsigned(h - '0');
            else if (h >= 'a' && h <= 'f')
              code |= unsigned(h - 'a' + 10);
            else if (h >= 'A' && h <= 'F')
              code |= unsigned(h - 'A' + 10);
            else
              throw std::runtime_error("PsbValue::fromJson bad hex escape");
          }
          if (code <= 0x7fu) {
            out.push_back(static_cast<char>(code));
          } else if (code <= 0x7ffu) {
            out.push_back(static_cast<char>(0xc0u | (code >> 6)));
            out.push_back(static_cast<char>(0x80u | (code & 0x3fu)));
          } else {
            out.push_back(static_cast<char>(0xe0u | (code >> 12)));
            out.push_back(static_cast<char>(0x80u | ((code >> 6) & 0x3fu)));
            out.push_back(static_cast<char>(0x80u | (code & 0x3fu)));
          }
          break;
        }
        default:
          out.push_back(e);
          break;
        }
      } else {
        out.push_back(c);
      }
    }
    if (pos >= src.size() || src[pos] != '"')
      throw std::runtime_error("PsbValue::fromJson unterminated string");
    ++pos;
    return out;
  }

  PsbValue parseValue() {
    skipWs();
    if (pos >= src.size())
      throw std::runtime_error("PsbValue::fromJson unexpected end");
    char c = src[pos];
    if (c == '"')
      return PsbValue::makeString(parseString());
    if (c == '[') {
      ++pos;
      PsbValue::List list;
      skipWs();
      if (pos < src.size() && src[pos] == ']') {
        ++pos;
        return PsbValue::makeList(std::move(list));
      }
      while (true) {
        list.push_back(parseValue());
        skipWs();
        if (pos < src.size() && src[pos] == ',') {
          ++pos;
          continue;
        }
        if (pos < src.size() && src[pos] == ']') {
          ++pos;
          break;
        }
        throw std::runtime_error("PsbValue::fromJson bad array syntax");
      }
      return PsbValue::makeList(std::move(list));
    }
    if (c == '{') {
      ++pos;
      PsbValue::Dict dict;
      skipWs();
      if (pos < src.size() && src[pos] == '}') {
        ++pos;
        return PsbValue::makeDict(std::move(dict));
      }
      while (true) {
        std::string key = parseString();
        skipWs();
        if (pos >= src.size() || src[pos] != ':')
          throw std::runtime_error("PsbValue::fromJson expected ':'");
        ++pos;
        dict[std::move(key)] = parseValue();
        skipWs();
        if (pos < src.size() && src[pos] == ',') {
          ++pos;
          continue;
        }
        if (pos < src.size() && src[pos] == '}') {
          ++pos;
          break;
        }
        throw std::runtime_error("PsbValue::fromJson bad object syntax");
      }
      return PsbValue::makeDict(std::move(dict));
    }
    if (src.compare(pos, 4, "null") == 0) {
      pos += 4;
      return PsbValue::makeNull();
    }
    if (src.compare(pos, 4, "true") == 0) {
      pos += 4;
      return PsbValue::makeBool(true);
    }
    if (src.compare(pos, 5, "false") == 0) {
      pos += 5;
      return PsbValue::makeBool(false);
    }
    std::size_t start = pos;
    bool isFloat = false;
    if (src[pos] == '-' || src[pos] == '+')
      ++pos;
    while (pos < src.size() &&
           (std::isdigit(static_cast<unsigned char>(src[pos])) ||
            src[pos] == '.' || src[pos] == 'e' || src[pos] == 'E' ||
            src[pos] == '-' || src[pos] == '+')) {
      if (src[pos] == '.' || src[pos] == 'e' || src[pos] == 'E')
        isFloat = true;
      ++pos;
    }
    if (pos == start)
      throw std::runtime_error("PsbValue::fromJson invalid token");
    const std::string token = src.substr(start, pos - start);
    if (isFloat)
      return PsbValue::makeDouble(std::stod(token));
    return PsbValue::makeInt(std::stoll(token));
  }
};

} // namespace

PsbValue PsbValue::makeNull() { return PsbValue{}; }

PsbValue PsbValue::makeBool(bool value) {
  PsbValue v;
  v.type_ = Type::Bool;
  v.boolVal_ = value;
  return v;
}

PsbValue PsbValue::makeInt(std::int64_t value) {
  PsbValue v;
  v.type_ = Type::Int;
  v.intVal_ = value;
  return v;
}

PsbValue PsbValue::makeUIntArray(std::vector<std::uint64_t> values) {
  PsbValue v;
  v.type_ = Type::UIntArray;
  v.uintArrayVal_ = std::move(values);
  return v;
}

PsbValue PsbValue::makeFloat(float value) {
  PsbValue v;
  v.type_ = Type::Float;
  v.doubleVal_ = static_cast<double>(value);
  return v;
}

PsbValue PsbValue::makeDouble(double value) {
  PsbValue v;
  v.type_ = Type::Double;
  v.doubleVal_ = value;
  return v;
}

PsbValue PsbValue::makeString(std::string value) {
  PsbValue v;
  v.type_ = Type::String;
  v.stringVal_ = std::move(value);
  return v;
}

PsbValue PsbValue::makeChunk(PsbChunkRef chunk) {
  PsbValue v;
  v.type_ = Type::Chunk;
  v.chunkVal_ = chunk;
  return v;
}

PsbValue PsbValue::makeList(List values) {
  PsbValue v;
  v.type_ = Type::List;
  v.listVal_ = std::move(values);
  return v;
}

PsbValue PsbValue::makeDict(Dict entries) {
  PsbValue v;
  v.type_ = Type::Dict;
  v.dictVal_ = std::move(entries);
  return v;
}

const PsbValue &PsbValue::nullValue() {
  static const PsbValue kNull{};
  return kNull;
}

bool PsbValue::asBool(bool fallback) const noexcept {
  if (type_ == Type::Bool)
    return boolVal_;
  if (type_ == Type::Int)
    return intVal_ != 0;
  return fallback;
}

std::int64_t PsbValue::asInt(std::int64_t fallback) const noexcept {
  if (type_ == Type::Int)
    return intVal_;
  if (type_ == Type::Float || type_ == Type::Double)
    return static_cast<std::int64_t>(doubleVal_);
  if (type_ == Type::Bool)
    return boolVal_ ? 1 : 0;
  return fallback;
}

double PsbValue::asDouble(double fallback) const noexcept {
  if (type_ == Type::Float || type_ == Type::Double)
    return doubleVal_;
  if (type_ == Type::Int)
    return static_cast<double>(intVal_);
  return fallback;
}

float PsbValue::asFloat(float fallback) const noexcept {
  return static_cast<float>(asDouble(fallback));
}

std::int64_t PsbValue::toInt(std::int64_t fallback) const noexcept {
  if (type_ == Type::String) {
    char *end = nullptr;
    const long long v = std::strtoll(stringVal_.c_str(), &end, 10);
    if (end && end != stringVal_.c_str())
      return static_cast<std::int64_t>(v);
  }
  return asInt(fallback);
}

double PsbValue::toDouble(double fallback) const noexcept {
  if (type_ == Type::String) {
    char *end = nullptr;
    const double v = std::strtod(stringVal_.c_str(), &end);
    if (end && end != stringVal_.c_str())
      return v;
  }
  return asDouble(fallback);
}

const std::string &PsbValue::asString() const noexcept {
  static const std::string kEmpty;
  return type_ == Type::String ? stringVal_ : kEmpty;
}

std::string PsbValue::toString(const std::string &fallback) const {
  if (type_ == Type::String)
    return stringVal_;
  if (type_ == Type::Int)
    return std::to_string(intVal_);
  if (type_ == Type::Float || type_ == Type::Double) {
    std::ostringstream os;
    os << doubleVal_;
    return os.str();
  }
  if (type_ == Type::Bool)
    return boolVal_ ? "true" : "false";
  return fallback;
}

const std::vector<std::uint64_t> &PsbValue::asUIntArray() const noexcept {
  static const std::vector<std::uint64_t> kEmpty;
  return type_ == Type::UIntArray ? uintArrayVal_ : kEmpty;
}

const PsbValue::List &PsbValue::asList() const noexcept {
  static const List kEmpty;
  return type_ == Type::List ? listVal_ : kEmpty;
}

const PsbValue::Dict &PsbValue::asDict() const noexcept {
  static const Dict kEmpty;
  return type_ == Type::Dict ? dictVal_ : kEmpty;
}

std::size_t PsbValue::size() const noexcept {
  if (type_ == Type::List)
    return listVal_.size();
  if (type_ == Type::Dict)
    return dictVal_.size();
  if (type_ == Type::UIntArray)
    return uintArrayVal_.size();
  if (type_ == Type::String)
    return stringVal_.size();
  return 0;
}

bool PsbValue::has(const std::string &key) const noexcept {
  return type_ == Type::Dict && dictVal_.find(key) != dictVal_.end();
}

const PsbValue &PsbValue::operator[](std::size_t index) const noexcept {
  if (type_ == Type::List && index < listVal_.size())
    return listVal_[index];
  return nullValue();
}

const PsbValue &PsbValue::operator[](const std::string &key) const noexcept {
  if (type_ == Type::Dict) {
    const auto it = dictVal_.find(key);
    if (it != dictVal_.end())
      return it->second;
  }
  return nullValue();
}

std::string PsbValue::toJson() const {
  switch (type_) {
  case Type::Null:
    return "null";
  case Type::Bool:
    return boolVal_ ? "true" : "false";
  case Type::Int:
    return std::to_string(intVal_);
  case Type::Float:
  case Type::Double: {
    if (!std::isfinite(doubleVal_))
      return "0.0";
    std::ostringstream os;
    os.precision(15);
    os << doubleVal_;
    return os.str();
  }
  case Type::String:
    return jsonEscape(stringVal_);
  case Type::UIntArray: {
    std::string out = "[";
    for (std::size_t i = 0; i < uintArrayVal_.size(); ++i) {
      if (i)
        out += ",";
      out += std::to_string(uintArrayVal_[i]);
    }
    out += "]";
    return out;
  }
  case Type::Chunk:
    return "{\"chunk\":" + std::to_string(chunkVal_.index) + "}";
  case Type::List: {
    std::string out = "[";
    for (std::size_t i = 0; i < listVal_.size(); ++i) {
      if (i)
        out += ",";
      out += listVal_[i].toJson();
    }
    out += "]";
    return out;
  }
  case Type::Dict: {
    std::string out = "{";
    bool first = true;
    for (const auto &[k, v] : dictVal_) {
      if (!first)
        out += ",";
      first = false;
      out += jsonEscape(k) + ":" + v.toJson();
    }
    out += "}";
    return out;
  }
  }
  return "null";
}

PsbValue PsbValue::fromJson(const std::string &json) {
  JsonParser parser{json, 0};
  return parser.parseValue();
}

bool PsbFile::isMdfBytes(const std::uint8_t *data, std::size_t size) noexcept {
  return data && size >= 8 && std::memcmp(data, kMdfMagic, 4) == 0;
}

bool PsbFile::isPsbBytes(const std::uint8_t *data, std::size_t size) noexcept {
  return data && size >= 40 && std::memcmp(data, kPsbMagic, 4) == 0;
}

std::vector<std::uint8_t> PsbFile::decompressMdf(const std::uint8_t *data,
                                                 std::size_t size) {
  if (!isMdfBytes(data, size))
    throw std::runtime_error("PSB: input is not an MDF container");
  const std::uint32_t expectedSize = readU32LE(data + 4);
  constexpr std::uint32_t kMaxMdfOutput = 256u * 1024u * 1024u;
  if (expectedSize > kMaxMdfOutput)
    throw std::runtime_error("PSB: MDF declared size exceeds limit");

  z_stream stream{};
  stream.next_in = const_cast<std::uint8_t *>(data + 8);
  stream.avail_in = static_cast<uInt>(size - 8);
  if (inflateInit(&stream) != Z_OK)
    throw std::runtime_error("PSB: zlib inflateInit failed");

  std::vector<std::uint8_t> out(expectedSize ? expectedSize : 65536u);
  std::size_t total = 0;
  int ret = Z_OK;
  do {
    if (total == out.size()) {
      if (out.size() >= kMaxMdfOutput) {
        inflateEnd(&stream);
        throw std::runtime_error("PSB: MDF output exceeds maximum size");
      }
      out.resize(std::min<std::size_t>(out.size() * 2, kMaxMdfOutput));
    }
    stream.next_out = out.data() + total;
    stream.avail_out = static_cast<uInt>(out.size() - total);
    const auto prevIn = stream.total_in;
    const auto prevOut = stream.total_out;
    ret = inflate(&stream, Z_NO_FLUSH);
    total = stream.total_out;
    if (ret != Z_STREAM_END && stream.total_in == prevIn &&
        stream.total_out == prevOut) {
      inflateEnd(&stream);
      throw std::runtime_error("PSB: truncated or stalled MDF zlib stream");
    }
    if (ret != Z_OK && ret != Z_STREAM_END && ret != Z_BUF_ERROR) {
      inflateEnd(&stream);
      throw std::runtime_error("PSB: MDF zlib inflate failed");
    }
  } while (ret != Z_STREAM_END);

  inflateEnd(&stream);
  if (expectedSize != 0 && total != expectedSize)
    throw std::runtime_error("PSB: MDF uncompressed size mismatch");
  out.resize(total);
  return out;
}

std::vector<std::uint8_t>
PsbFile::wrapMdf(const std::vector<std::uint8_t> &psb) {
  uLongf compSize = compressBound(static_cast<uLong>(psb.size()));
  std::vector<std::uint8_t> out(8 + compSize);
  std::memcpy(out.data(), kMdfMagic, 4);
  const std::uint32_t rawLen = static_cast<std::uint32_t>(psb.size());
  out[4] = static_cast<std::uint8_t>(rawLen & 0xffu);
  out[5] = static_cast<std::uint8_t>((rawLen >> 8) & 0xffu);
  out[6] = static_cast<std::uint8_t>((rawLen >> 16) & 0xffu);
  out[7] = static_cast<std::uint8_t>((rawLen >> 24) & 0xffu);
  if (compress2(out.data() + 8, &compSize, psb.data(),
                static_cast<uLong>(psb.size()), Z_BEST_SPEED) != Z_OK)
    throw std::runtime_error("PSB: cannot compress MDF container");
  out.resize(8 + compSize);
  return out;
}

PsbFile PsbFile::parse(const std::vector<std::uint8_t> &bytes) {
  return parse(bytes.data(), bytes.size());
}

PsbFile PsbFile::parse(const std::uint8_t *data, std::size_t size) {
  if (!data || size < 8)
    throw std::runtime_error("PSB: buffer too small");
  std::vector<std::uint8_t> owned;
  if (isMdfBytes(data, size)) {
    owned = decompressMdf(data, size);
    data = owned.data();
    size = owned.size();
  }
  if (!isPsbBytes(data, size))
    throw std::runtime_error("PSB: invalid PSB magic or truncated header");

  PsbFile file;
  if (!owned.empty())
    file.raw_ = std::move(owned);
  else
    file.raw_.assign(data, data + size);

  const auto *raw = file.raw_.data();
  const std::size_t rawSize = file.raw_.size();

  auto &hdr = file.header_;
  hdr.version = readU16LE(raw + 4);
  hdr.encrypt = readU16LE(raw + 6);
  hdr.offEncrypt = readU32LE(raw + 8);
  hdr.offNames = readU32LE(raw + 12);
  hdr.offStrings = readU32LE(raw + 16);
  hdr.offStringsData = readU32LE(raw + 20);
  hdr.offChunkOffsets = readU32LE(raw + 24);
  hdr.offChunkLengths = readU32LE(raw + 28);
  hdr.offChunkData = readU32LE(raw + 32);
  hdr.offEntries = readU32LE(raw + 36);

  if (hdr.version < 2 || hdr.version > 4)
    throw std::runtime_error("PSB: unsupported PSB version " +
                             std::to_string(hdr.version));
  if (hdr.encrypt != 0)
    throw std::runtime_error("PSB: encrypted PSB streams are not supported");

  const auto readUintArray = [&](std::size_t pos,
                                 std::size_t *nextPos =
                                     nullptr) -> std::vector<std::uint64_t> {
    if (pos >= rawSize)
      throw std::runtime_error("PSB: uint array offset out of bounds");
    if (raw[pos] == 0x00u && pos + 1 < rawSize && raw[pos + 1] >= 0x0cu &&
        raw[pos + 1] <= 0x14u)
      ++pos;
    const std::uint8_t tag = raw[pos];
    if (tag == 0x00u || tag == 0x01u || tag == 0x04u) {
      if (nextPos)
        *nextPos = pos + 1;
      return {};
    }
    if (tag < 0x0cu || tag > 0x14u)
      throw std::runtime_error("PSB: invalid uint array header tag " +
                               std::to_string(tag) + " at pos " +
                               std::to_string(pos));
    const std::size_t countBytes = tag - 0x0cu;
    if (pos + 1 + countBytes + 1 > rawSize)
      throw std::runtime_error("PSB: truncated uint array header");
    const std::uint64_t count =
        countBytes ? readUnsignedLE(raw + pos + 1, countBytes) : 0;
    const std::uint8_t elemTag = raw[pos + 1 + countBytes];
    if (count == 0) {
      if (nextPos)
        *nextPos = pos + 2 + countBytes;
      return {};
    }
    if (elemTag < 0x0cu || elemTag > 0x14u)
      throw std::runtime_error("PSB: invalid uint array element tag " +
                               std::to_string(elemTag));
    const std::size_t elemBytes = elemTag - 0x0cu;
    const std::size_t dataStart = pos + 2 + countBytes;
    if (elemBytes > 0 && count > (rawSize - dataStart) / elemBytes)
      throw std::runtime_error("PSB: uint array exceeds file bounds");
    std::vector<std::uint64_t> result(static_cast<std::size_t>(count), 0);
    if (elemBytes > 0) {
      for (std::size_t i = 0; i < result.size(); ++i)
        result[i] = readUnsignedLE(raw + dataStart + i * elemBytes, elemBytes);
    }
    if (nextPos)
      *nextPos = dataStart + result.size() * elemBytes;
    return result;
  };

  // 1. Decode Double-Array Name Trie at offNames
  if (hdr.offNames != 0) {
    if (hdr.offNames >= rawSize)
      throw std::runtime_error("PSB: offNames out of bounds");
    std::size_t p1 = 0, p2 = 0;
    const auto offsets = readUintArray(hdr.offNames, &p1);
    const auto trees = readUintArray(p1, &p2);
    const auto orgs = readUintArray(p2, nullptr);
    file.names_.resize(orgs.size());
    for (std::size_t i = 0; i < orgs.size(); ++i) {
      std::uint64_t cur = orgs[i];
      std::string name;
      std::size_t guard = 0;
      while (cur != 0) {
        if (cur >= trees.size() || ++guard > trees.size())
          throw std::runtime_error("PSB: corrupt double-array name trie");
        const std::uint64_t parent = trees[static_cast<std::size_t>(cur)];
        if (parent >= offsets.size() ||
            cur < offsets[static_cast<std::size_t>(parent)])
          throw std::runtime_error("PSB: invalid name trie transition");
        const std::uint64_t ch =
            cur - offsets[static_cast<std::size_t>(parent)];
        if (ch > 0xffu)
          throw std::runtime_error("PSB: invalid byte in name trie");
        if (ch != 0)
          name.push_back(static_cast<char>(ch));
        cur = parent;
      }
      std::reverse(name.begin(), name.end());
      file.names_[i] = std::move(name);
    }
  }

  // 2. Decode String Offsets Table at offStrings
  std::vector<std::uint64_t> stringOffsets;
  if (hdr.offStrings != 0) {
    if (hdr.offStrings >= rawSize || hdr.offStringsData > rawSize)
      throw std::runtime_error("PSB: offStrings out of bounds");
    stringOffsets = readUintArray(hdr.offStrings, nullptr);
  }

  // 3. Decode Chunk Table at offChunkOffsets / offChunkLengths
  if (hdr.offChunkOffsets != 0 && hdr.offChunkLengths != 0) {
    if (hdr.offChunkOffsets >= rawSize || hdr.offChunkLengths >= rawSize)
      throw std::runtime_error("PSB: chunk table out of bounds");
    std::size_t afterChunkOffsets = 0;
    const auto chunkOffsets =
        readUintArray(hdr.offChunkOffsets, &afterChunkOffsets);
    const auto chunkLengths = readUintArray(
        std::max<std::size_t>(hdr.offChunkLengths, afterChunkOffsets), nullptr);
    if (chunkOffsets.size() != chunkLengths.size())
      throw std::runtime_error("PSB: chunk offset/length count mismatch");
    file.chunks_.resize(chunkOffsets.size());
    for (std::size_t i = 0; i < chunkOffsets.size(); ++i) {
      const std::uint64_t off = chunkOffsets[i];
      const std::uint64_t len = chunkLengths[i];
      if (hdr.offChunkData > rawSize || off > rawSize - hdr.offChunkData ||
          len > rawSize - hdr.offChunkData - off)
        throw std::runtime_error("PSB: chunk range exceeds file bounds");
      file.chunks_[i] = {static_cast<std::uint32_t>(i),
                         static_cast<std::uint32_t>(hdr.offChunkData + off),
                         static_cast<std::uint32_t>(len)};
    }
  }

  // 4. Recursively parse entry tree at offEntries
  if (hdr.offEntries == 0 || hdr.offEntries >= rawSize)
    throw std::runtime_error("PSB: offEntries out of bounds");

  auto parseAt = [&](auto &self, std::size_t pos,
                     std::size_t depth) -> PsbValue {
    if (pos >= rawSize)
      throw std::runtime_error("PSB: value offset out of bounds");
    if (depth > 256)
      throw std::runtime_error("PSB: nesting depth limit exceeded");
    const std::uint8_t tag = raw[pos];
    if (tag == 0x00u || tag == 0x01u)
      return PsbValue::makeNull();
    if (tag == 0x02u)
      return PsbValue::makeBool(true);
    if (tag == 0x03u)
      return PsbValue::makeBool(false);
    if (tag >= 0x04u && tag <= 0x0cu) {
      const std::size_t n = tag - 0x04u;
      if (pos + 1 + n > rawSize)
        throw std::runtime_error("PSB: truncated integer value");
      return PsbValue::makeInt(readSignedLE(raw + pos + 1, n));
    }
    if (tag >= 0x0du && tag <= 0x14u) {
      return PsbValue::makeUIntArray(readUintArray(pos, nullptr));
    }
    if (tag >= 0x15u && tag <= 0x18u) {
      const std::size_t n = tag - 0x14u;
      if (pos + 1 + n > rawSize)
        throw std::runtime_error("PSB: truncated string index");
      const std::uint64_t idx = readUnsignedLE(raw + pos + 1, n);
      if (idx >= stringOffsets.size())
        throw std::runtime_error("PSB: string index out of range");
      const std::uint64_t strOff =
          hdr.offStringsData + stringOffsets[static_cast<std::size_t>(idx)];
      if (strOff >= rawSize)
        throw std::runtime_error("PSB: string data offset out of bounds");
      const char *start = reinterpret_cast<const char *>(raw + strOff);
      const std::size_t maxLen = rawSize - static_cast<std::size_t>(strOff);
      const std::size_t len = strnlen(start, maxLen);
      if (len == maxLen)
        throw std::runtime_error("PSB: unterminated string in string table");
      return PsbValue::makeString(std::string(start, len));
    }
    if (tag >= 0x19u && tag <= 0x1cu) {
      const std::size_t n = tag - 0x18u;
      if (pos + 1 + n > rawSize)
        throw std::runtime_error("PSB: truncated chunk index");
      const std::uint64_t idx = readUnsignedLE(raw + pos + 1, n);
      if (idx >= file.chunks_.size())
        throw std::runtime_error("PSB: chunk index out of range");
      return PsbValue::makeChunk(file.chunks_[static_cast<std::size_t>(idx)]);
    }
    if (tag == 0x1du)
      return PsbValue::makeFloat(0.0f);
    if (tag == 0x1eu) {
      if (pos + 5 > rawSize)
        throw std::runtime_error("PSB: truncated float value");
      float f = 0.0f;
      std::uint32_t bits = readU32LE(raw + pos + 1);
      std::memcpy(&f, &bits, sizeof(f));
      return PsbValue::makeFloat(f);
    }
    if (tag == 0x1fu) {
      if (pos + 9 > rawSize)
        throw std::runtime_error("PSB: truncated double value");
      double d = 0.0;
      std::uint64_t bits = readUnsignedLE(raw + pos + 1, 8);
      std::memcpy(&d, &bits, sizeof(d));
      return PsbValue::makeDouble(d);
    }
    if (tag == 0x20u) {
      std::size_t payloadStart = 0;
      const auto offsets = readUintArray(pos + 1, &payloadStart);
      PsbValue::List list;
      list.reserve(offsets.size());
      for (std::uint64_t rel : offsets) {
        if (rel > rawSize - payloadStart)
          throw std::runtime_error("PSB: list element offset out of bounds");
        list.push_back(self(self, payloadStart + static_cast<std::size_t>(rel),
                            depth + 1));
      }
      return PsbValue::makeList(std::move(list));
    }
    if (tag == 0x21u) {
      std::size_t afterKeys = 0;
      std::size_t payloadStart = 0;
      const auto keyIds = readUintArray(pos + 1, &afterKeys);
      const auto valOffsets = readUintArray(afterKeys, &payloadStart);
      if (keyIds.size() != valOffsets.size())
        throw std::runtime_error("PSB: dictionary key/value count mismatch");
      PsbValue::Dict dict;
      for (std::size_t i = 0; i < keyIds.size(); ++i) {
        const std::uint64_t keyId = keyIds[i];
        if (keyId >= file.names_.size())
          throw std::runtime_error("PSB: dictionary key ID out of range");
        const std::uint64_t rel = valOffsets[i];
        if (rel > rawSize - payloadStart)
          throw std::runtime_error(
              "PSB: dictionary value offset out of bounds");
        dict.emplace(file.names_[static_cast<std::size_t>(keyId)],
                     self(self, payloadStart + static_cast<std::size_t>(rel),
                          depth + 1));
      }
      return PsbValue::makeDict(std::move(dict));
    }
    throw std::runtime_error("PSB: unknown value tag " + std::to_string(tag));
  };

  file.root_ = parseAt(parseAt, hdr.offEntries, 0);
  return file;
}

std::vector<std::uint8_t> PsbFile::chunkBytes(std::uint32_t index) const {
  if (index >= chunks_.size())
    throw std::out_of_range("PSB: chunk index out of range");
  const auto &c = chunks_[index];
  if (c.offset > raw_.size() || c.length > raw_.size() - c.offset)
    throw std::runtime_error("PSB: invalid chunk slice");
  return std::vector<std::uint8_t>(raw_.begin() + c.offset,
                                   raw_.begin() + c.offset + c.length);
}

std::vector<std::uint8_t>
PsbFile::serializeV2(const PsbValue &root,
                     const std::vector<std::vector<std::uint8_t>> &chunks,
                     bool wrapInMdf) {
  std::set<std::string> nameSet;
  std::vector<std::string> stringList;
  std::map<std::string, std::uint32_t> stringMap;

  auto collect = [&](auto &self, const PsbValue &v) -> void {
    if (v.isString()) {
      const auto &s = v.asString();
      if (stringMap.find(s) == stringMap.end()) {
        const auto id = static_cast<std::uint32_t>(stringList.size());
        stringMap.emplace(s, id);
        stringList.push_back(s);
      }
    } else if (v.isList()) {
      for (const auto &item : v.asList())
        self(self, item);
    } else if (v.isDict()) {
      for (const auto &[k, val] : v.asDict()) {
        nameSet.insert(k);
        self(self, val);
      }
    }
  };
  collect(collect, root);

  const std::vector<std::string> names(nameSet.begin(), nameSet.end());
  std::map<std::string, std::uint32_t> nameIndex;
  for (std::size_t i = 0; i < names.size(); ++i)
    nameIndex.emplace(names[i], static_cast<std::uint32_t>(i));

  // Build Double-Array Name Trie for `names`.
  struct TrieNode {
    std::map<std::uint8_t, std::size_t> children;
  };
  std::vector<TrieNode> trie(1);
  std::vector<std::size_t> terminalTrieNode(names.size(), 0);
  for (std::size_t i = 0; i < names.size(); ++i) {
    std::size_t cur = 0;
    for (unsigned char ch : names[i]) {
      const std::uint8_t b = ch == 0 ? 1u : ch;
      auto it = trie[cur].children.find(b);
      if (it == trie[cur].children.end()) {
        const std::size_t next = trie.size();
        trie.emplace_back();
        trie[cur].children.emplace(b, next);
        cur = next;
      } else {
        cur = it->second;
      }
    }
    // Add terminal NUL transition so prefix keys never collide with internal
    // nodes.
    auto it = trie[cur].children.find(0);
    if (it == trie[cur].children.end()) {
      const std::size_t term = trie.size();
      trie.emplace_back();
      trie[cur].children.emplace(0, term);
      cur = term;
    } else {
      cur = it->second;
    }
    terminalTrieNode[i] = cur;
  }

  std::vector<std::uint64_t> daOffsets(1, 0);
  std::vector<std::uint64_t> daTrees(1, 0);
  std::vector<bool> daUsed(1, true);
  std::vector<std::size_t> daIndex(trie.size(), 0);

  auto assignDa = [&](auto &self, std::size_t u) -> void {
    if (trie[u].children.empty())
      return;
    const std::size_t uDa = daIndex[u];
    std::size_t base = 1;
    while (true) {
      bool ok = true;
      for (const auto &[ch, _] : trie[u].children) {
        const std::size_t idx = base + ch;
        if (idx < daUsed.size() && daUsed[idx]) {
          ok = false;
          break;
        }
      }
      if (ok)
        break;
      ++base;
    }
    if (uDa >= daOffsets.size()) {
      daOffsets.resize(uDa + 1, 0);
      daTrees.resize(uDa + 1, 0);
      daUsed.resize(uDa + 1, false);
    }
    daOffsets[uDa] = base;
    for (const auto &[ch, child] : trie[u].children) {
      const std::size_t cDa = base + ch;
      if (cDa >= daOffsets.size()) {
        daOffsets.resize(cDa + 1, 0);
        daTrees.resize(cDa + 1, 0);
        daUsed.resize(cDa + 1, false);
      }
      daUsed[cDa] = true;
      daTrees[cDa] = uDa;
      daIndex[child] = cDa;
    }
    for (const auto &[_, child] : trie[u].children)
      self(self, child);
  };
  assignDa(assignDa, 0);

  std::vector<std::uint64_t> daOrgs(names.size(), 0);
  for (std::size_t i = 0; i < names.size(); ++i)
    daOrgs[i] = daIndex[terminalTrieNode[i]];

  // Encode value tree recursively
  auto encodeValue = [&](auto &self,
                         const PsbValue &v) -> std::vector<std::uint8_t> {
    std::vector<std::uint8_t> buf;
    switch (v.type()) {
    case PsbValue::Type::Null:
      buf.push_back(0x01u);
      break;
    case PsbValue::Type::Bool:
      buf.push_back(v.asBool() ? 0x02u : 0x03u);
      break;
    case PsbValue::Type::Int: {
      const std::int64_t val = v.asInt();
      if (val == 0) {
        buf.push_back(0x04u);
      } else if (val >= -128 && val <= 127) {
        buf.push_back(0x05u);
        buf.push_back(static_cast<std::uint8_t>(val & 0xff));
      } else if (val >= -32768 && val <= 32767) {
        buf.push_back(0x06u);
        appendUnsignedLE(buf, static_cast<std::uint64_t>(val), 2);
      } else if (val >= -2147483648LL && val <= 2147483647LL) {
        buf.push_back(0x08u);
        appendUnsignedLE(buf, static_cast<std::uint64_t>(val), 4);
      } else {
        buf.push_back(0x0cu);
        appendUnsignedLE(buf, static_cast<std::uint64_t>(val), 8);
      }
      break;
    }
    case PsbValue::Type::UIntArray:
      appendUintArray(buf, v.asUIntArray());
      break;
    case PsbValue::Type::Float: {
      const float f = v.asFloat();
      if (f == 0.0f) {
        buf.push_back(0x1du);
      } else {
        buf.push_back(0x1eu);
        std::uint32_t bits = 0;
        std::memcpy(&bits, &f, sizeof(bits));
        appendU32LE(buf, bits);
      }
      break;
    }
    case PsbValue::Type::Double: {
      const double d = v.asDouble();
      buf.push_back(0x1fu);
      std::uint64_t bits = 0;
      std::memcpy(&bits, &d, sizeof(bits));
      appendUnsignedLE(buf, bits, 8);
      break;
    }
    case PsbValue::Type::String: {
      const std::uint32_t idx = stringMap.at(v.asString());
      const std::size_t n = minUnsignedBytes(idx);
      buf.push_back(static_cast<std::uint8_t>(0x14u + n));
      appendUnsignedLE(buf, idx, n);
      break;
    }
    case PsbValue::Type::Chunk: {
      const std::uint32_t idx = v.asChunk().index;
      const std::size_t n = minUnsignedBytes(idx);
      buf.push_back(static_cast<std::uint8_t>(0x18u + n));
      appendUnsignedLE(buf, idx, n);
      break;
    }
    case PsbValue::Type::List: {
      const auto &items = v.asList();
      std::vector<std::uint64_t> offsets;
      offsets.reserve(items.size());
      std::vector<std::uint8_t> payload;
      for (const auto &item : items) {
        offsets.push_back(payload.size());
        auto enc = self(self, item);
        payload.insert(payload.end(), enc.begin(), enc.end());
      }
      buf.push_back(0x20u);
      appendUintArray(buf, offsets);
      buf.insert(buf.end(), payload.begin(), payload.end());
      break;
    }
    case PsbValue::Type::Dict: {
      const auto &entries = v.asDict();
      std::vector<std::uint64_t> keyIds;
      std::vector<std::uint64_t> valOffsets;
      keyIds.reserve(entries.size());
      valOffsets.reserve(entries.size());
      std::vector<std::uint8_t> payload;
      for (const auto &[k, val] : entries) {
        keyIds.push_back(nameIndex.at(k));
        valOffsets.push_back(payload.size());
        auto enc = self(self, val);
        payload.insert(payload.end(), enc.begin(), enc.end());
      }
      buf.push_back(0x21u);
      appendUintArray(buf, keyIds);
      appendUintArray(buf, valOffsets);
      buf.insert(buf.end(), payload.begin(), payload.end());
      break;
    }
    }
    return buf;
  };

  const auto encodedEntries = encodeValue(encodeValue, root);

  std::vector<std::uint8_t> out(40, 0);
  std::memcpy(out.data(), kPsbMagic, 4);
  out[4] = 2; // version = 2
  out[5] = 0;
  out[6] = 0; // encrypt = 0
  out[7] = 0;

  const auto patchHeader = [&](std::size_t headerOffset, std::uint32_t value) {
    for (int i = 0; i < 4; ++i)
      out[headerOffset + i] =
          static_cast<std::uint8_t>((value >> (8 * i)) & 0xffu);
  };

  // offNames
  const std::uint32_t offNames = static_cast<std::uint32_t>(out.size());
  appendUintArray(out, daOffsets);
  appendUintArray(out, daTrees);
  appendUintArray(out, daOrgs);

  // offStrings & offStringsData
  std::vector<std::uint64_t> strOffsets;
  std::vector<std::uint8_t> strData;
  for (const auto &s : stringList) {
    strOffsets.push_back(strData.size());
    strData.insert(strData.end(), s.begin(), s.end());
    strData.push_back(0);
  }
  const std::uint32_t offStrings = static_cast<std::uint32_t>(out.size());
  appendUintArray(out, strOffsets);
  const std::uint32_t offStringsData = static_cast<std::uint32_t>(out.size());
  out.insert(out.end(), strData.begin(), strData.end());

  // offChunkOffsets, offChunkLengths, offChunkData
  std::vector<std::uint64_t> chunkOffsets;
  std::vector<std::uint64_t> chunkLengths;
  std::vector<std::uint8_t> chunkData;
  for (const auto &c : chunks) {
    chunkOffsets.push_back(chunkData.size());
    chunkLengths.push_back(c.size());
    chunkData.insert(chunkData.end(), c.begin(), c.end());
  }
  const std::uint32_t offChunkOffsets = static_cast<std::uint32_t>(out.size());
  appendUintArray(out, chunkOffsets);
  const std::uint32_t offChunkLengths = static_cast<std::uint32_t>(out.size());
  appendUintArray(out, chunkLengths);
  const std::uint32_t offChunkData = static_cast<std::uint32_t>(out.size());
  out.insert(out.end(), chunkData.begin(), chunkData.end());

  // offEntries
  const std::uint32_t offEntries = static_cast<std::uint32_t>(out.size());
  out.insert(out.end(), encodedEntries.begin(), encodedEntries.end());

  patchHeader(8, offNames); // offEncrypt points to start of body when enc=0
  patchHeader(12, offNames);
  patchHeader(16, offStrings);
  patchHeader(20, offStringsData);
  patchHeader(24, offChunkOffsets);
  patchHeader(28, offChunkLengths);
  patchHeader(32, offChunkData);
  patchHeader(36, offEntries);

  return wrapInMdf ? wrapMdf(out) : out;
}

} // namespace Kotonoha::Providers
