#pragma once
// RPA/DDLC asset adapter; game presentation stays in the port.

#include "Ddlc/Archive.hpp"
#include "Ddlc/Script.hpp"

#include <Kotonoha/core/AssetProvider.hpp>

#include <filesystem>
#include <system_error>

namespace Ddlc {

inline constexpr const char *kCharacterFiles[] = {"monika.chr", "natsuki.chr",
                                                  "sayori.chr", "yuri.chr"};

// True when the installation's characters/ (sibling of game/) holds the four
// .chr files.
bool hasOriginalCharacters(const std::filesystem::path &gameDirectory,
                           std::error_code &error);

// The writable, user-visible directory standing for Ren'Py's config.basedir:
// $DDLC_SAVE_DIR when set, otherwise the game's documents directory
// (SDL pref path on desktop, Documents on iOS, external files on Android).
std::filesystem::path userDirectory();
inline std::filesystem::path
charactersDirectory(const std::filesystem::path &user) {
  return user / "characters";
}

// Creates <user>/characters with the original .chr files the first time (when
// the folder does not exist yet). Copies come from the installation's sibling
// characters/ folder or, failing that, from the archives (DDLC ships them in
// scripts.rpa). Later runs never touch the folder: the game and the player
// delete and restore files there. Returns true when the folder was created.
bool seedCharacters(const std::filesystem::path &user,
                    const std::filesystem::path &gameDirectory,
                    const Ddlc::Assets *archives = nullptr);

} // namespace Ddlc

namespace Kotonoha::Providers {

// Ren'Py RPA archive + loose-file provider used by DDLC.
class DdlcAssets final : public Kotonoha::AssetProvider {
public:
  explicit DdlcAssets(const std::filesystem::path &directory,
                      const std::filesystem::path &overlayDirectory = {});
  explicit DdlcAssets(
      std::shared_ptr<Kotonoha::Filesystem::Filesystem> source,
      const std::filesystem::path &overlayDirectory = {});

  std::string name() const override;
  bool containsAsset(const std::string &id) const override;
  std::vector<Uint8> readAsset(const std::string &id) const override;
  std::vector<std::string>
  listAsset(const std::string &prefix = {}) const override;

  const Ddlc::Assets &source() const { return assets_; }
  const Ddlc::Script &script() const { return script_; }

private:
  void configureOverlaysAndScripts(
      const std::filesystem::path &overlayDirectory,
      bool inspectCharacterPaths);
  Ddlc::Assets assets_;
  Ddlc::Script script_;
};

} // namespace Kotonoha::Providers
