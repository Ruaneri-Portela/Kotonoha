#pragma once

#include <Kotonoha/core/GameLauncher.hpp>

#include <string>

namespace ZeroNoTsukaima {

// The engine game launcher for the user's own Zero no Tsukaima disc image
// (.iso/.bin, never copied), remembered in disc.txt next to the settings.
class ZeroLauncher final : public Kotonoha::GameLauncher {
public:
  // `disc` overrides the remembered path when not empty.
  ZeroLauncher(std::string settingsPath, std::string disc);

protected:
  bool checkPath(const std::string &path, std::string &info) override;
};

} // namespace ZeroNoTsukaima
