#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace Kotonoha {

// Packaged resources; Android unpacks APK assets into private storage.
std::filesystem::path resourceDirectory();
std::filesystem::path engineAssetDirectory();
std::filesystem::path gameAssetDirectory(const std::string &game);
// Data is read-only bundle resources on Apple, and Android/obb/<package>.
std::filesystem::path preferredDataDirectory(const std::string &game);
// Writable saves/settings/logs: Documents on iOS, Android/data/<package>/files
// on Android, SDL_GetPrefPath on desktop. Creates the directory or throws.
std::filesystem::path preferredDocumentsDirectory(const std::string &game);

// Single lookup policy for every packaged/user file. Roots, in order: bundle
// resources, preferred data (Android OBB), working directory, executable
// directory and the game's documents directory (only when game is not empty).
class FileResolver {
public:
  static std::vector<std::filesystem::path> roots(const std::string &game = {});
  // First existing <root>/relative; absolute paths are returned when present.
  static std::optional<std::filesystem::path> find(const std::filesystem::path &relative,
                                                   const std::string &game = {});
  // Like find() but throws, listing every searched location.
  static std::filesystem::path resolve(const std::filesystem::path &relative,
                                       const std::string &game = {});
  // assets/<relative> (engine) and game-assets/<relative> (game data; also
  // games/<game>/game-assets/<relative> for source-tree runs).
  static std::optional<std::filesystem::path> findEngineAsset(const std::filesystem::path &relative);
  static std::filesystem::path engineAsset(const std::filesystem::path &relative);
  static std::optional<std::filesystem::path> findGameAsset(const std::string &game,
                                                            const std::filesystem::path &relative);
  static std::filesystem::path gameAsset(const std::string &game,
                                         const std::filesystem::path &relative);
  // Engine fonts live only in assets/fonts.
  static std::filesystem::path fontDirectory();
  static std::filesystem::path font(const std::string &file = "NotoSansJP-Regular.ttf");
};

void writeTextFileAtomically(const std::string &path,
                             const std::string &contents);

} // namespace Kotonoha
