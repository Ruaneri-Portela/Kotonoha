#include <Kotonoha/core/Config.hpp>
#include <Kotonoha/core/FileSystem.hpp>

#include <SDL3/SDL.h>
#include <algorithm>
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

#ifdef __ANDROID__
std::filesystem::path androidDirectory(const char *method) {
  auto *env = static_cast<JNIEnv *>(SDL_GetAndroidJNIEnv());
  auto activity = static_cast<jobject>(SDL_GetAndroidActivity());
  if (!env || !activity)
    throw std::runtime_error("Android storage requires an SDL activity");

  jclass type = env->GetObjectClass(activity);
  jmethodID id =
      type ? env->GetMethodID(type, method, "()Ljava/lang/String;") : nullptr;

  // A chamada precisa acontecer ENQUANTO activity ainda é um local ref válido
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
    env->DeleteLocalRef(activity); // Libera o activity aqui
    throw std::runtime_error(std::string("Cannot locate Android directory: ") +
                             method);
  }

  const char *text = env->GetStringUTFChars(value, nullptr);
  if (!text) {
    env->DeleteLocalRef(value);
    env->DeleteLocalRef(type);
    env->DeleteLocalRef(activity); // Libera o activity aqui
    throw std::runtime_error("Cannot read Android storage path");
  }

  std::filesystem::path result(text);
  env->ReleaseStringUTFChars(value, text);
  env->DeleteLocalRef(value);
  env->DeleteLocalRef(type);
  env->DeleteLocalRef(activity); // Libera o activity ao final de tudo

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
  if (auto found = FileResolver::find("assets");
      found && std::filesystem::is_directory(*found))
    return *found;
  return resourceDirectory() / "assets";
}

std::filesystem::path gameAssetDirectory(const std::string &game) {
  validateGame(game);
  if (auto found = FileResolver::find("game-assets", game);
      found && std::filesystem::is_directory(*found))
    return *found;
  return resourceDirectory() / "game-assets";
}

std::optional<std::filesystem::path>
findAssetsGame(const std::string &game = "") {
  std::string targetGame = game.empty() ? preferredGameIdentifier() : game;
  if (!targetGame.empty()) {
    validateGame(targetGame);
  }

  std::error_code error;
  for (const auto &root : FileResolver::roots(targetGame)) {
    std::vector<std::filesystem::path> candidates = {root / "game-assets"};

    if (!targetGame.empty()) {
      candidates.push_back(root / "games" / targetGame / "game-assets");
      candidates.push_back(root / targetGame / "game-assets");
    }

    for (const auto &candidate : candidates) {
      if (std::filesystem::is_directory(candidate, error))
        return candidate;
    }
  }
  return std::nullopt;
}

std::string preferredGameIdentifier() {
  if (const char *meta =
          SDL_GetAppMetadataProperty(SDL_PROP_APP_METADATA_IDENTIFIER_STRING);
      meta && *meta)
    return meta;
  return {};
}

std::filesystem::path preferredDataDirectory() {
  return preferredDataDirectory(preferredGameIdentifier());
}

std::filesystem::path preferredDataDirectory(const std::string &game) {
  (void)game;
#ifdef __ANDROID__
  return androidDirectory("getKotonohaDataDirectory");
#else
  return resourceDirectory();
#endif
}

std::filesystem::path
findAssetOverlayDirectory(const std::filesystem::path &root) {
  std::vector<std::filesystem::path> candidates;

  if (const char *env = SDL_getenv("KOTONOHA_OVERLAY")) {
    if (*env)
      candidates.push_back(env);
  }

  if (!root.empty()) {
    if (root.is_absolute()) {
      candidates.push_back(root);
    } else {
      for (const auto &r : FileResolver::roots()) {
        candidates.push_back(r / root);
      }
    }
  } else {
    for (const auto &r : FileResolver::roots()) {
      candidates.push_back(r / "overlay");
    }
  }

  std::error_code error;
  for (const auto &candidate : candidates) {
    if (!candidate.empty() && std::filesystem::is_directory(candidate, error)) {
      return candidate;
    }
  }

  return {};
}

std::filesystem::path preferredDocumentsDirectory() {
  return preferredDocumentsDirectory(preferredGameIdentifier());
}

std::filesystem::path preferredDocumentsDirectory(const std::string &game) {
  std::string targetGame = preferredGameIdentifier();
  if (targetGame.empty())
    targetGame = game;
  validateGame(targetGame);
  std::filesystem::path directory;
#ifdef __ANDROID__
  directory = androidDirectory("getKotonohaDocumentsDirectory");
#elif defined(TARGET_OS_IPHONE) && TARGET_OS_IPHONE
  const char *path = SDL_GetUserFolder(SDL_FOLDER_DOCUMENTS);
  if (!path)
    throw std::runtime_error(std::string("Cannot locate Documents: ") +
                             SDL_GetError());
  directory = path;
#else
  char *path = SDL_GetPrefPath("Kotonoha", targetGame.c_str());
  if (!path)
    throw std::runtime_error(std::string("Cannot locate documents: ") +
                             SDL_GetError());
  directory = path;
  SDL_free(path);
#endif

  std::filesystem::create_directories(directory);
  if (directory.filename().empty())
    directory = directory.parent_path();
  return directory;
}

std::vector<std::filesystem::path>
FileResolver::roots(const std::string &game) {
  std::vector<std::filesystem::path> result;
  const auto add = [&](auto &&produce) {
    std::filesystem::path path;
    try {
      path = produce();
    } catch (const std::exception &) {
      return;
    }
    if (path.empty())
      return;
    std::error_code error;
    auto normal = std::filesystem::weakly_canonical(path, error);
    if (error)
      normal = path.lexically_normal();
    if (normal.filename().empty())
      normal = normal.parent_path();
    if (std::find(result.begin(), result.end(), normal) == result.end())
      result.push_back(std::move(normal));
  };

  add([] { return resourceDirectory(); });
  add([&] { return preferredDataDirectory(game); });
  add([] { return std::filesystem::current_path(); });

  add([] {
    const char *base = SDL_GetBasePath();
    return base ? std::filesystem::path(base) : std::filesystem::path();
  });

  add([&] { return preferredDocumentsDirectory(game); });

  return result;
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
  return find(std::filesystem::path("assets") / relative);
}

std::filesystem::path
FileResolver::engineAsset(const std::filesystem::path &relative) {
  return resolve(std::filesystem::path("assets") / relative);
}

std::optional<std::filesystem::path>
FileResolver::findGameAsset(const std::string &game,
                            const std::filesystem::path &relative) {
  std::string targetGame = game;
  if (targetGame.empty())
    targetGame = preferredGameIdentifier();
  if (!targetGame.empty())
    validateGame(targetGame);
  if (relative.is_absolute())
    return find(relative);
  std::error_code error;
  for (const auto &root : roots(targetGame))
    for (const auto &candidate :
         {root / "game-assets" / relative,
          root / "games" / targetGame / "game-assets" / relative,
          root / relative})
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
  std::string targetGame = game;
  if (targetGame.empty())
    targetGame = preferredGameIdentifier();
  for (const auto &root : roots(targetGame))
    searched += "\n  " + (root / "game-assets" / relative).string();
  throw std::runtime_error("Game file not found: " + relative.string() +
                           "; searched:" + searched);
}

std::filesystem::path FileResolver::fontDirectory() {
  if (auto found = find("fonts");
      found && std::filesystem::is_directory(*found))
    return *found;
  if (auto found = find("assets/fonts");
      found && std::filesystem::is_directory(*found))
    return *found;
  std::error_code ec;
  auto resDir = resourceDirectory();
  if (std::filesystem::exists(resDir / "fonts", ec))
    return resDir / "fonts";
  if (std::filesystem::exists(resDir / "assets" / "fonts", ec))
    return resDir / "assets" / "fonts";
  return engineAsset("fonts");
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
  if (auto found = find(filename);
      found && std::filesystem::exists(*found, error))
    return *found;
  if (auto found = find(std::filesystem::path("fonts") / filename);
      found && std::filesystem::exists(*found, error))
    return *found;
  if (auto found = find(std::filesystem::path("assets") / "fonts" / filename);
      found && std::filesystem::exists(*found, error))
    return *found;
  try {
    auto resDir = resourceDirectory();
    if (std::filesystem::exists(resDir / filename, error))
      return resDir / filename;
    if (std::filesystem::exists(resDir / "fonts" / filename, error))
      return resDir / "fonts" / filename;
    if (std::filesystem::exists(resDir / "assets" / "fonts" / filename, error))
      return resDir / "assets" / "fonts" / filename;
  } catch (...) {
  }
  return engineAsset(std::filesystem::path("fonts") / filename);
}

const std::unordered_map<std::string, std::vector<uint8_t>> &
FileResolver::getFontMap() {
  static std::unordered_map<std::string, std::vector<uint8_t>> cache;
  static std::once_flag flag;
  std::call_once(flag, []() {
    std::vector<std::filesystem::path> searchDirs;
    searchDirs.push_back(fontDirectory());
    try {
      auto resDir = resourceDirectory();
      searchDirs.push_back(resDir);
      searchDirs.push_back(resDir / "fonts");
      searchDirs.push_back(resDir / "assets" / "fonts");
    } catch (...) {
    }

    std::error_code ec;
    for (const auto &dir : searchDirs) {
      if (std::filesystem::exists(dir, ec) &&
          std::filesystem::is_directory(dir, ec)) {
        for (const auto &entry : std::filesystem::directory_iterator(dir, ec)) {
          if (entry.is_regular_file(ec)) {
            auto ext = entry.path().extension().string();
            std::string lower;
            for (char c : ext)
              lower += static_cast<char>(std::tolower(c));
            if (lower == ".ttf" || lower == ".otf" || lower == ".ttc") {
              std::string filename = entry.path().filename().string();
              if (cache.find(filename) == cache.end()) {
                std::ifstream file(entry.path(),
                                   std::ios::binary | std::ios::ate);
                if (file) {
                  std::streamsize size = file.tellg();
                  file.seekg(0, std::ios::beg);
                  std::vector<uint8_t> buffer(size);
                  if (file.read(reinterpret_cast<char *>(buffer.data()),
                                size)) {
                    cache[filename] = std::move(buffer);
                  }
                }
              }
            }
          }
        }
      }
    }

    if (cache.find(defaultFontName()) == cache.end()) {
      auto defPath = font(defaultFontName());
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
  if (path.empty() || std::filesystem::exists(temporary))
    throw std::runtime_error(
        "Invalid output path or existing temporary file: " + temporary);
  std::ofstream output(temporary, std::ios::binary);
  if (!output)
    throw std::runtime_error("Cannot create file: " + temporary);
  output.write(contents.data(), static_cast<std::streamsize>(contents.size()));
  output.close();
  if (!output) {
    std::filesystem::remove(temporary);
    throw std::runtime_error("Cannot write file: " + path);
  }
  if (!SDL_RenamePath(temporary.c_str(), path.c_str())) {
    const std::string error = SDL_GetError();
    std::filesystem::remove(temporary);
    throw std::runtime_error("Cannot replace file: " + path + ": " + error);
  }
}

} // namespace Kotonoha
