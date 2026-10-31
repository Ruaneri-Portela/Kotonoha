#pragma once

#include "Ddlc/Archive.hpp"
#include "Ddlc/Script.hpp"

#include <Kotonoha/core/AssetProvider.hpp>

#include <filesystem>
#include <system_error>

namespace Ddlc {

inline constexpr const char* kCharacterFiles[] = {"monika.chr", "natsuki.chr", "sayori.chr", "yuri.chr"};

// True when the installation's characters/ (sibling of game/) holds the four .chr files.
bool hasOriginalCharacters(const std::filesystem::path& gameDirectory, std::error_code& error);

// The writable, user-visible directory standing for Ren'Py's config.basedir:
// $DDLC_SAVE_DIR when set, otherwise the game's documents directory
// (SDL pref path on desktop, Documents on iOS, external files on Android).
std::filesystem::path userDirectory();
inline std::filesystem::path charactersDirectory(const std::filesystem::path& user) { return user / "characters"; }

// Creates <user>/characters with the original .chr files the first time (when
// the folder does not exist yet). Copies come from the installation's sibling
// characters/ folder or, failing that, from the archives (DDLC ships them in
// scripts.rpa). Later runs never touch the folder: the game and the player
// delete and restore files there. Returns true when the folder was created.
bool seedCharacters(const std::filesystem::path& user, const std::filesystem::path& gameDirectory,
                    const Assets* archives = nullptr);

class GameAssets final : public Kotonoha::AssetProvider {
public:
    explicit GameAssets(const std::filesystem::path& directory);

    std::string name() const override;
    bool contains(const std::string& id) const override;
    std::vector<Uint8> read(const std::string& id) const override;
    std::vector<std::string> list(const std::string& prefix = {}) const override;

    const Assets& source() const { return assets_; }
    const Script& script() const { return script_; }

private:
    Assets assets_;
    Script script_;
};

} // namespace Ddlc
