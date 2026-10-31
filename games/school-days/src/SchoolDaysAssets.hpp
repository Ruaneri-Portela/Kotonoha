#pragma once

#include <Kotonoha/core/AssetProvider.hpp>
#include <filesystem>

namespace SchoolDays {

class Assets final : public Kotonoha::AssetProvider {
  std::filesystem::path directory;
  bool mounted = false;

public:
  explicit Assets(std::filesystem::path root, const std::string &scripts = {});
  ~Assets() override;
  const std::filesystem::path &root() const { return directory; }
  std::filesystem::path orsRoot() const;
  std::string name() const override { return "School Days HQ"; }
  std::string resolve(const std::string &logical, bool image = false) const;
  bool contains(const std::string &id) const override;
  std::vector<Uint8> read(const std::string &id) const override;
  std::string localPath(const std::string &id) const override;
  static bool checkDirectory(const std::filesystem::path &root, std::string &info);
  static std::filesystem::path defaultDirectory();
  static std::filesystem::path findDefaultRoute();
  static std::filesystem::path defaultRoute();
};

} // namespace SchoolDays
