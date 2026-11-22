// Copyright 2026 Kotonoha contributors. SPDX-License-Identifier: MIT
#pragma once

// C++17 equivalent of NitroSharp.NsScript.EntityQuery / EntityPattern.
// Deliberately keep parser rules compatible with the reference: "*" matches
// zero or more characters inside ONE entity-name component, and '?' is literal.
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace N2System {

enum class EntityQueryScope {
  Current,
  MainThread,
  CurrentAliases,
  AllAliases
};

struct EntityQueryPart {
  std::string pattern;
  EntityQueryScope scope = EntityQueryScope::Current;
  bool forceEnd = false;

  bool hasWildcard() const { return pattern.find('*') != std::string::npos; }
};

class EntityQuery {
public:
  static std::optional<EntityQuery> tryParse(std::string_view query) {
    if (query.empty())
      return std::nullopt;
    EntityQuery parsed;
    std::optional<EntityQueryScope> previous;
    bool forceEnd = false;
    std::size_t start = 0;
    while (start < query.size()) {
      if (forceEnd)
        return std::nullopt;
      const auto slash = query.find('/', start);
      const auto end = slash == std::string_view::npos ? query.size() : slash;
      auto component = query.substr(start, end - start);
      if (component.empty())
        return std::nullopt;
      EntityQueryPart part;
      if (component.size() >= 2 && component.substr(0, 2) == "<@") {
        part.scope = EntityQueryScope::AllAliases;
        component.remove_prefix(2);
      } else if (component.front() == '<') {
        part.scope = EntityQueryScope::MainThread;
        component.remove_prefix(1);
      } else if (component.front() == '@') {
        part.scope = EntityQueryScope::CurrentAliases;
        component.remove_prefix(1);
      }
      if (!component.empty() && component.back() == '>') {
        part.forceEnd = true;
        component.remove_suffix(1);
      }
      if (component.empty() ||
          component.find_first_of("<>@/") != std::string_view::npos)
        return std::nullopt;
      if (previous && part.scope == EntityQueryScope::MainThread)
        return std::nullopt;
      if (previous == EntityQueryScope::MainThread &&
          part.scope != EntityQueryScope::Current)
        return std::nullopt;
      part.pattern = std::string(component);
      parsed.parts_.push_back(std::move(part));
      previous = parsed.parts_.back().scope;
      forceEnd = parsed.parts_.back().forceEnd;
      if (slash == std::string_view::npos)
        break;
      start = slash + 1;
    }
    if (query.back() == '/' || parsed.parts_.empty())
      return std::nullopt;
    return parsed;
  }

  const std::vector<EntityQueryPart> &parts() const { return parts_; }

  static bool matchPattern(std::string_view text, std::string_view pattern) {
    std::size_t ti = 0, pi = 0;
    std::size_t star = std::string_view::npos, retry = 0;
    while (ti < text.size()) {
      if (pi < pattern.size() && pattern[pi] == text[ti]) {
        ++ti;
        ++pi;
      } else if (pi < pattern.size() && pattern[pi] == '*') {
        star = pi++;
        retry = ti;
      } else if (star != std::string_view::npos) {
        pi = star + 1;
        ti = ++retry;
      } else {
        return false;
      }
    }
    while (pi < pattern.size() && pattern[pi] == '*')
      ++pi;
    return pi == pattern.size();
  }

private:
  std::vector<EntityQueryPart> parts_;
};

// NitroSharp.EntityPath is the non-wildcard subset of EntityQuery. The
// native VM uses paths for direct property access, whereas query expressions
// are used for multi-target effects and actions.
struct EntityPathPart {
  std::string value;
  bool isAlias = false;
};

class EntityPath {
public:
  static std::optional<EntityPath> tryParse(std::string_view path) {
    const auto query = EntityQuery::tryParse(path);
    if (!query)
      return std::nullopt;
    EntityPath result;
    for (const auto &part : query->parts()) {
      if (part.hasWildcard() ||
          (part.scope != EntityQueryScope::Current &&
           part.scope != EntityQueryScope::CurrentAliases))
        return std::nullopt;
      result.parts_.push_back(
          {part.pattern, part.scope == EntityQueryScope::CurrentAliases});
    }
    return result;
  }

  const std::vector<EntityPathPart> &parts() const { return parts_; }

private:
  std::vector<EntityPathPart> parts_;
};

} // namespace N2System
