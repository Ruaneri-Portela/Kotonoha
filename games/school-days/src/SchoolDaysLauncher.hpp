#pragma once

#include <Kotonoha/core/GameLauncher.hpp>

namespace SchoolDays {

class Launcher final : public Kotonoha::GameLauncher {
public:
  static constexpr const char *RouteRememberFile = "route.txt";

  Launcher(std::string settingsPath, std::string directory,
           std::string route = {});
  const std::string &routePath() const;
  void setRoutePath(std::string path);
  bool routeValid() const;
  const std::string &routeInfo() const;

protected:
  bool checkPath(const std::string &path, std::string &info) override;

private:
  std::size_t routeOption = 0;
};

} // namespace SchoolDays
