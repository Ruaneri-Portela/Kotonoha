#include <Kotonoha/core/Config.hpp>
#include <Kotonoha/core/FileSystem.hpp>

#include <SDL3/SDL.h>
#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <stdexcept>
#ifdef __APPLE__
#include <TargetConditionals.h>
#endif

#ifdef __ANDROID__
#include <jni.h>
#endif

namespace Kotonoha {
namespace {
void validateGame(const std::string &game) {
  if (game.empty() || game == "." || game == ".." ||
      game.find_first_of("/\\:") != std::string::npos ||
      game.find('\0') != std::string::npos)
    throw std::invalid_argument("Invalid game identifier");
}

std::filesystem::path normalizeDirectory(std::filesystem::path path) {
  if (path.empty())
    return {};
  path = path.lexically_normal();
  if (path.filename().empty())
    path = path.parent_path();
  return path;
}

void addDirectory(std::vector<std::filesystem::path> &paths,
                  const std::filesystem::path &path) {
  auto normalized = normalizeDirectory(path);
  if (!normalized.empty() &&
      std::find(paths.begin(), paths.end(), normalized) == paths.end())
    paths.push_back(std::move(normalized));
}

std::optional<std::filesystem::path>
findInDirectories(const std::vector<std::filesystem::path> &directories,
                  const std::filesystem::path &relative) {
  std::error_code error;
  for (const auto &directory : directories) {
    auto candidate = directory / relative;
    if (relative.empty() ? std::filesystem::is_directory(candidate, error)
                         : std::filesystem::exists(candidate, error))
      return normalizeDirectory(candidate);
  }
  return std::nullopt;
}

#ifdef __ANDROID__
std::filesystem::path androidDirectory(const char *method) {
  auto *env = static_cast<JNIEnv *>(SDL_GetAndroidJNIEnv());
  auto activity = static_cast<jobject>(SDL_GetAndroidActivity());
  if (!env || !activity)
    throw std::runtime_error("Android storage requires an SDL activity");

  jclass type = env->GetObjectClass(activity);
  jmethodID id =
      type ? env->GetMethodID(type, method, "()Ljava/lang/String;") : nullptr;
  auto value =
      id ? static_cast<jstring>(env->CallObjectMethod(activity, id)) : nullptr;
  if (env->ExceptionCheck() || !value) {
    if (env->ExceptionCheck()) {
      env->ExceptionDescribe();
      env->ExceptionClear();
    }
    if (value)
      env->DeleteLocalRef(value);
    if (type)
      env->DeleteLocalRef(type);
    env->DeleteLocalRef(activity);
    throw std::runtime_error(std::string("Cannot locate Android directory: ") +
                             method);
  }

  const char *text = env->GetStringUTFChars(value, nullptr);
  if (!text) {
    env->DeleteLocalRef(value);
    env->DeleteLocalRef(type);
    env->DeleteLocalRef(activity);
    throw std::runtime_error("Cannot read Android storage path");
  }

  std::filesystem::path result(text);
  env->ReleaseStringUTFChars(value, text);
  env->DeleteLocalRef(value);
  env->DeleteLocalRef(type);
  env->DeleteLocalRef(activity);
  if (result.empty())
    throw std::runtime_error(std::string("Empty Android directory: ") + method);
  return result;
}
#endif
} // namespace

std::filesystem::path resourceDirectory() {
#ifdef __ANDROID__
  return androidDirectory("getKotonohaResourceDirectory");
#else
  const char *base = SDL_GetBasePath();
  if (!base)
    throw std::runtime_error(std::string("Cannot locate executable: ") +
                             SDL_GetError());
  std::filesystem::path directory(base);
  if (directory.filename().empty())
    directory = directory.parent_path();
#ifdef __APPLE__
  if (directory.filename() == "MacOS" &&
      directory.parent_path().filename() == "Contents")
    return directory.parent_path() / "Resources";
#endif
  return directory;
#endif
}

std::filesystem::path engineAssetDirectory() {
  const auto directories = applicationDirectories().engineAssets;
  if (auto found = findInDirectories(directories, {}))
    return *found;
  return directories.front();
}

std::filesystem::path gameAssetDirectory(const std::string &game) {
  const auto directories = applicationDirectories(game).gameAssets;
  if (auto found = findInDirectories(directories, {}))
    return *found;
  return directories.front();
}

std::string preferredGameIdentifier() {
#ifdef __ANDROID__
  return androidDirectory("getKotonohaPackageIdentifier").string();
#else
  if (const char *meta =
          SDL_GetAppMetadataProperty(SDL_PROP_APP_METADATA_IDENTIFIER_STRING);
      meta && *meta)
    return meta;
  return {};
#endif
}

DirectoryLayout directoryLayout(const StorageLocations &locations,
                                const std::string &game) {
  if (locations.resources.empty())
    throw std::invalid_argument("A resource directory is required");
  if (!game.empty())
    validateGame(game);
  DirectoryLayout result;
  const auto addGame = [&](const std::filesystem::path &root, bool sourceTree) {
    if (root.empty())
      return;
    addDirectory(result.gameAssets, root / "game-assets");
    addDirectory(result.gameAssets, root / "game_assets");
    if (sourceTree && !game.empty()) {
      addDirectory(result.gameAssets, root / "games" / game / "game-assets");
      addDirectory(result.gameAssets, root / "games" / game / "game_assets");
      addDirectory(result.gameAssets, root / game / "game-assets");
    }
  };
  addDirectory(result.engineAssets, locations.resources / "assets");
  if (locations.platform == StoragePlatform::Android) {
    if (!locations.obb.empty()) {
      addGame(locations.obb, false);
      addDirectory(result.gameAssets, locations.obb);
    }
    addDirectory(result.gameAssets, locations.privateGameAssets);
    addGame(locations.resources, false);
  } else if (locations.platform == StoragePlatform::IOS) {
    addGame(locations.resources, false);
    if (!locations.userData.empty()) {
      addDirectory(result.engineAssets, locations.userData / "assets");
      addGame(locations.userData, true);
      addDirectory(result.gameAssets, locations.userData);
    }
  } else {
    addGame(locations.resources, true);
    if (!locations.workingDirectory.empty()) {
      addDirectory(result.engineAssets, locations.workingDirectory / "assets");
      addGame(locations.workingDirectory, true);
    }
  }
  if (!locations.packageIdentifier.empty()) {
    validateGame(locations.packageIdentifier);
    if (!locations.userData.empty())
      result.saves =
          locations.userData / "Kotonoha" / locations.packageIdentifier;
  }
  // Named selections (overlays, game folders/discs, scripts) share this
  // policy. Merely looking them up never creates a writable save directory.
  addDirectory(result.references, locations.obb);
  if (!locations.privateGameAssets.empty())
    addDirectory(result.references, locations.privateGameAssets.parent_path());
  addDirectory(result.references, locations.resources);
  addDirectory(result.references, locations.userData);
  for (const auto &path : result.gameAssets)
    addDirectory(result.references, path);
  for (const auto &path : result.engineAssets) {
    addDirectory(result.references, path);
    addDirectory(result.references, path / "fonts");
    addDirectory(result.references, path / "Anime4K");
    addDirectory(result.references, path / "ArtCNN");
  }
  addDirectory(result.references, result.saves);
  if (locations.platform == StoragePlatform::Desktop)
    addDirectory(result.references, locations.workingDirectory);
  return result;
}

std::string FileResolver::referenceName(const std::filesystem::path &path) {
  std::string portable = path.string();
  std::replace(portable.begin(), portable.end(), '\\', '/');
  auto name = normalizeDirectory(portable).filename().string();
  if (!name.empty())
    validateGame(name);
  return name;
}

std::vector<std::filesystem::path>
referenceCandidates(const DirectoryLayout &layout,
                    const std::filesystem::path &reference) {
  std::vector<std::filesystem::path> result;
  if (reference.empty())
    return result;
  if (reference.is_absolute())
    addDirectory(result, reference);
  // A migrated absolute reference can be found on another device by name.
  const auto name = FileResolver::referenceName(reference);
  for (const auto &root : layout.references) {
    if (!reference.is_absolute() && !reference.has_root_name())
      addDirectory(result, root / reference);
    if (root.filename() == name)
      addDirectory(result, root);
    addDirectory(result, root / name);
  }
  return result;
}

std::vector<std::filesystem::path>
FileResolver::candidates(const std::filesystem::path &reference,
                         const std::string &game) {
  return referenceCandidates(applicationDirectories(game), reference);
}

std::optional<std::filesystem::path>
FileResolver::findAsset(const std::filesystem::path &reference,
                        const std::string &game) {
  if (reference.empty())
    return std::nullopt;
  if (reference.is_absolute())
    if (auto found = find(reference))
      return found;
  auto first = reference.begin();
  if (*first == "assets" || *first == "game-assets" ||
      *first == "game_assets") {
    const bool engine = *first == "assets";
    std::filesystem::path relative;
    for (++first; first != reference.end(); ++first)
      relative /= *first;
    return engine ? findEngineAsset(relative) : findGameAsset(game, relative);
  }
  if (auto found = findGameAsset(game, reference))
    return found;
  if (auto found = findEngineAsset(reference))
    return found;
  std::error_code error;
  for (const auto &candidate : candidates(reference, game))
    if (std::filesystem::is_regular_file(candidate, error))
      return candidate;
  return std::nullopt;
}

static StorageLocations storageLocations() {
  StorageLocations locations;
  locations.resources = resourceDirectory();
  locations.packageIdentifier = preferredGameIdentifier();
#ifdef __ANDROID__
  locations.platform = StoragePlatform::Android;
  try {
    locations.userData = androidDirectory("getKotonohaDocumentsDirectory");
  } catch (const std::exception &) {
    // Read-only resource lookup must also work when external storage is absent.
  }
  locations.privateGameAssets =
      androidDirectory("getKotonohaGameAssetDirectory");
  try {
    locations.obb = androidDirectory("getKotonohaDataDirectory");
  } catch (const std::exception &) {
  }
#elif defined(TARGET_OS_IPHONE) && TARGET_OS_IPHONE
  locations.platform = StoragePlatform::IOS;
  if (const char *documents = SDL_GetUserFolder(SDL_FOLDER_DOCUMENTS))
    locations.userData = documents;
#else
  // Ask SDL for the organization root, not for the game name. This also
  // preserves Kotonoha on platforms that ignore SDL_GetPrefPath's org field.
  if (char *path = SDL_GetPrefPath(nullptr, "Kotonoha")) {
    locations.userData = normalizeDirectory(path).parent_path();
    SDL_free(path);
  }
  std::error_code error;
  locations.workingDirectory = std::filesystem::current_path(error);
#ifdef __APPLE__
  if (locations.resources.filename() == "Resources" &&
      locations.resources.parent_path().filename() == "Contents")
    locations.workingDirectory.clear();
#endif
#endif
  return locations;
}

DirectoryLayout applicationDirectories(const std::string &game) {
  return directoryLayout(storageLocations(), game);
}

std::filesystem::path preferredDataDirectory() {
  return preferredDataDirectory(preferredGameIdentifier());
}

std::filesystem::path preferredDataDirectory(const std::string &game) {
  (void)game;
#ifdef __ANDROID__
  return androidDirectory("getKotonohaDataDirectory");
#elif defined(TARGET_OS_IPHONE) && TARGET_OS_IPHONE
  if (const char *documents = SDL_GetUserFolder(SDL_FOLDER_DOCUMENTS))
    return normalizeDirectory(documents);
  throw std::runtime_error("Cannot locate iOS Documents");
#else
  return resourceDirectory();
#endif
}

std::filesystem::path
findAssetOverlayDirectory(const std::filesystem::path &root) {
  auto reference = root;
  if (reference.empty()) {
    const char *env = SDL_getenv("KOTONOHA_OVERLAY");
    reference = env && *env ? env : "overlay";
  }
  std::error_code error;
  for (const auto &candidate : FileResolver::candidates(reference)) {
    if (!candidate.empty() && std::filesystem::is_directory(candidate, error)) {
      return candidate;
    }
  }

  return {};
}

std::filesystem::path preferredDocumentsDirectory() { return saveDirectory(); }

std::filesystem::path preferredDocumentsDirectory(const std::string &game) {
  if (!game.empty())
    validateGame(game);
  auto locations = storageLocations();
  // Old callers may pass a source-tree slug. Only metadata or an explicit
  // reverse-domain identifier is allowed to choose the writable directory.
#ifndef __ANDROID__
  if (game.find('.') != std::string::npos)
    locations.packageIdentifier = game;
#endif
  auto directory = directoryLayout(locations).saves;
  if (directory.empty())
    throw std::runtime_error(
        "Set the application package identifier before accessing saves");
  std::filesystem::create_directories(directory);
  return normalizeDirectory(directory);
}

std::filesystem::path saveDirectory() {
  return preferredDocumentsDirectory(std::string{});
}

std::vector<std::filesystem::path>
FileResolver::roots(const std::string &game) {
  return applicationDirectories(game).references;
}

std::optional<std::filesystem::path>
FileResolver::find(const std::filesystem::path &relative,
                   const std::string &game) {
  std::error_code error;
  if (relative.empty())
    return std::nullopt;
  if (relative.is_absolute())
    return std::filesystem::exists(relative, error) ? std::optional(relative)
                                                    : std::nullopt;
  for (const auto &root : roots(game)) {
    auto candidate = root / relative;
    if (std::filesystem::exists(candidate, error))
      return candidate;
  }
  return std::nullopt;
}

std::filesystem::path
FileResolver::resolve(const std::filesystem::path &relative,
                      const std::string &game) {
  if (auto found = find(relative, game))
    return *found;
  std::string searched;
  for (const auto &root : relative.is_absolute()
                              ? std::vector<std::filesystem::path>{}
                              : roots(game))
    searched += "\n  " + (root / relative).string();
  throw std::runtime_error("File not found: " + relative.string() +
                           (searched.empty() ? "" : "; searched:" + searched));
}

std::optional<std::filesystem::path>
FileResolver::findEngineAsset(const std::filesystem::path &relative) {
  if (relative.is_absolute())
    return find(relative);
  return findInDirectories(applicationDirectories().engineAssets, relative);
}

std::filesystem::path
FileResolver::engineAsset(const std::filesystem::path &relative) {
  if (auto found = findEngineAsset(relative))
    return *found;
  std::string searched;
  for (const auto &directory : applicationDirectories().engineAssets)
    searched += "\n  " + (directory / relative).string();
  throw std::runtime_error("Engine asset not found: " + relative.string() +
                           (searched.empty() ? "" : "; searched:" + searched));
}

std::optional<std::filesystem::path>
FileResolver::findGameAsset(const std::string &game,
                            const std::filesystem::path &relative) {
  if (relative.is_absolute())
    return find(relative);
  if (auto found =
          findInDirectories(applicationDirectories(game).gameAssets, relative))
    return found;
  std::error_code error;
  for (const auto &root : roots(game))
    for (const auto &candidate :
         {root / "game-assets" / relative,
          root / "games" / game / "game-assets" / relative, root / relative})
      if (std::filesystem::exists(candidate, error))
        return candidate;
#ifdef __ANDROID__
  // OBB data may hold the game files directly.
  if (!targetGame.empty()) {
    if (auto data = preferredDataDirectory(targetGame) / relative;
        std::filesystem::exists(data, error))
      return data;
  }
#endif
  return std::nullopt;
}

std::filesystem::path
FileResolver::gameAsset(const std::string &game,
                        const std::filesystem::path &relative) {
  if (auto found = findGameAsset(game, relative))
    return *found;
  std::string searched;
  for (const auto &directory : applicationDirectories(game).gameAssets)
    searched += "\n  " + (directory / relative).string();
  throw std::runtime_error("Game file not found: " + relative.string() +
                           "; searched:" + searched);
}

std::filesystem::path FileResolver::fontDirectory() {
  return engineAsset("fonts");
}

bool FileResolver::isFontFile(const std::filesystem::path &path) {
  auto extension = path.extension().string();
  std::transform(
      extension.begin(), extension.end(), extension.begin(),
      [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
  if (extension != ".ttf" && extension != ".otf" && extension != ".ttc")
    return false;
  std::error_code error;
  if (!std::filesystem::is_regular_file(path, error))
    return false;
  std::ifstream file(path, std::ios::binary);
  char signature[4];
  if (!file.read(signature, sizeof(signature)))
    return false;
  const std::string header(signature, sizeof(signature));
  return header == std::string("\0\1\0\0", 4) || header == "OTTO" ||
         header == "ttcf" || header == "true";
}

std::filesystem::path FileResolver::font(const std::string &file) {
  std::string filename = file.empty() ? defaultFontName() : file;
  std::error_code error;
  std::filesystem::path path(filename);
  if (path.is_absolute() ||
      filename.find_first_of("/\\") != std::string::npos) {
    if (auto found = find(path))
      return *found;
    if (std::filesystem::exists(path, error))
      return path;
  }
  if (auto found = findEngineAsset(std::filesystem::path("fonts") / filename))
    return *found;
  if (!file.empty())
    if (auto found = findAsset(path))
      return *found;
  return engineAsset(std::filesystem::path("fonts") / filename);
}

const std::unordered_map<std::string, std::vector<uint8_t>> &
FileResolver::getFontMap() {
  static std::unordered_map<std::string, std::vector<uint8_t>> cache;
  static std::once_flag flag;
  std::call_once(flag, []() {
    std::vector<std::filesystem::path> searchDirs;
    for (const auto &directory : applicationDirectories().engineAssets)
      searchDirs.push_back(directory / "fonts");

    std::error_code ec;
    for (const auto &dir : searchDirs) {
      if (std::filesystem::exists(dir, ec) &&
          std::filesystem::is_directory(dir, ec)) {
        for (const auto &entry : std::filesystem::directory_iterator(dir, ec)) {
          if (isFontFile(entry.path())) {
            std::string filename = entry.path().filename().string();
            if (cache.find(filename) == cache.end()) {
              std::ifstream file(entry.path(),
                                 std::ios::binary | std::ios::ate);
              if (file) {
                std::streamsize size = file.tellg();
                file.seekg(0, std::ios::beg);
                std::vector<uint8_t> buffer(size);
                if (file.read(reinterpret_cast<char *>(buffer.data()), size)) {
                  cache[filename] = std::move(buffer);
                }
              }
            }
          }
        }
      }
    }

    if (cache.find(defaultFontName()) == cache.end()) {
      auto defPath = font(defaultFontName());
      if (!isFontFile(defPath))
        throw std::runtime_error("Invalid default font: " + defPath.string());
      std::ifstream file(defPath, std::ios::binary | std::ios::ate);
      if (file) {
        std::streamsize size = file.tellg();
        file.seekg(0, std::ios::beg);
        std::vector<uint8_t> buffer(size);
        if (file.read(reinterpret_cast<char *>(buffer.data()), size)) {
          cache[defaultFontName()] = std::move(buffer);
        }
      }
    }
  });
  return cache;
}

const std::vector<uint8_t> *
FileResolver::getFontBytes(const std::string &name) {
  const auto &map = getFontMap();
  if (!name.empty()) {
    std::filesystem::path p(name);
    std::string filename = p.filename().string();
    auto found = map.find(filename);
    if (found != map.end())
      return &found->second;
    found = map.find(name);
    if (found != map.end())
      return &found->second;
    // User-selected fonts can be outside the bundled font catalog. Resolve
    // their portable name again, then cache only supported font data.
    try {
      const auto path = font(name);
      if (isFontFile(path)) {
        static std::mutex mutex;
        static std::unordered_map<std::string, std::vector<uint8_t>> selected;
        std::lock_guard<std::mutex> lock(mutex);
        const auto key = path.string();
        if (auto cached = selected.find(key); cached != selected.end())
          return &cached->second;
        std::ifstream input(path, std::ios::binary | std::ios::ate);
        const auto size = input.tellg();
        if (size > 0) {
          std::vector<uint8_t> bytes(static_cast<size_t>(size));
          input.seekg(0);
          if (input.read(reinterpret_cast<char *>(bytes.data()), size))
            return &selected.emplace(key, std::move(bytes)).first->second;
        }
      }
    } catch (const std::exception &) {
    }
  }
  auto found = map.find(defaultFontName());
  if (found != map.end())
    return &found->second;
  if (!map.empty())
    return &map.begin()->second;
  return nullptr;
}

std::vector<uint8_t> FileResolver::loadFontMemory(const std::string &file) {
  if (const auto *bytes = getFontBytes(file)) {
    return *bytes;
  }
  return {};
}

void writeTextFileAtomically(const std::string &path,
                             const std::string &contents) {
  const std::string temporary = path + ".tmp";
  if (path.empty() || std::filesystem::exists(std::filesystem::path(temporary)))
    throw std::runtime_error(
        "Invalid output path or existing temporary file: " + temporary);
  std::ofstream output(std::filesystem::path(temporary), std::ios::binary);
  if (!output)
    throw std::runtime_error("Cannot create file: " + temporary);
  output.write(contents.data(), static_cast<std::streamsize>(contents.size()));
  output.close();
  if (!output) {
    std::filesystem::remove(std::filesystem::path(temporary));
    throw std::runtime_error("Cannot write file: " + path);
  }
  if (!SDL_RenamePath(temporary.c_str(), path.c_str())) {
    const std::string error = SDL_GetError();
    std::filesystem::remove(std::filesystem::path(temporary));
    throw std::runtime_error("Cannot replace file: " + path + ": " + error);
  }
}

} // namespace Kotonoha
