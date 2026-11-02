#include <Kotonoha/core/AssetOverlay.hpp>
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
AssetOverlay::AssetOverlay(const std::filesystem::path &root) {
  if (root.empty())
    return;
  root_ = std::filesystem::canonical(root);
  if (!std::filesystem::is_directory(root_))
    throw std::invalid_argument("Asset overlay must be a directory: " +
                                root.string());
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
