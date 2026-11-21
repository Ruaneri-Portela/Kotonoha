// Copyright 2026 Kotonoha contributors. SPDX-License-Identifier: MIT
#include <Kirikiri/KirikiriAssets.hpp>
#include <Kirikiri/TjsBootstrap.hpp>

#include <algorithm>
#include <cctype>
#include <cstring>
#include <string>
#include <vector>

namespace Kotonoha::Providers {
namespace {

struct Token {
  enum Kind { Identifier, Literal, Symbol } kind;
  std::string text;
};

bool identifierChar(unsigned char value) {
  return std::isalnum(value) != 0 || value == '_' || value == '$';
}

std::vector<Token> tokenize(const std::string &text) {
  std::vector<Token> tokens;
  // This is a bounded scanner, not an interpreter. Unknown TJS syntax is
  // ignored; comments and quoted text cannot create executable calls.
  const auto size = std::min<std::size_t>(text.size(), 256u * 1024u);
  for (std::size_t i = 0; i < size && tokens.size() < 32768;) {
    const auto value = static_cast<unsigned char>(text[i]);
    if (std::isspace(value)) {
      ++i;
      continue;
    }
    if (text[i] == '/' && i + 1 < size) {
      if (text[i + 1] == '/') {
        i += 2;
        while (i < size && text[i] != '\n' && text[i] != '\r')
          ++i;
        continue;
      }
      if (text[i + 1] == '*') {
        i += 2;
        while (i + 1 < size && !(text[i] == '*' && text[i + 1] == '/'))
          ++i;
        i = std::min(size, i + 2);
        continue;
      }
    }
    if (text[i] == '"' || text[i] == '\'') {
      const char quote = text[i++];
      std::string value;
      bool closed = false;
      while (i < size) {
        const char ch = text[i++];
        if (ch == quote) {
          closed = true;
          break;
        }
        if (ch == '\\' && i < size) {
          const char escaped = text[i++];
          if (escaped == '\\' || escaped == '\'' || escaped == '"')
            value += escaped;
          else if (escaped == 'n')
            value += '\n';
          else if (escaped == 't')
            value += '\t';
          else {
            value += '\\';
            value += escaped;
          }
        } else {
          value += ch;
        }
      }
      if (closed)
        tokens.push_back({Token::Literal, std::move(value)});
      continue;
    }
    if (identifierChar(value)) {
      const auto first = i++;
      while (i < size && identifierChar(static_cast<unsigned char>(text[i])))
        ++i;
      tokens.push_back({Token::Identifier, text.substr(first, i - first)});
      continue;
    }
    tokens.push_back({Token::Symbol, text.substr(i++, 1)});
  }
  return tokens;
}

std::string folded(std::string value) {
  for (auto &ch : value) {
    if (ch == '\\')
      ch = '/';
    else if (ch >= 'A' && ch <= 'Z')
      ch = static_cast<char>(ch - 'A' + 'a');
  }
  return value;
}

bool supportedCall(const std::string &receiver, const std::string &method) {
  return (receiver == "Scripts" &&
          (method == "execStorage" || method == "evalStorage")) ||
         (receiver == "Storages" && method == "isExistentStorage") ||
         (receiver == "Plugins" && method == "link");
}

bool safeStorage(const std::string &id) {
  if (id.empty() || id.size() > 1024 || id[0] == '/' ||
      id.find("://") != std::string::npos)
    return false;
  const auto normalized = folded(id);
  for (std::size_t i = 0; i < normalized.size();) {
    const auto next = normalized.find('/', i);
    if (normalized.substr(i, next == std::string::npos ? next : next - i) ==
        "..")
      return false;
    if (next == std::string::npos)
      break;
    i = next + 1;
  }
  return true;
}

} // namespace

std::vector<std::string>
extractTjs2CompiledStrings(const std::vector<std::uint8_t> &bytes) {
  std::vector<std::string> strings;
  if (bytes.size() < 28 || std::memcmp(bytes.data(), "TJS2100\0", 8) != 0)
    return strings;

  const auto readU16LE = [](const std::uint8_t *p) -> std::uint16_t {
    return std::uint16_t(p[0]) | (std::uint16_t(p[1]) << 8);
  };
  const auto readU32LE = [](const std::uint8_t *p) -> std::uint32_t {
    return std::uint32_t(p[0]) | (std::uint32_t(p[1]) << 8) |
           (std::uint32_t(p[2]) << 16) | (std::uint32_t(p[3]) << 24);
  };

  std::size_t pos = 12;
  while (pos + 8 <= bytes.size()) {
    if (std::memcmp(bytes.data() + pos, "DATA", 4) == 0) {
      const std::uint32_t dsz = readU32LE(bytes.data() + pos + 4);
      pos += 8;
      if (dsz > bytes.size() - pos || dsz < 28)
        break;
      const std::uint8_t *dbuf = bytes.data() + pos;
      const std::uint32_t numStr = readU32LE(dbuf + 20);
      std::size_t sp = 24;
      for (std::uint32_t i = 0; i < numStr && sp + 2 <= dsz; ++i) {
        const std::uint16_t slen = readU16LE(dbuf + sp);
        sp += 2;
        if (static_cast<std::size_t>(slen) * 2 > dsz - sp)
          break;
        std::string str;
        for (std::size_t k = 0; k < slen; ++k) {
          const std::uint16_t ch = readU16LE(dbuf + sp + k * 2);
          if (ch <= 0x7f)
            str.push_back(static_cast<char>(ch));
          else if (ch <= 0x7ff) {
            str.push_back(static_cast<char>(0xc0 | (ch >> 6)));
            str.push_back(static_cast<char>(0x80 | (ch & 0x3f)));
          } else {
            str.push_back(static_cast<char>(0xe0 | (ch >> 12)));
            str.push_back(static_cast<char>(0x80 | ((ch >> 6) & 0x3f)));
            str.push_back(static_cast<char>(0x80 | (ch & 0x3f)));
          }
        }
        strings.push_back(std::move(str));
        sp += static_cast<std::size_t>(slen) * 2;
      }
      break;
    }
    const std::uint32_t csz = readU32LE(bytes.data() + pos + 4);
    pos += 8 + csz;
  }
  return strings;
}

TjsBootstrapReport inspectTjsBootstrap(const KirikiriAssets &assets) {
  TjsBootstrapReport report;
  const auto files = assets.list();
  // Real KiriKiri normally launches startup.tjs at the mounted game root.
  // Avoid choosing arbitrary .tjs or unrelated patch scripts.
  for (const auto &name : files)
    if (folded(name) == "startup.tjs") {
      report.entry = name;
      break;
    }
  if (report.entry.empty())
    for (const auto &name : files)
      if (folded(name) == "system/startup.tjs") {
        report.entry = name;
        break;
      }
  if (report.entry.empty())
    return report;

  std::vector<std::uint8_t> rawBytes;
  try {
    rawBytes = assets.readAsset(report.entry);
  } catch (const std::exception &e) {
    report.error = e.what();
    return report;
  }

  if (rawBytes.size() >= 8 &&
      std::memcmp(rawBytes.data(), "TJS2100\0", 8) == 0) {
    const auto compiledStrings = extractTjs2CompiledStrings(rawBytes);
    for (const auto &str : compiledStrings) {
      if (!safeStorage(str))
        continue;
      if (std::find(report.staticallyReferenced.begin(),
                    report.staticallyReferenced.end(),
                    str) == report.staticallyReferenced.end()) {
        report.staticallyReferenced.push_back(str);
        if (!assets.contains(str))
          report.missing.push_back(str);
      }
    }
    return report;
  }

  std::string script;
  try {
    script = assets.loadTextUtf8(report.entry, "CP932");
  } catch (const std::exception &e) {
    report.error = e.what();
    return report;
  }
  const auto tokens = tokenize(script);
  for (std::size_t i = 0; i + 4 < tokens.size(); ++i) {
    if (tokens[i].kind != Token::Identifier || tokens[i + 1].text != "." ||
        tokens[i + 2].kind != Token::Identifier || tokens[i + 3].text != "(" ||
        !supportedCall(tokens[i].text, tokens[i + 2].text))
      continue;
    const auto &arg = tokens[i + 4];
    if (arg.kind != Token::Literal || !safeStorage(arg.text)) {
      ++report.dynamicCalls;
      continue;
    }
    if (std::find(report.staticallyReferenced.begin(),
                  report.staticallyReferenced.end(),
                  arg.text) != report.staticallyReferenced.end())
      continue;
    report.staticallyReferenced.push_back(arg.text);
    if (!assets.contains(arg.text))
      report.missing.push_back(arg.text);
  }
  return report;
}

} // namespace Kotonoha::Providers
