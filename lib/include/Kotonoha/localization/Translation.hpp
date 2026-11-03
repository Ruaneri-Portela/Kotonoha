#pragma once

#include <map>
#include <set>
#include <string>
#include <unordered_map>
#include <vector>

namespace Kotonoha {

class Translation {
  using Dictionary = std::unordered_map<std::string, std::string>;
  std::unordered_map<std::string, Dictionary> sections;
  mutable std::set<std::string> globalSources;

public:
  using SourceTexts = std::map<int, std::vector<std::string>>;
  void load(const std::string &path);
  std::string text(int nodeId, const std::string &original) const;
  void dump(const std::string &path, const SourceTexts &texts) const;

  std::unordered_map<std::string, Dictionary> &mutableSections() {
    return sections;
  }
  const std::unordered_map<std::string, Dictionary> &getSections() const {
    return sections;
  }
};

} // namespace Kotonoha
