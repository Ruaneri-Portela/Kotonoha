#pragma once
#include <map>
#include <string>
#include <vector>
namespace Air {
class Config {
  std::map<std::string, std::vector<std::string>> values;

public:
  explicit Config(const std::string &text);
  const std::vector<std::string> &get(const std::string &key) const;
  int integer(const std::string &key, int fallback = 0, size_t item = 0) const;
  std::string text(const std::string &key, const std::string &fallback = "",
                   size_t item = 0) const;
  struct Resolution {
    int width = 640;
    int height = 480;
  };
  Resolution detectResolution() const;
};
std::string canonical(std::string);
std::string decodeText(const std::string &, int encoding);
} // namespace Air
