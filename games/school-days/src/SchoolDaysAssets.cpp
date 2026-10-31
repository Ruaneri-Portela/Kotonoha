#include "SchoolDaysAssets.hpp"

#include <Kotonoha/SchoolDaysAssetMount.hpp>
#include <Kotonoha/SchoolDaysImageAssetResolver.hpp>
#include <Kotonoha/core/FileSystem.hpp>
#include <stdexcept>

namespace SchoolDays {
namespace {
std::filesystem::path ScriptRoot(const std::filesystem::path &root) {
  std::error_code ec;
  for (const auto &entry : std::filesystem::directory_iterator(root, ec)) {
    if (!Kotonoha::SchoolDaysAsciiCaseEqual(entry.path().filename().string(), "Script") ||
        !entry.is_directory(ec)) continue;
    for (const auto &language : std::filesystem::directory_iterator(entry.path(), ec))
      if (Kotonoha::SchoolDaysAsciiCaseEqual(language.path().filename().string(), "English") &&
          language.is_directory(ec)) return language.path();
  }
  return {};
}
}

Assets::Assets(std::filesystem::path root, const std::string &scripts)
    : directory(std::move(root)) {
  std::string info;
  if (!checkDirectory(directory, info)) throw std::runtime_error(info);
  if (ScriptRoot(directory).empty()) {
    if (!Kotonoha::MountSchoolDaysRetailGpks(directory, &info))
      throw std::runtime_error(info);
    mounted = true;
    Kotonoha::InstallMountedSchoolDaysAssetResolver();
  }
  if (!scripts.empty() && !Kotonoha::SetSchoolDaysScriptDirectory(scripts, &info)) {
    if (mounted) {
      Kotonoha::UninstallMountedSchoolDaysAssetResolver();
      Kotonoha::UnmountSchoolDaysRetailGpks();
    }
    throw std::runtime_error(info);
  }
}

std::filesystem::path Assets::orsRoot() const {
  const auto scripts = ScriptRoot(directory);
  return scripts.empty() ? directory : scripts;
}

Assets::~Assets() {
  clearCache();
  if (mounted) {
    Kotonoha::UninstallMountedSchoolDaysAssetResolver();
    Kotonoha::UnmountSchoolDaysRetailGpks();
  }
  Kotonoha::SetSchoolDaysScriptDirectory({});
}

std::string Assets::resolve(const std::string &logical, bool image) const {
  if (Kotonoha::IsSchoolDaysGpkUri(logical)) return logical;
  const auto found = image
      ? Kotonoha::ResolveSchoolDaysImageAsset(directory.string().c_str(), logical)
      : Kotonoha::ResolveSchoolDaysAssetPath(directory.string().c_str(), logical);
  if (!found.present || found.collision)
    throw std::runtime_error("Missing or ambiguous School Days asset: " + logical);
  return found.physical;
}

bool Assets::contains(const std::string &id) const {
  if (Kotonoha::IsSchoolDaysGpkUri(id)) {
    std::string logical;
    return Kotonoha::SchoolDaysLogicalFromGpkUri(id, &logical) &&
           Kotonoha::LookupMountedSchoolDaysAsset(logical) ==
               Kotonoha::SchoolDaysGpkLookupStatus::Present;
  }
  std::error_code ec;
  return std::filesystem::is_regular_file(id, ec);
}

std::vector<Uint8> Assets::read(const std::string &id) const {
  std::vector<Uint8> bytes;
  std::string error;
  if (!Kotonoha::ReadSchoolDaysAssetRef(id, &bytes, &error))
    throw std::runtime_error(error + ": " + id);
  return bytes;
}

std::string Assets::localPath(const std::string &id) const {
  return Kotonoha::IsSchoolDaysGpkUri(id) ? std::string() : id;
}

bool Assets::checkDirectory(const std::filesystem::path &root, std::string &info) {
  std::error_code ec;
  if (!std::filesystem::is_directory(root, ec)) {
    info = "School Days directory not found: " + root.string();
    return false;
  }
  if (!ScriptRoot(root).empty()) {
    info = "Extracted School Days Script/English directory found";
    return true;
  }
  for (const auto &candidate : {root, root / "Packs"}) {
    if (Kotonoha::ResolveSchoolDaysAssetPath(candidate.string().c_str(), "BGM.GPK").present) {
      for (const auto &spec : Kotonoha::SchoolDaysRetailGpkSpecs())
        if (!Kotonoha::ResolveSchoolDaysAssetPath(candidate.string().c_str(), spec.filename).present) {
          info = std::string("Incomplete retail pack set: missing ") + spec.filename;
          return false;
        }
      info = "School Days HQ retail 29-pack set found (validated on launch)";
      return true;
    }
  }
  info = "Select the game directory with Script/English or the complete retail Packs directory";
  return false;
}

std::filesystem::path Assets::defaultDirectory() {
  std::string info;
  // FileResolver covers the bundle, OBB data, working/executable directories,
  // documents and the source tree's games/school-days/game-assets.
  if (const auto found = Kotonoha::FileResolver::findGameAsset("school-days", {}))
    if (checkDirectory(*found, info)) return *found;
  for (const auto &root : Kotonoha::FileResolver::roots("school-days"))
    for (const auto &candidate : {root / "game-assets", root / "school-days", root})
      if (checkDirectory(candidate, info)) return candidate;
  return Kotonoha::preferredDataDirectory("school-days");
}

std::filesystem::path Assets::findDefaultRoute() {
  const auto route = Kotonoha::FileResolver::findGameAsset("school-days", "school-days-hq.ktnroute");
  return route ? *route : std::filesystem::path();
}

std::filesystem::path Assets::defaultRoute() {
  const auto route = findDefaultRoute();
  if (!route.empty()) return route;
  throw std::runtime_error("school-days-hq.ktnroute not found; supply --ktrf");
}

} // namespace SchoolDays
