#pragma once

#include <string>
#include <vector>

namespace Kotonoha {
struct AssStyle {
  std::string header;
  std::vector<std::string> names;
  static AssStyle parse(const std::string &ass);
  static AssStyle load(const std::string &path);
};
} // namespace Kotonoha
