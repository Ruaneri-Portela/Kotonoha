// Copyright 2026 Kotonoha contributors. SPDX-License-Identifier: MIT
#include <Kirikiri/TjsBootstrap.hpp>
#include <Kirikiri/KirikiriAssets.hpp>

#include <algorithm>
#include <cctype>
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
        while (i < size && text[i] != '\n' && text[i] != '\r') ++i;
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
      while (i < size &&
             identifierChar(static_cast<unsigned char>(text[i])))
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
    if (ch == '\\') ch = '/';
    else if (ch >= 'A' && ch <= 'Z') ch = static_cast<char>(ch - 'A' + 'a');
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
    if (normalized.substr(i, next == std::string::npos
                                ? next : next - i) == "..")
      return false;
    if (next == std::string::npos) break;
    i = next + 1;
  }
  return true;
}

} // namespace

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

  std::string script;
  try {
    script = assets.loadTextUtf8(report.entry, "CP932");
  } catch (const std::exception &e) {
    report.error = e.what();
    return report;
  }
  const auto tokens = tokenize(script);
  for (std::size_t i = 0; i + 4 < tokens.size(); ++i) {
    if (tokens[i].kind != Token::Identifier ||
        tokens[i + 1].text != "." ||
        tokens[i + 2].kind != Token::Identifier ||
        tokens[i + 3].text != "(" ||
        !supportedCall(tokens[i].text, tokens[i + 2].text))
      continue;
    const auto &arg = tokens[i + 4];
    if (arg.kind != Token::Literal || !safeStorage(arg.text)) {
      ++report.dynamicCalls;
      continue;
    }
    if (std::find(report.staticallyReferenced.begin(),
                  report.staticallyReferenced.end(), arg.text) !=
        report.staticallyReferenced.end())
      continue;
    report.staticallyReferenced.push_back(arg.text);
    if (!assets.contains(arg.text))
      report.missing.push_back(arg.text);
  }
  return report;
}

} // namespace Kotonoha::Providers
