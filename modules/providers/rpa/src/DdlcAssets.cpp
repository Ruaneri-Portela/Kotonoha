#include <Ddlc/DdlcAssets.hpp>

#include <Kotonoha/Filesystem/StoragePaths.hpp>

#include <SDL3/SDL.h>

#include <Kotonoha/Filesystem/Adapters/StreamAdapter.hpp>
#include <algorithm>
#include <stdexcept>

namespace Kotonoha::Providers {
using namespace Ddlc;
} // namespace Kotonoha::Providers

namespace Ddlc {
namespace {

bool equalsIgnoreCase(std::string_view a, std::string_view b) {
  if (a.size() != b.size())
    return false;
  for (std::size_t i = 0; i < a.size(); ++i) {
    char ca = a[i];
    char cb = b[i];
    if (ca >= 'A' && ca <= 'Z')
      ca = static_cast<char>(ca - 'A' + 'a');
    if (cb >= 'A' && cb <= 'Z')
      cb = static_cast<char>(cb - 'A' + 'a');
    if (ca != cb)
      return false;
  }
  return true;
}

std::filesystem::path findChildCaseInsensitive(const std::filesystem::path &dir,
                                               std::string_view name) {
  std::error_code ec;
  const auto direct = dir / name;
  if (std::filesystem::exists(direct, ec) && !ec)
    return direct;
  if (!std::filesystem::is_directory(dir, ec) || ec)
    return {};
  for (std::filesystem::directory_iterator it(
           dir, std::filesystem::directory_options::skip_permission_denied, ec),
       end;
       it != end && !ec; it.increment(ec)) {
    if (equalsIgnoreCase(it->path().filename().string(), name))
      return it->path();
  }
  return {};
}

std::filesystem::path
findPathCaseInsensitive(const std::filesystem::path &base,
                        std::initializer_list<std::string_view> components) {
  std::filesystem::path current = base;
  for (std::string_view part : components) {
    current = findChildCaseInsensitive(current, part);
    if (current.empty())
      return {};
  }
  return current;
}

bool isRegularFileCI(const std::filesystem::path &base,
                     std::initializer_list<std::string_view> components) {
  const auto found = findPathCaseInsensitive(base, components);
  if (found.empty())
    return false;
  std::error_code ec;
  return std::filesystem::is_regular_file(found, ec) && !ec;
}

bool isDirectoryCI(const std::filesystem::path &base,
                   std::initializer_list<std::string_view> components,
                   std::filesystem::path *resolved = nullptr) {
  const auto found = findPathCaseInsensitive(base, components);
  if (found.empty())
    return false;
  std::error_code ec;
  if (!std::filesystem::is_directory(found, ec) || ec)
    return false;
  if (resolved)
    *resolved = found;
  return true;
}

bool hasRenPyPayload(const std::filesystem::path &folder) {
  std::error_code ec;
  for (std::filesystem::directory_iterator
           it(folder,
              std::filesystem::directory_options::skip_permission_denied, ec),
       end;
       it != end && !ec; it.increment(ec)) {
    std::error_code fileError;
    if (!it->is_regular_file(fileError) || fileError)
      continue;
    std::string ext = it->path().extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(),
                   [](unsigned char c) { return char(std::tolower(c)); });
    if (ext == ".rpa" || ext == ".rpx" || ext == ".rpyc" || ext == ".rpymc")
      return true;
  }
  return false;
}

} // namespace

bool hasOriginalCharacters(const std::filesystem::path &gameDirectory,
                           std::error_code &error) {
  const auto game = std::filesystem::absolute(gameDirectory, error);
  if (error)
    return false;
  std::filesystem::path charsDir;
  if (!isDirectoryCI(game.parent_path(), {"characters"}, &charsDir))
    return false;
  for (const char *name : kCharacterFiles) {
    if (!isRegularFileCI(charsDir, {name}))
      return false;
  }
  return true;
}

std::filesystem::path userDirectory() {
  if (const char *configured = SDL_getenv("DDLC_SAVE_DIR");
      configured && *configured) {
    std::filesystem::path directory(configured);
    std::error_code error;
    std::filesystem::create_directories(directory, error);
    if (error)
      throw std::runtime_error("Cannot create DDLC save directory: " +
                               error.message());
    return std::filesystem::absolute(directory);
  }
  return Kotonoha::saveDirectory();
}

bool seedCharacters(const std::filesystem::path &user,
                    const std::filesystem::path &gameDirectory,
                    const Assets *archives) {
  const auto target = charactersDirectory(user);
  std::error_code error;
  if (std::filesystem::exists(target, error))
    return false;
  if (error)
    throw std::runtime_error("Cannot inspect " + target.string() + ": " +
                             error.message());
  const auto staging = user / "characters.seeding";
  std::filesystem::remove_all(staging, error);
  std::filesystem::create_directories(staging, error);
  if (error)
    throw std::runtime_error("Cannot create " + staging.string() + ": " +
                             error.message());
  std::filesystem::path originals;
  if (!isDirectoryCI(
          std::filesystem::absolute(gameDirectory, error).parent_path(),
          {"characters"}, &originals)) {
    originals = std::filesystem::absolute(gameDirectory, error).parent_path() /
                "characters";
  }
  for (const char *name : kCharacterFiles) {
    const auto source = findChildCaseInsensitive(originals, name);
    if (!source.empty() && std::filesystem::is_regular_file(source, error)) {
      std::filesystem::copy_file(source, staging / name, error);
      if (error)
        throw std::runtime_error("Cannot copy " + source.string() + ": " +
                                 error.message());
      continue;
    }
    if (archives && archives->has(name)) {
      const auto bytes = archives->read(name);
      Kotonoha::Filesystem::OutputFile out(staging / name, std::ios::binary);
      if (!out || !out.write(reinterpret_cast<const char *>(bytes.data()),
                             std::streamsize(bytes.size())))
        throw std::runtime_error("Cannot write " + (staging / name).string());
      continue;
    }
    SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                "DDLC: no original %s to place in %s", name,
                target.string().c_str());
  }
  std::filesystem::rename(staging, target, error);
  if (error)
    throw std::runtime_error("Cannot create " + target.string() + ": " +
                             error.message());
  SDL_Log("DDLC: character files placed in %s", target.string().c_str());
  return true;
}

} // namespace Ddlc

namespace Kotonoha::Providers {
using namespace Ddlc;

DdlcAssets::DdlcAssets(const std::filesystem::path &directory,
                       const std::filesystem::path &overlayDirectory) {
  auto gameDirectory = directory;
  std::filesystem::path subDir;
  if (isDirectoryCI(directory, {"game"}, &subDir) && hasRenPyPayload(subDir)) {
    gameDirectory = subDir;
  } else if (isDirectoryCI(directory, {"vnx", "game"}, &subDir) &&
             isRegularFileCI(subDir, {"scripts.rpa"})) {
    gameDirectory = subDir;
  }
  std::error_code ec;
  const auto absGame =
      std::filesystem::absolute(gameDirectory, ec).lexically_normal();
  if (ec || !assets_.open(absGame.string()))
    throw std::runtime_error("Cannot open Ren'Py archives in " +
                             directory.string());
  sourceIdentity_ = absGame.generic_string();
  configureOverlaysAndScripts(overlayDirectory, true);
}

DdlcAssets::DdlcAssets(std::shared_ptr<Kotonoha::Filesystem::Filesystem> source,
                       const std::filesystem::path &overlayDirectory) {
  if (!assets_.open(std::move(source)))
    throw std::runtime_error("Cannot open virtual Ren'Py archives");
  configureOverlaysAndScripts(overlayDirectory, false);
}

void DdlcAssets::configureOverlaysAndScripts(
    const std::filesystem::path &overlayDirectory, bool inspectCharacterPaths) {
  std::filesystem::path effectiveOverlay = overlayDirectory;
  bool autoSiblingVnx = false;
  if (effectiveOverlay.empty() && inspectCharacterPaths) {
    const auto parent = std::filesystem::path(assets_.dir()).parent_path();
    if (isDirectoryCI(parent, {"vnx", "game"})) {
      effectiveOverlay = parent;
      autoSiblingVnx = true;
    }
  }
  if (!effectiveOverlay.empty()) {
    std::vector<std::filesystem::path> roots;
    std::filesystem::path subRoot;
    if (isDirectoryCI(effectiveOverlay, {"vnx", "game"}, &subRoot))
      roots.push_back(subRoot);
    if (!autoSiblingVnx && isDirectoryCI(effectiveOverlay, {"game"}, &subRoot))
      roots.push_back(subRoot);
    if (roots.empty())
      roots.push_back(effectiveOverlay);
    auto rootOverlay =
        std::make_shared<Kotonoha::AssetOverlay>(effectiveOverlay);
    auto overlays = std::make_shared<std::vector<Kotonoha::AssetOverlay>>();
    std::vector<std::string> packages;
    for (const auto &root : roots) {
      overlays->emplace_back(root);
      std::vector<std::filesystem::path> archives;
      std::error_code dirEc;
      for (std::filesystem::directory_iterator it(
               root, std::filesystem::directory_options::skip_permission_denied,
               dirEc),
           end;
           it != end && !dirEc; it.increment(dirEc)) {
        std::error_code fileEc;
        const auto extension =
            Kotonoha::AssetOverlay::key(it->path().extension().string());
        if (it->is_regular_file(fileEc) && !fileEc &&
            (extension == ".rpa" || extension == ".rpx"))
          archives.push_back(it->path());
      }
      std::sort(archives.rbegin(), archives.rend());
      for (const auto &path : archives)
        packages.push_back(path.string());
    }
    assets_.mountOverlay(
        packages,
        [overlays](const std::string &id, std::vector<uint8_t> *bytes) {
          for (const auto &overlay : *overlays) {
            auto file = overlay.resolve(id);
            if (file.empty())
              continue;
            if (bytes) {
              Kotonoha::FileSystemAssetProvider source;
              *bytes = source.read(file);
            }
            return true;
          }
          return false;
        },
        [overlays] {
          std::vector<std::string> result;
          for (const auto &overlay : *overlays) {
            auto names = overlay.list();
            result.insert(result.end(), names.begin(), names.end());
          }
          return result;
        },
        [rootOverlay](const std::string &id, std::vector<uint8_t> *bytes) {
          auto file = rootOverlay->resolve(id);
          if (file.empty())
            return false;
          if (bytes) {
            Kotonoha::FileSystemAssetProvider source;
            *bytes = source.read(file);
          }
          return true;
        },
        [rootOverlay] { return rootOverlay->list(); });
    setOverlay(roots.front());
  }
  std::string error;
  if (!script_.load(assets_, &error))
    throw std::runtime_error("Cannot load compiled Ren'Py scripts" +
                             (error.empty() ? std::string() : ": " + error));
  if (!script_.hasLabel("start") &&
      !(isKatawa() && script_.hasLabel("imachine")))
    throw std::runtime_error("Ren'Py game has no executable start label");
  // Ren'Py's splashscreen is optional, unlike the original DDLC port.
  std::error_code pathError;
  if (isDdlc() && inspectCharacterPaths &&
      !hasOriginalCharacters(assets_.dir(), pathError)) {
    if (pathError)
      throw std::runtime_error("Cannot inspect DDLC character files: " +
                               pathError.message());
    SDL_Log("DDLC: no sibling characters directory; the .chr files come from "
            "scripts.rpa");
  }
}

std::string DdlcAssets::name() const {
  if (isKatawa())
    return "Katawa Shoujo (" + assets_.dir() + ")";
  if (isDdlc())
    return "Doki Doki Literature Club! (" + assets_.dir() + ")";
  auto path = std::filesystem::path(sourceIdentity_.empty() ? assets_.dir()
                                                            : sourceIdentity_);
  if (path.filename() == "game")
    path = path.parent_path();
  const auto title = path.filename().string();
  return title.empty() || title == "<virtual>" ? "Ren'Py Game"
                                               : "Ren'Py: " + title;
}

bool DdlcAssets::containsAsset(const std::string &id) const {
  return assets_.has(id);
}

std::vector<Uint8> DdlcAssets::readAsset(const std::string &id) const {
  if (!contains(id))
    throw std::runtime_error("DDLC asset not found: " + id);
  return assets_.read(id);
}

std::vector<std::string>
DdlcAssets::listAsset(const std::string &prefix) const {
  auto result = assets_.names();
  result.erase(std::remove_if(result.begin(), result.end(),
                              [&](const std::string &id) {
                                return id.compare(0, prefix.size(), prefix) !=
                                       0;
                              }),
               result.end());
  std::sort(result.begin(), result.end());
  result.erase(std::unique(result.begin(), result.end()), result.end());
  return result;
}

} // namespace Kotonoha::Providers
