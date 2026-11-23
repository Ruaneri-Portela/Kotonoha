// Copyright 2026 Kotonoha contributors. SPDX-License-Identifier: MIT
#include <N2System/DebugCatalog.hpp>
#include <set>
#include <string>
#include <utility>

namespace N2System {
namespace {
bool isDialogue(std::string_view function) {
  return function == "SetDialogue" || function == "Dialogue" ||
         function == "Talk" || function == "Message";
}
bool isChoice(std::string_view function) {
  return function == "CreateChoice" || function == "SelectChoice" ||
         function == "Choice";
}

// Scan a single call's argument list. Keep the *true argument index* instead
// of the index of only quoted literals, so a variable speaker argument never
// promotes the subsequent voice filename into a translatable dialogue key.
template <typename Append>
void scanArguments(std::string_view source, std::size_t start, std::size_t stop,
                   Append append) {
  if (start >= source.size() || stop > source.size() || start >= stop)
    return;
  std::size_t pos = start;
  int depth = 1;
  std::size_t argument = 0;
  while (pos < stop && depth > 0) {
    const char ch = source[pos++];
    if (ch == '/' && pos < stop && source[pos] == '/') {
      while (pos < stop && source[pos] != '\n')
        ++pos;
      continue;
    }
    if (ch == '/' && pos < stop && source[pos] == '*') {
      ++pos;
      while (pos + 1 < stop && !(source[pos] == '*' && source[pos + 1] == '/'))
        ++pos;
      if (pos + 1 < stop)
        pos += 2;
      continue;
    }
    if (ch == '"' || ch == '\'') {
      std::string literal;
      bool closed = false;
      while (pos < stop) {
        char c = source[pos++];
        if (c == '\\' && pos < stop) {
          c = source[pos++];
          if (c == 'n')
            c = '\n';
          else if (c == 'r')
            c = '\r';
          else if (c == 't')
            c = '\t';
        } else if (c == ch) {
          closed = true;
          break;
        }
        literal.push_back(c);
      }
      if (closed && !literal.empty())
        append(argument, std::move(literal));
      continue;
    }
    if (ch == '(')
      ++depth;
    else if (ch == ')')
      --depth;
    else if (ch == ',' && depth == 1)
      ++argument;
  }
}
} // namespace

std::map<int, std::vector<std::string>>
ExtractTranslationTexts(const ScriptIndex &index, std::string_view source) {
  std::map<int, std::vector<std::string>> result;
  for (std::size_t route = 0; route < index.chapters().size(); ++route) {
    const auto &chapter = index.chapters()[route];
    auto &texts = result[static_cast<int>(route)];
    std::set<std::string> seen;
    for (const auto &invocation : chapter.invocations) {
      const bool dialogue = isDialogue(invocation.function);
      const bool choice = isChoice(invocation.function);
      if (!dialogue && !choice)
        continue;
      const auto opening = source.find('(', invocation.offset);
      if (opening == std::string_view::npos || opening >= chapter.end)
        continue;
      scanArguments(source, opening + 1, chapter.end,
                    [&](std::size_t arg, std::string literal) {
                      // Third dialogue argument is voice audio; first choice
                      // argument is the control/entity name. They are not
                      // visible text.
                      if ((dialogue && arg >= 2) || (choice && arg == 0))
                        return;
                      if (seen.insert(literal).second)
                        texts.push_back(std::move(literal));
                    });
    }
  }
  return result;
}
} // namespace N2System
