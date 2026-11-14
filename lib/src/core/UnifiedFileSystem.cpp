#include <Kotonoha/core/UnifiedFileSystem.hpp>

#include <Kotonoha/core/AssetProvider.hpp>

#include <algorithm>
#include <fstream>
#include <limits>
#include <stdexcept>
#include <utility>

namespace Kotonoha {
namespace {
constexpr std::uintmax_t maxReadBytes = 1024ULL * 1024ULL * 1024ULL;

std::vector<std::uint8_t> readPhysical(const std::filesystem::path &file) {
  std::error_code ec;
  const auto size = std::filesystem::file_size(file, ec);
  if (ec || size > maxReadBytes)
    throw std::runtime_error("Cannot read asset size or asset exceeds 1 GiB: " +
                             file.string());

  std::ifstream stream(file, std::ios::binary);
  if (!stream)
    throw std::runtime_error("Cannot open asset: " + file.string());
  std::vector<std::uint8_t> bytes(static_cast<std::size_t>(size));
  if (!bytes.empty() &&
      !stream.read(reinterpret_cast<char *>(bytes.data()),
                   static_cast<std::streamsize>(bytes.size())))
    throw std::runtime_error("Cannot read asset: " + file.string());
  return bytes;
}
} // namespace

UnifiedFileSystem::UnifiedFileSystem(
    const StorageLocations &locations, std::string game,
    std::shared_ptr<AssetProvider> gameProvider)
    : layout_(directoryLayout(locations, game)),
      gameProvider_(std::move(gameProvider)) {}

void UnifiedFileSystem::setGameProvider(
    std::shared_ptr<AssetProvider> gameProvider) {
  gameProvider_ = std::move(gameProvider);
}

std::filesystem::path UnifiedFileSystem::checkedRelative(
    const std::filesystem::path &relative) {
  auto text = relative.generic_string();
  std::replace(text.begin(), text.end(), '\\', '/');
  if (text.empty() || text.find('\0') != std::string::npos ||
      text.find(':') != std::string::npos)
    throw std::invalid_argument("Invalid resource path");

  const std::filesystem::path path(text);
  if (path.is_absolute() || path.has_root_name() || path.has_root_directory())
    throw std::invalid_argument("Resource path must be relative");
  // Reject traversal even if lexical normalization would cancel it.
  for (const auto &part : path)
    if (part == "..")
      throw std::invalid_argument("Resource path may not traverse directories");

  const auto clean = path.lexically_normal();
  if (clean.empty() || clean == "." || clean.filename().empty())
    throw std::invalid_argument("Invalid resource path");
  return clean;
}

bool UnifiedFileSystem::within(const std::filesystem::path &path,
                               const std::filesystem::path &root) {
  auto actual = path.begin();
  for (auto base = root.begin(); base != root.end(); ++base, ++actual)
    if (actual == path.end() || *actual != *base)
      return false;
  return true;
}

std::optional<std::filesystem::path> UnifiedFileSystem::findInRoots(
    const std::vector<std::filesystem::path> &roots,
    const std::filesystem::path &relative) {
  for (const auto &root : roots) {
    if (root.empty())
      continue;
    const auto candidate = root / relative;
    std::error_code ec;
    if (!std::filesystem::is_regular_file(candidate, ec))
      continue;
    const auto resolvedRoot = std::filesystem::weakly_canonical(root, ec);
    if (ec)
      continue;
    const auto resolvedFile = std::filesystem::weakly_canonical(candidate, ec);
    if (!ec && within(resolvedFile, resolvedRoot))
      return candidate;
  }
  return std::nullopt;
}

std::optional<std::filesystem::path> UnifiedFileSystem::find(
    FileDomain domain, const std::filesystem::path &relative) const {
  const auto safe = checkedRelative(relative);
  if (domain == FileDomain::Game && gameProvider_ &&
      gameProvider_->contains(safe.generic_string())) {
    // An archive-backed resource may be readable without a local path.
    try {
      const auto local = gameProvider_->localPath(safe.generic_string());
      if (!local.empty()) {
        std::error_code ec;
        if (std::filesystem::is_regular_file(local, ec))
          return std::filesystem::path(local);
      }
    } catch (const std::exception &) {
      // A provider may support read() but not localPath().
    }
    return std::nullopt;
  }

  switch (domain) {
  case FileDomain::Engine:
    return findInRoots(layout_.engineAssets, safe);
  case FileDomain::Game:
    return findInRoots(layout_.gameAssets, safe);
  case FileDomain::Reference:
    return findInRoots(layout_.references, safe);
  case FileDomain::Saves:
    return findInRoots({layout_.saves}, safe);
  }
  return std::nullopt;
}

bool UnifiedFileSystem::contains(
    FileDomain domain, const std::filesystem::path &relative) const {
  const auto safe = checkedRelative(relative);
  if (domain == FileDomain::Game && gameProvider_ &&
      gameProvider_->contains(safe.generic_string()))
    return true;
  return find(domain, safe).has_value();
}

std::optional<std::vector<std::uint8_t>> UnifiedFileSystem::read(
    FileDomain domain, const std::filesystem::path &relative) const {
  const auto safe = checkedRelative(relative);
  if (domain == FileDomain::Game && gameProvider_ &&
      gameProvider_->contains(safe.generic_string())) {
    const auto bytes = gameProvider_->read(safe.generic_string());
    return std::vector<std::uint8_t>(bytes.begin(), bytes.end());
  }
  if (const auto file = find(domain, safe))
    return readPhysical(*file);
  return std::nullopt;
}

void UnifiedFileSystem::writeSave(
    const std::filesystem::path &relative,
    const std::vector<std::uint8_t> &bytes) const {
  const auto safe = checkedRelative(relative);
  if (layout_.saves.empty())
    throw std::runtime_error("No writable save directory configured");

  std::error_code ec;
  const auto root = std::filesystem::weakly_canonical(layout_.saves, ec);
  if (ec)
    throw std::runtime_error("Cannot resolve save directory");
  const auto destination = layout_.saves / safe;
  // Check path containment *before* creating any parent directories.
  auto parent = std::filesystem::weakly_canonical(destination.parent_path(), ec);
  if (ec || !within(parent, root))
    throw std::invalid_argument("Save path escapes its writable directory");
  if (std::filesystem::is_symlink(destination, ec))
    throw std::invalid_argument("Save destination may not be a symlink");

  std::filesystem::create_directories(destination.parent_path());
  parent = std::filesystem::weakly_canonical(destination.parent_path(), ec);
  if (ec || !within(parent, root))
    throw std::invalid_argument("Save directory changed during creation");

  // Existing engine writer uses SDL_RenamePath for cross-platform replacement.
  writeTextFileAtomically(destination.string(),
                          std::string(bytes.begin(), bytes.end()));
}

} // namespace Kotonoha
