#pragma once

#include <Kotonoha/core/GameLauncher.hpp>

namespace Ddlc {

class GameLauncher final : public Kotonoha::GameLauncher {
public:
    GameLauncher(std::string settingsPath, std::string directory);

protected:
    bool checkPath(const std::string& path, std::string& info) override;
};

} // namespace Ddlc
