#include <Kotonoha/Filesystem/Path.hpp>
#include <Kotonoha/Filesystem/StoragePaths.hpp>
#include <Kotonoha/core/AssetOverlay.hpp>
#include <SDL3/SDL_log.h>
#include <stdexcept>

namespace Kotonoha {

std::string AssetOverlay::key(std::string id) {
  id = Kotonoha::Filesystem::normalizeUtf8Nfc(id);
  for (auto &c : id) {
    if (c == '\\')
      c = '/';
    if (c >= 'A' && c <= 'Z')
      c += 32;
  }
  std::filesystem::path p(id);
  if (p.is_absolute() || id.find(':') != id.npos)
    return {};
  p = p.lexically_normal();
  for (const auto &part : p)
    if (part == "..")
      return {};
  auto result = p.generic_string();
  return result == "." ? std::string() : result;
}

AssetOverlay::AssetOverlay(const std::filesystem::path &root,
                           bool discoverOverlay) {
  std::error_code ec;
  std::filesystem::path targetRoot =
      discoverOverlay ? findAssetOverlayDirectory(root) : root;
  // Absolute paths that exist on disk should be accepted directly even when
  // discovery (e.g. through reference roots or KOTONOHA_OVERLAY) fails to
  // locate them — this covers network volumes and paths with special
  // characters that may not resolve through the standard search.
  if (targetRoot.empty() && !root.empty() && root.is_absolute() &&
      std::filesystem::is_directory(root, ec))
    targetRoot = root;
  if (!root.empty() &&
      (targetRoot.empty() || !std::filesystem::is_directory(targetRoot, ec)))
    throw std::invalid_argument("Asset directory does not exist: " +
                                root.string());

  if (targetRoot.empty() || !std::filesystem::exists(targetRoot, ec) ||
      !std::filesystem::is_directory(targetRoot, ec))
    return;

  root_ = std::filesystem::canonical(targetRoot, ec);
  if (ec || !std::filesystem::is_directory(root_, ec))
    return;

  std::filesystem::recursive_directory_iterator it(
      root_, std::filesystem::directory_options::skip_permission_denied, ec),
      end;
  if (ec)
    return;
  for (; it != end; it.increment(ec)) {
    if (ec) {
      ec.clear();
      continue;
    }
    const auto filename = it->path().filename().string();
    if (filename == ".DS_Store" ||
        (filename.size() >= 2 && filename[0] == '.' && filename[1] == '_') ||
        (filename.size() >= 7 &&
         filename.compare(filename.size() - 7, 7, ".icloud") == 0))
      continue;

    std::error_code entryEc;
    if (!it->is_regular_file(entryEc) || entryEc)
      continue;
    auto actual = std::filesystem::canonical(it->path(), entryEc);
    if (entryEc)
      continue;
    auto relative = actual.lexically_relative(root_);
    if (relative.empty() || *relative.begin() == "..")
      throw std::invalid_argument(
          "Asset overlay symlink escapes its directory: " +
          it->path().string());
    auto id = key(it->path().lexically_relative(root_).generic_string());
    if (id.empty()) {
      SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                  "AssetOverlay: skipping unsupported overlay path '%s'",
                  it->path().string().c_str());
      continue;
    }
    if (!files_.emplace(id, actual).second) {
      SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                  "AssetOverlay: case-insensitive collision for '%s', "
                  "keeping first entry",
                  id.c_str());
    }
  }
}

std::string AssetOverlay::resolve(const std::string &id) const {
  auto i = files_.find(key(id));
  return i == files_.end() ? std::string() : i->second.string();
}

std::vector<std::string> AssetOverlay::list(const std::string &prefix) const {
  std::vector<std::string> result;
  auto start = key(prefix);
  for (auto &[id, path] : files_)
    if (id.compare(0, start.size(), start) == 0)
      result.push_back(id);
  return result;
}

} // namespace Kotonoha
