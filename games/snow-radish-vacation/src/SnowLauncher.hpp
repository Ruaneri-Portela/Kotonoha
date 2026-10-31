#pragma once

#include <Kotonoha/core/GameLauncher.hpp>

#include <string>

namespace SnowRadish {

// The engine game launcher for the user's own retail DAT folder (SCRIPT /
// GRAPHIC / SOUND / VOICE.DAT, never copied), remembered in dir.txt next to
// the settings.
class SnowLauncher final : public Kotonoha::GameLauncher {
public:
  // `directory` overrides the remembered folder when not empty.
  SnowLauncher(std::string settingsPath, std::string directory);

protected:
  bool checkPath(const std::string &path, std::string &info) override;
};

} // namespace SnowRadish
