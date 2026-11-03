#pragma once
#include <Kotonoha/core/GameLauncher.hpp>
namespace Amagami {
class AmagamiLauncher final : public Kotonoha::GameLauncher {
public:
  AmagamiLauncher(std::string settingsPath, std::string disc);
  static bool validatePath(const std::string &, std::string &);

protected:
  bool checkPath(const std::string &path, std::string &info) override {
    return validatePath(path, info);
  }
};
} // namespace Amagami
