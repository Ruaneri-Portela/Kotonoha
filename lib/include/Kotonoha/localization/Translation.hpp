#pragma once

#include <map>
#include <string>
#include <unordered_map>
#include <vector>

namespace Kotonoha {

class Translation {
  using Dictionary = std::unordered_map<std::string, std::string>;
  std::unordered_map<std::string, Dictionary> sections;

public:
  using SourceTexts = std::map<int, std::vector<std::string>>;
  void load(const std::string &path);
  std::string text(int nodeId, const std::string &original) const;
  void dump(const std::string &path, const SourceTexts &texts) const;
};

} // namespace Kotonoha
