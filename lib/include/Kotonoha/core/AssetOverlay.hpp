#pragma once
#include <filesystem>
#include <map>
#include <string>
#include <vector>
namespace Kotonoha {
// Indexed once when mounted. Asset IDs are relative, slash-normalized and
// ASCII case-insensitive, matching the archives used by the ports.
class AssetOverlay {
  std::filesystem::path root_;
  std::map<std::string, std::filesystem::path> files_;

public:
  explicit AssetOverlay(const std::filesystem::path &root = {},
                        bool discoverOverlay = true);
  static std::string key(std::string id);
  const std::filesystem::path &root() const { return root_; }
  std::string resolve(const std::string &id) const;
  std::vector<std::string> list(const std::string &prefix = {}) const;
};
} // namespace Kotonoha
