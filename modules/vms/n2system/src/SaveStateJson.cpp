// Copyright 2026 Kotonoha contributors. SPDX-License-Identifier: MIT
// Portable JSON debug-save codec. The binary N2SV format is used for retail
// save slots; JSON remains a round-trippable, human-readable interchange.
#include <N2System/SaveState.hpp>

#include <algorithm>
#include <charconv>
#include <cctype>
#include <cstdint>
#include <limits>
#include <map>
#include <sstream>
#include <type_traits>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace N2System {
namespace {
void quoted(std::ostringstream &out, std::string_view value) {
  static constexpr char hex[] = "0123456789abcdef";
  out << '"';
  for (unsigned char c : value) {
    switch (c) {
    case '"': out << "\\\""; break;
    case '\\': out << "\\\\"; break;
    case '\n': out << "\\n"; break;
    case '\r': out << "\\r"; break;
    case '\t': out << "\\t"; break;
    case '\b': out << "\\b"; break;
    case '\f': out << "\\f"; break;
    default:
      if (c < 0x20) {
        out << "\\u00" << hex[c >> 4] << hex[c & 15];
      } else {
        out << static_cast<char>(c);
      }
    }
  }
  out << '"';
}

void appendUtf8(std::string &out, std::uint32_t cp) {
  if (cp <= 0x7f) {
    out.push_back(static_cast<char>(cp));
  } else if (cp <= 0x7ff) {
    out.push_back(static_cast<char>(0xc0 | (cp >> 6)));
    out.push_back(static_cast<char>(0x80 | (cp & 0x3f)));
  } else if (cp <= 0xffff) {
    out.push_back(static_cast<char>(0xe0 | (cp >> 12)));
    out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3f)));
    out.push_back(static_cast<char>(0x80 | (cp & 0x3f)));
  } else {
    out.push_back(static_cast<char>(0xf0 | (cp >> 18)));
    out.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3f)));
    out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3f)));
    out.push_back(static_cast<char>(0x80 | (cp & 0x3f)));
  }
}

enum class Kind { Null, String, Integer, Boolean, Object, Array };
struct Value {
  Kind kind = Kind::Null;
  std::string text;
  std::int64_t integer = 0;
  bool boolean = false;
  std::map<std::string, Value> object;
  std::vector<Value> array;
  const Value *field(std::string_view key) const {
    const auto it = object.find(std::string(key));
    return it == object.end() ? nullptr : &it->second;
  }
};

class Parser {
public:
  explicit Parser(std::string_view source) : source_(source) {}
  bool parse(Value &root) {
    if (source_.size() > 36u * 1024u * 1024u)
      return false;
    skipSpace();
    if (!readValue(root, 0))
      return false;
    skipSpace();
    return cursor_ == source_.size();
  }

private:
  std::string_view source_;
  std::size_t cursor_ = 0;

  void skipSpace() {
    while (cursor_ < source_.size() &&
           (source_[cursor_] == ' ' || source_[cursor_] == '\n' ||
            source_[cursor_] == '\r' || source_[cursor_] == '\t'))
      ++cursor_;
  }
  bool consume(char expected) {
    skipSpace();
    if (cursor_ >= source_.size() || source_[cursor_] != expected)
      return false;
    ++cursor_;
    return true;
  }
  bool hex4(std::uint32_t &value) {
    if (source_.size() - cursor_ < 4)
      return false;
    value = 0;
    for (int i = 0; i < 4; ++i) {
      const char c = source_[cursor_++];
      const int n = c >= '0' && c <= '9' ? c - '0' :
                    c >= 'a' && c <= 'f' ? c - 'a' + 10 :
                    c >= 'A' && c <= 'F' ? c - 'A' + 10 : -1;
      if (n < 0)
        return false;
      value = (value << 4) | static_cast<std::uint32_t>(n);
    }
    return true;
  }
  bool readString(std::string &value) {
    if (!consume('"'))
      return false;
    value.clear();
    while (cursor_ < source_.size()) {
      const auto c = static_cast<unsigned char>(source_[cursor_++]);
      if (c == '"')
        return true;
      if (c < 0x20)
        return false;
      if (c == '\\') {
        if (cursor_ >= source_.size())
          return false;
        const char next = source_[cursor_++];
        switch (next) {
        case '"': value.push_back('"'); break;
        case '\\': value.push_back('\\'); break;
        case '/': value.push_back('/'); break;
        case 'n': value.push_back('\n'); break;
        case 'r': value.push_back('\r'); break;
        case 't': value.push_back('\t'); break;
        case 'b': value.push_back('\b'); break;
        case 'f': value.push_back('\f'); break;
        case 'u': {
          std::uint32_t cp;
          if (!hex4(cp))
            return false;
          if (cp >= 0xd800 && cp <= 0xdbff) {
            if (source_.size() - cursor_ < 6 ||
                source_.substr(cursor_, 2) != "\\u")
              return false;
            cursor_ += 2;
            std::uint32_t low;
            if (!hex4(low) || low < 0xdc00 || low > 0xdfff)
              return false;
            cp = 0x10000 + ((cp - 0xd800) << 10) + (low - 0xdc00);
          } else if (cp >= 0xdc00 && cp <= 0xdfff) {
            return false;
          }
          appendUtf8(value, cp);
          break;
        }
        default:
          return false;
        }
      } else {
        value.push_back(static_cast<char>(c));
      }
      if (value.size() > 16u * 1024u * 1024u)
        return false;
    }
    return false;
  }
  bool readValue(Value &value, int depth) {
    skipSpace();
    if (depth > 24 || cursor_ == source_.size())
      return false;
    const char c = source_[cursor_];
    if (c == '"') {
      value.kind = Kind::String;
      return readString(value.text);
    }
    if (c == '{') {
      value.kind = Kind::Object;
      ++cursor_;
      if (consume('}'))
        return true;
      for (;;) {
        std::string name;
        if (!readString(name) || !consume(':'))
          return false;
        Value member;
        if (!readValue(member, depth + 1) ||
            !value.object.emplace(std::move(name), std::move(member)).second)
          return false;
        if (consume('}'))
          return true;
        if (!consume(',') || value.object.size() > 16384)
          return false;
      }
    }
    if (c == '[') {
      value.kind = Kind::Array;
      ++cursor_;
      if (consume(']'))
        return true;
      for (;;) {
        Value member;
        if (!readValue(member, depth + 1))
          return false;
        value.array.push_back(std::move(member));
        if (consume(']'))
          return true;
        if (!consume(',') || value.array.size() > 16384)
          return false;
      }
    }
    const auto match = [this](std::string_view literal) {
      if (source_.substr(cursor_, literal.size()) == literal) {
        cursor_ += literal.size();
        return true;
      }
      return false;
    };
    if (match("true")) {
      value.kind = Kind::Boolean; value.boolean = true; return true;
    }
    if (match("false")) {
      value.kind = Kind::Boolean; value.boolean = false; return true;
    }
    if (match("null")) {
      value.kind = Kind::Null; return true;
    }
    const std::size_t start = cursor_;
    if (c == '-')
      ++cursor_;
    if (cursor_ >= source_.size() ||
        source_[cursor_] < '0' || source_[cursor_] > '9')
      return false;
    if (source_[cursor_] == '0') {
      ++cursor_;
    } else {
      while (cursor_ < source_.size() &&
             source_[cursor_] >= '0' && source_[cursor_] <= '9')
        ++cursor_;
    }
    const auto parsed = std::from_chars(
        source_.data() + start, source_.data() + cursor_, value.integer);
    value.kind = Kind::Integer;
    return parsed.ec == std::errc{} &&
           parsed.ptr == source_.data() + cursor_;
  }
};

template <typename T>
void writeMap(std::ostringstream &out, const std::map<std::string, T> &values) {
  out << '{';
  bool first = true;
  for (const auto &[name, val] : values) {
    if (!first)
      out << ',';
    first = false;
    quoted(out, name);
    out << ':';
    if constexpr (std::is_same_v<T, std::string>) {
      quoted(out, val);
    } else if constexpr (std::is_same_v<T, bool>) {
      out << (val ? "true" : "false");
    } else {
      out << val;
    }
  }
  out << '}';
}

template <typename T>
bool readMap(const Value *value, std::map<std::string, T> &out) {
  if (!value)
    return true; // Old debug saves may omit optional maps.
  if (value->kind != Kind::Object)
    return false;
  for (const auto &[key, entry] : value->object) {
    if constexpr (std::is_same_v<T, std::string>) {
      if (entry.kind != Kind::String) return false;
      out[key] = entry.text;
    } else if constexpr (std::is_same_v<T, bool>) {
      if (entry.kind != Kind::Boolean) return false;
      out[key] = entry.boolean;
    } else {
      if (entry.kind != Kind::Integer) return false;
      out[key] = static_cast<T>(entry.integer);
    }
  }
  return true;
}
int nibble(char c) {
  return c >= '0' && c <= '9' ? c - '0' :
         c >= 'a' && c <= 'f' ? c - 'a' + 10 :
         c >= 'A' && c <= 'F' ? c - 'A' + 10 : -1;
}
} // namespace

std::string SaveStateCodec::serializeJson(const SaveData &data) {
  std::ostringstream out;
  out << "{\"chapter\":";
  quoted(out, data.chapterName);
  out << ",\"position\":" << data.position << ",\"activeSpeaker\":";
  quoted(out, data.activeSpeaker);
  out << ",\"activeText\":";
  quoted(out, data.activeText);
  out << ",\"activeVoice\":";
  quoted(out, data.activeVoice);
  out << ",\"integers\":";
  writeMap(out, data.integers);
  out << ",\"strings\":";
  writeMap(out, data.strings);
  out << ",\"flags\":";
  writeMap(out, data.flags);
  out << ",\"backlog\":[";
  bool first = true;
  for (const auto &entry : data.backlog) {
    if (!first) out << ',';
    first = false;
    out << "{\"speaker\":";
    quoted(out, entry.speaker);
    out << ",\"text\":";
    quoted(out, entry.text);
    out << ",\"voicePath\":";
    quoted(out, entry.voicePath);
    out << '}';
  }
  out << ']';
  if (!data.runtimeBlob.empty()) {
    static constexpr char digits[] = "0123456789abcdef";
    std::string hex;
    hex.reserve(data.runtimeBlob.size() * 2);
    for (auto byte : data.runtimeBlob) {
      hex.push_back(digits[byte >> 4]);
      hex.push_back(digits[byte & 15]);
    }
    out << ",\"runtimeHex\":";
    quoted(out, hex);
  }
  out << "}\n";
  return out.str();
}

bool SaveStateCodec::deserializeJson(std::string_view json, SaveData &outData) {
  Value root;
  if (!Parser(json).parse(root) || root.kind != Kind::Object)
    return false;
  SaveData parsed;
  const auto getString = [&root](std::string_view key, std::string &field) {
    const Value *value = root.field(key);
    if (!value)
      return true;
    if (value->kind != Kind::String)
      return false;
    field = value->text;
    return true;
  };
  if (!getString("chapter", parsed.chapterName) ||
      parsed.chapterName.empty() ||
      !getString("activeSpeaker", parsed.activeSpeaker) ||
      !getString("activeText", parsed.activeText) ||
      !getString("activeVoice", parsed.activeVoice))
    return false;
  if (const Value *position = root.field("position")) {
    if (position->kind != Kind::Integer || position->integer < 0)
      return false;
    parsed.position = static_cast<std::size_t>(position->integer);
  }
  if (!readMap(root.field("integers"), parsed.integers) ||
      !readMap(root.field("strings"), parsed.strings) ||
      !readMap(root.field("flags"), parsed.flags))
    return false;
  if (const Value *backlog = root.field("backlog")) {
    if (backlog->kind != Kind::Array || backlog->array.size() > 500)
      return false;
    for (const auto &item : backlog->array) {
      if (item.kind != Kind::Object)
        return false;
      DialogueBacklogEntry entry;
      const auto readText = [&item](std::string_view key, std::string &dst) {
        const auto *field = item.field(key);
        if (!field || field->kind != Kind::String)
          return false;
        dst = field->text;
        return true;
      };
      if (!readText("speaker", entry.speaker) ||
          !readText("text", entry.text) ||
          !readText("voicePath", entry.voicePath))
        return false;
      parsed.backlog.push_back(std::move(entry));
    }
  }
  if (const Value *runtime = root.field("runtimeHex")) {
    if (runtime->kind != Kind::String || runtime->text.size() % 2 ||
        runtime->text.size() > 32u * 1024u * 1024u)
      return false;
    parsed.runtimeBlob.reserve(runtime->text.size() / 2);
    for (std::size_t i = 0; i < runtime->text.size(); i += 2) {
      const int hi = nibble(runtime->text[i]);
      const int lo = nibble(runtime->text[i + 1]);
      if (hi < 0 || lo < 0)
        return false;
      parsed.runtimeBlob.push_back(static_cast<uint8_t>((hi << 4) | lo));
    }
  }
  outData = std::move(parsed);
  return true;
}
} // namespace N2System
