#include <Kotonoha/core/AssetOverlay.hpp>
#include <Kotonoha/core/FileSystem.hpp>
#include <stdexcept>

namespace Kotonoha {

std::string AssetOverlay::key(std::string id) {
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

  for (const auto &entry :
       std::filesystem::recursive_directory_iterator(root_)) {
    if (!entry.is_regular_file())
      continue;
    auto actual = std::filesystem::canonical(entry.path());
    auto relative = actual.lexically_relative(root_);
    if (relative.empty() || *relative.begin() == "..")
      throw std::invalid_argument(
          "Asset overlay symlink escapes its directory: " +
          entry.path().string());
    auto id = key(entry.path().lexically_relative(root_).generic_string());
    if (!files_.emplace(id, actual).second)
      throw std::invalid_argument("Ambiguous asset overlay name: " + id);
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
