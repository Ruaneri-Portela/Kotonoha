#include <Kotonoha/core/FileSystem.hpp>

#include <SDL3/SDL.h>
#include <filesystem>
#include <algorithm>
#include <fstream>
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
  env->DeleteLocalRef(activity);
  jmethodID id = type ? env->GetMethodID(type, method, "()Ljava/lang/String;") : nullptr;
  auto value = id ? static_cast<jstring>(env->CallObjectMethod(activity, id)) : nullptr;
  if (env->ExceptionCheck() || !value) {
    if (env->ExceptionCheck()) {
      env->ExceptionDescribe();
      env->ExceptionClear();
    }
    if (value) env->DeleteLocalRef(value);
    if (type) env->DeleteLocalRef(type);
    throw std::runtime_error(std::string("Cannot locate Android directory: ") + method);
  }
  const char *text = env->GetStringUTFChars(value, nullptr);
  if (!text) {
    env->DeleteLocalRef(value);
    env->DeleteLocalRef(type);
    throw std::runtime_error("Cannot read Android storage path");
  }
  std::filesystem::path result(text);
  env->ReleaseStringUTFChars(value, text);
  env->DeleteLocalRef(value);
  env->DeleteLocalRef(type);
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
  if (auto found = FileResolver::find("assets"); found && std::filesystem::is_directory(*found))
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

std::filesystem::path preferredDataDirectory(const std::string &game) {
  validateGame(game);
#ifdef __ANDROID__
  return androidDirectory("getKotonohaDataDirectory");
#else
  return resourceDirectory();
#endif
}

std::filesystem::path preferredDocumentsDirectory(const std::string &game) {
  validateGame(game);
  std::filesystem::path directory;
#ifdef __ANDROID__
  directory = androidDirectory("getKotonohaDocumentsDirectory");
#elif defined(TARGET_OS_IPHONE) && TARGET_OS_IPHONE
  // Utiliza o SDL_GetUserFolder para resgatar a pasta Documents oficial do App no iOS
  const char *path = SDL_GetUserFolder(SDL_FOLDER_DOCUMENTS);
  if (!path)
    throw std::runtime_error(std::string("Cannot locate Documents: ") + SDL_GetError());
  
  directory = path;
  
  // OPCIONAL: Se quiser separar as subpastas por jogo mantendo visível no Files app:
  // directory /= game;
#else
  char *path = SDL_GetPrefPath("Kotonoha", game.c_str());
  if (!path)
    throw std::runtime_error(std::string("Cannot locate documents: ") + SDL_GetError());
  directory = path;
  SDL_free(path);
#endif

  std::filesystem::create_directories(directory);
  if (directory.filename().empty())
    directory = directory.parent_path();
  return directory;
}

std::vector<std::filesystem::path> FileResolver::roots(const std::string &game) {
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
  if (!game.empty())
    add([&] { return preferredDataDirectory(game); });
  add([] { return std::filesystem::current_path(); });
  add([] {
    const char *base = SDL_GetBasePath();
    return base ? std::filesystem::path(base) : std::filesystem::path();
  });
  if (!game.empty())
    add([&] { return preferredDocumentsDirectory(game); });
  return result;
}

std::optional<std::filesystem::path> FileResolver::find(const std::filesystem::path &relative,
                                                        const std::string &game) {
  std::error_code error;
  if (relative.empty())
    return std::nullopt;
  if (relative.is_absolute())
    return std::filesystem::exists(relative, error) ? std::optional(relative) : std::nullopt;
  for (const auto &root : roots(game)) {
    auto candidate = root / relative;
    if (std::filesystem::exists(candidate, error))
      return candidate;
  }
  return std::nullopt;
}

std::filesystem::path FileResolver::resolve(const std::filesystem::path &relative,
                                            const std::string &game) {
  if (auto found = find(relative, game))
    return *found;
  std::string searched;
  for (const auto &root : relative.is_absolute() ? std::vector<std::filesystem::path>{}
                                                 : roots(game))
    searched += "\n  " + (root / relative).string();
  throw std::runtime_error("File not found: " + relative.string() +
                           (searched.empty() ? "" : "; searched:" + searched));
}

std::optional<std::filesystem::path> FileResolver::findEngineAsset(const std::filesystem::path &relative) {
  return find(std::filesystem::path("assets") / relative);
}

std::filesystem::path FileResolver::engineAsset(const std::filesystem::path &relative) {
  return resolve(std::filesystem::path("assets") / relative);
}

std::optional<std::filesystem::path> FileResolver::findGameAsset(const std::string &game,
                                                                 const std::filesystem::path &relative) {
  validateGame(game);
  if (relative.is_absolute())
    return find(relative);
  std::error_code error;
  for (const auto &root : roots(game))
    for (const auto &candidate : {root / "game-assets" / relative,
                                  root / "games" / game / "game-assets" / relative})
      if (std::filesystem::exists(candidate, error))
        return candidate;
#ifdef __ANDROID__
  // OBB data may hold the game files directly.
  if (auto data = preferredDataDirectory(game) / relative; std::filesystem::exists(data, error))
    return data;
#endif
  return std::nullopt;
}

std::filesystem::path FileResolver::gameAsset(const std::string &game,
                                              const std::filesystem::path &relative) {
  if (auto found = findGameAsset(game, relative))
    return *found;
  std::string searched;
  for (const auto &root : roots(game))
    searched += "\n  " + (root / "game-assets" / relative).string();
  throw std::runtime_error("Game file not found: " + relative.string() + "; searched:" + searched);
}

std::filesystem::path FileResolver::fontDirectory() { return engineAsset("fonts"); }

std::filesystem::path FileResolver::font(const std::string &file) {
  if (file.empty())
    return engineAsset("fonts/NotoSansJP-Regular.ttf");
  std::error_code error;
  std::filesystem::path path(file);
  if (path.is_absolute() || file.find_first_of("/\\") != std::string::npos) {
    if (auto found = find(path))
      return *found;
    if (std::filesystem::exists(path, error))
      return path;
  }
  if (auto found = findEngineAsset(std::filesystem::path("fonts") / file))
    return *found;
  return engineAsset(std::filesystem::path("fonts") / file);
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
