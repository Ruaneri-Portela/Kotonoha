#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace Kotonoha {

enum class StoragePlatform { Desktop, Android, IOS };

// Platform locations, before applying Kotonoha's directory layout. Keeping
// these separate from lookup makes the mobile policies testable on desktop.
struct StorageLocations {
  StoragePlatform platform = StoragePlatform::Desktop;
  std::string packageIdentifier;
  std::filesystem::path resources;
  std::filesystem::path userData;
  std::filesystem::path privateGameAssets;
  std::filesystem::path obb;
  std::filesystem::path workingDirectory;
};

struct DirectoryLayout {
  std::vector<std::filesystem::path> engineAssets;
  std::vector<std::filesystem::path> gameAssets;
  std::vector<std::filesystem::path> references;
  std::filesystem::path saves;
};

// Ordered read roots and a single write root. Does not create directories.
DirectoryLayout directoryLayout(const StorageLocations &locations,
                                const std::string &game = {});
DirectoryLayout applicationDirectories(const std::string &game = {});
std::vector<std::filesystem::path>
referenceCandidates(const DirectoryLayout &layout,
                    const std::filesystem::path &reference);

// Bundle Resources on macOS, the app bundle on iOS, files/ on Android,
// and the executable directory on Windows/Linux.
std::filesystem::path resourceDirectory();
std::filesystem::path engineAssetDirectory();
std::filesystem::path gameAssetDirectory(const std::string &game = {});
// External game data: Android OBB, iOS Documents, desktop resources.
std::filesystem::path preferredDataDirectory();
std::filesystem::path preferredDataDirectory(const std::string &game);
std::string preferredGameIdentifier();
std::filesystem::path findAssetOverlayDirectory(const std::filesystem::path &root = {});

// <platform user-data root>/Kotonoha/<package id>. The game slug/display name
// is never used as the save identity. Creates the directory or throws.
std::filesystem::path saveDirectory();
// Compatibility API for settings and existing ports. A nonempty argument
// is an explicit package id, or a game slug when app metadata is available.
std::filesystem::path preferredDocumentsDirectory();
std::filesystem::path preferredDocumentsDirectory(const std::string &game);

// Generic named-resource lookup uses the shared platform search locations.
// Engine/game mounts use their dedicated roots instead.
class FileResolver {
public:
  // Portable launcher/settings reference; independent of a device's absolute
  // path. Existing absolute selections are accepted for the current session.
  static std::string referenceName(const std::filesystem::path &path);
  static std::vector<std::filesystem::path>
  candidates(const std::filesystem::path &reference, const std::string &game = {});
  static std::optional<std::filesystem::path>
  findAsset(const std::filesystem::path &reference, const std::string &game = {});
  static std::vector<std::filesystem::path> roots(const std::string &game = {});
  // First existing <root>/relative; absolute paths are returned when present.
  static std::optional<std::filesystem::path>
  find(const std::filesystem::path &relative, const std::string &game = {});
  // Like find() but throws, listing every searched location.
  static std::filesystem::path resolve(const std::filesystem::path &relative,
                                       const std::string &game = {});
  // Engine: assets/<relative>. Game: platform game-asset roots; game_assets
  // and the existing game-assets package/source-tree spelling are supported.
  static std::optional<std::filesystem::path>
  findEngineAsset(const std::filesystem::path &relative);
  static std::filesystem::path
  engineAsset(const std::filesystem::path &relative);
  static std::optional<std::filesystem::path>
  findGameAsset(const std::string &game = {}, const std::filesystem::path &relative = {});
  static std::filesystem::path gameAsset(const std::string &game = {},
                                         const std::filesystem::path &relative = {});
  // Engine fonts live only in assets/fonts.
  static bool isFontFile(const std::filesystem::path &path);
  static std::filesystem::path fontDirectory();
  static std::filesystem::path
  font(const std::string &file = "NotoSansJP-Regular.ttf");
  static std::vector<uint8_t> loadFontMemory(const std::string &file = {});
  static const std::unordered_map<std::string, std::vector<uint8_t>> &
  getFontMap();
  static const std::vector<uint8_t> *getFontBytes(const std::string &name);
};

void writeTextFileAtomically(const std::string &path,
                             const std::string &contents);

} // namespace Kotonoha
