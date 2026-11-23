#include <Kotonoha/Filesystem/Mounts/DirectoryMount.hpp>
#include <Kotonoha/Filesystem/Path.hpp>
#include <Kotonoha/Filesystem/Sources/SDLFileSource.hpp>
#include <SDL3/SDL_log.h>

#include <limits>
#include <string_view>
#include <utility>
#ifdef __EMSCRIPTEN__
#include <emscripten/emscripten.h>
#endif

namespace Kotonoha::Filesystem {
namespace {

bool contained(const std::filesystem::path &file,
               const std::filesystem::path &root) {
  auto f = file.begin();
  for (auto r = root.begin(); r != root.end(); ++r, ++f)
    if (f == file.end() || *r != *f)
      return false;
  return true;
}

bool shouldSkipSystemEntry(std::string_view filename) {
  if (filename == ".DS_Store" || filename == ".Spotlight-V100" ||
      filename == ".Trashes" || filename == ".fseventsd")
    return true;
  if (filename.size() >= 2 && filename[0] == '.' && filename[1] == '_')
    return true;
  constexpr std::string_view kICloudSuffix = ".icloud";
  if (filename.size() >= kICloudSuffix.size() &&
      filename.compare(filename.size() - kICloudSuffix.size(),
                       kICloudSuffix.size(), kICloudSuffix) == 0)
    return true;
  return false;
}

} // namespace

DirectoryMount::DirectoryMount(const std::filesystem::path &source,
                               bool indexRecursively)
    : lazyIndex_(!indexRecursively) {
  std::error_code ec;
  root_ = std::filesystem::canonical(source, ec);
  if (ec || !std::filesystem::is_directory(root_, ec))
    throw FilesystemError(ErrorCode::NotDirectory,
                          "Mount is not a local directory: " + source.string());
  nodes_.emplace("", Node{FileInfo{"", FileType::Directory, 0}, root_});
  // Discovery only needs named probes and a few listed directories. Full
  // recursive indexing remains the default for game execution and providers.
  if (lazyIndex_)
    return;

  std::filesystem::recursive_directory_iterator it(
      root_, std::filesystem::directory_options::skip_permission_denied, ec),
      end;
  if (ec)
    throw FilesystemError(ErrorCode::IOError,
                          "Cannot enumerate mount directory: " +
                              root_.string());
  for (; it != end; it.increment(ec)) {
    if (ec) {
      SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                  "DirectoryMount: skipping unreadable entry in %s: %s",
                  root_.string().c_str(), ec.message().c_str());
      ec.clear();
      continue;
    }

    const auto filename = it->path().filename().string();
    if (shouldSkipSystemEntry(filename)) {
      std::error_code dirEc;
      if (it->is_directory(dirEc))
        it.disable_recursion_pending();
      continue;
    }

    const auto rel = it->path().lexically_relative(root_).generic_string();
    if (rel.find(':') != std::string::npos ||
        rel.find('\0') != std::string::npos) {
      SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                  "DirectoryMount: skipping entry with unsupported path "
                  "characters: %s",
                  rel.c_str());
      std::error_code dirEc;
      if (it->is_directory(dirEc))
        it.disable_recursion_pending();
      continue;
    }

    const auto id = normalizePath(rel);
    const auto key = lookupKey(id);

    std::error_code symEc;
    const auto symStatus = it->symlink_status(symEc);
    const bool isSym = !symEc && std::filesystem::is_symlink(symStatus);

    std::error_code canonEc;
    const auto real = std::filesystem::canonical(it->path(), canonEc);
    if (canonEc) {
      SDL_LogWarn(
          SDL_LOG_CATEGORY_APPLICATION,
          "DirectoryMount: skipping broken or unreadable entry '%s': %s",
          id.c_str(), canonEc.message().c_str());
      continue;
    }
    if (!contained(real, root_))
      throw FilesystemError(ErrorCode::InvalidPath,
                            "Mount entry escapes its directory: " + id);

    if (isSym) {
      std::error_code statEc;
      const auto targetStatus = it->status(statEc);
      if (statEc || std::filesystem::is_directory(targetStatus)) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                    "DirectoryMount: skipping directory symlink '%s'",
                    id.c_str());
        it.disable_recursion_pending();
        continue;
      }
    }

    std::error_code typeEc;
    const bool directory = it->is_directory(typeEc);
    const bool regular = !typeEc && it->is_regular_file(typeEc);
    if (typeEc || (!directory && !regular)) {
      SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                  "DirectoryMount: skipping unsupported entry '%s'",
                  id.c_str());
      continue;
    }

    std::int64_t size = 0;
    if (regular) {
      std::error_code sizeEc;
      const auto actualSize = it->file_size(sizeEc);
      if (sizeEc || actualSize > static_cast<std::uintmax_t>(INT64_MAX)) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                    "DirectoryMount: skipping entry with unreadable size '%s'",
                    id.c_str());
        continue;
      }
      size = static_cast<std::int64_t>(actualSize);
    }

    Node entry{
        FileInfo{id, directory ? FileType::Directory : FileType::Regular, size},
        it->path()};
    if (!nodes_.emplace(key, std::move(entry)).second) {
      SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                  "DirectoryMount: case-insensitive collision for '%s', "
                  "keeping first entry",
                  id.c_str());
    }
  }
  if (ec) {
    SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                "DirectoryMount: enumeration finished with warning in %s: %s",
                root_.string().c_str(), ec.message().c_str());
  }
}

// Lazy indexing is only used by engine discovery. Index one directory at a
// time, resolving parent names through the same case-insensitive VFS keys as
// the eager mount. Never follow a directory symlink or allow paths to escape.
void DirectoryMount::indexDirectory(std::string_view directory) const {
  if (!lazyIndex_)
    return;
  const std::string key = lookupKey(directory, true);
  if (indexedDirectories_.count(key))
    return;
  if (!key.empty()) {
    const auto slash = key.find_last_of('/');
    indexDirectory(slash == std::string::npos
                       ? std::string_view{}
                       : std::string_view(key).substr(0, slash));
  }
  const auto parent = nodes_.find(key);
  if (parent == nodes_.end() || parent->second.info.type != FileType::Directory)
    return;

  std::error_code ec;
  for (std::filesystem::directory_iterator
           it(parent->second.physical,
              std::filesystem::directory_options::skip_permission_denied, ec),
       end;
       !ec && it != end; it.increment(ec)) {
    const auto filename = it->path().filename().string();
    if (shouldSkipSystemEntry(filename))
      continue;
    const auto rel = it->path().lexically_relative(root_).generic_string();
    if (rel.find(':') != std::string::npos ||
        rel.find(static_cast<char>(0)) != std::string::npos)
      continue;

    std::error_code statusError;
    const auto symStatus = it->symlink_status(statusError);
    if (statusError)
      continue;
    const bool symlink = std::filesystem::is_symlink(symStatus);
    const bool directoryEntry = it->is_directory(statusError);
    if (statusError || (symlink && directoryEntry))
      continue;
    const bool fileEntry = !directoryEntry && it->is_regular_file(statusError);
    if (statusError || (!directoryEntry && !fileEntry))
      continue;

    const auto physical = std::filesystem::canonical(it->path(), statusError);
    if (statusError)
      continue;
    if (!contained(physical, root_))
      throw FilesystemError(ErrorCode::InvalidPath,
                            "Mount entry escapes its directory: " + rel);

    const auto id = normalizePath(rel);
    std::int64_t size = 0;
    if (fileEntry) {
      const auto actualSize = it->file_size(statusError);
      if (statusError || actualSize > static_cast<std::uintmax_t>(INT64_MAX))
        continue;
      size = static_cast<std::int64_t>(actualSize);
    }
    nodes_.emplace(
        lookupKey(id),
        Node{FileInfo{id,
                      directoryEntry ? FileType::Directory : FileType::Regular,
                      size},
             it->path()});
  }
  // An unreadable child must not trigger a full recursive retry.
  indexedDirectories_.insert(key);
}

void DirectoryMount::ensureParentIndexed(std::string_view path) const {
  if (!lazyIndex_)
    return;
  const auto key = lookupKey(path, true);
  const auto slash = key.find_last_of('/');
  indexDirectory(slash == std::string::npos
                     ? std::string_view{}
                     : std::string_view(key).substr(0, slash));
}

#ifdef __EMSCRIPTEN__
void DirectoryMount::refreshFromBrowser() const {
  // A virtual folder can be mounted after game startup. The browser changes
  // this generation only after a successful filesystem import.
  const int current =
      EM_ASM_INT({ return Module["KotonohaVfsGeneration"] | 0; });
  if (current == observedBrowserGeneration_)
    return;
  DirectoryMount updated(root_, !lazyIndex_);
  nodes_ = std::move(updated.nodes_);
  indexedDirectories_.clear();
  observedBrowserGeneration_ = current;
}
#endif

void DirectoryMount::tryAddFile(std::string_view path) const {
  const auto key = lookupKey(path, true);
  if (nodes_.count(key))
    return;
  std::error_code ec;
  const auto target = root_ / path;
  if (!std::filesystem::exists(target, ec) ||
      !std::filesystem::is_regular_file(target, ec))
    return;
  const auto real = std::filesystem::canonical(target, ec);
  if (ec || !contained(real, root_))
    return;
  const auto id =
      normalizePath(target.lexically_relative(root_).generic_string());
  const auto actualSize = std::filesystem::file_size(target, ec);
  if (ec)
    return;
  Node entry{
      FileInfo{id, FileType::Regular, static_cast<std::int64_t>(actualSize)},
      target};
  nodes_.emplace(key, std::move(entry));
}

bool DirectoryMount::exists(std::string_view path) const {
#ifdef __EMSCRIPTEN__
  refreshFromBrowser();
#endif
  ensureParentIndexed(path);
  tryAddFile(path);
  return nodes_.count(lookupKey(path, true)) != 0;
}

FileInfo DirectoryMount::stat(std::string_view path) const {
#ifdef __EMSCRIPTEN__
  refreshFromBrowser();
#endif
  ensureParentIndexed(path);
  tryAddFile(path);
  const auto found = nodes_.find(lookupKey(path, true));
  if (found == nodes_.end())
    throw FilesystemError(ErrorCode::NotFound, "File not found in mount");
  return found->second.info;
}

std::vector<FileInfo> DirectoryMount::list(std::string_view directory) const {
#ifdef __EMSCRIPTEN__
  refreshFromBrowser();
#endif
  indexDirectory(directory);
  const auto base = lookupKey(directory, true);
  const auto node = nodes_.find(base);
  if (node == nodes_.end())
    throw FilesystemError(ErrorCode::NotFound, "Directory not found");
  if (node->second.info.type != FileType::Directory)
    throw FilesystemError(ErrorCode::NotDirectory, "Not a directory");

  std::vector<FileInfo> entries;
  for (const auto &[key, value] : nodes_) {
    if (key.empty() || key == base)
      continue;
    const auto pos = key.find_last_of('/');
    const auto parent = pos == std::string::npos ? "" : key.substr(0, pos);
    if (parent == base)
      entries.push_back(value.info);
  }
  return entries;
}

std::unique_ptr<FileHandle> DirectoryMount::open(std::string_view path) const {
#ifdef __EMSCRIPTEN__
  refreshFromBrowser();
#endif
  ensureParentIndexed(path);
  tryAddFile(path);
  const auto found = nodes_.find(lookupKey(path));
  if (found == nodes_.end())
    throw FilesystemError(ErrorCode::NotFound, "File not found");
  if (found->second.info.type == FileType::Directory)
    throw FilesystemError(ErrorCode::IsDirectory, "Cannot open directory");
  std::error_code ec;
  const auto real = std::filesystem::canonical(found->second.physical, ec);
  if (ec || !contained(real, root_))
    throw FilesystemError(ErrorCode::PermissionDenied,
                          "File escaped the mount: " + std::string(path));
  return std::make_unique<SDLFileSource>(real);
}

} // namespace Kotonoha::Filesystem
