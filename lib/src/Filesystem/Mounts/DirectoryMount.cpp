#include <Kotonoha/Filesystem/Mounts/DirectoryMount.hpp>
#include <Kotonoha/Filesystem/Path.hpp>
#include <Kotonoha/Filesystem/Sources/SDLFileSource.hpp>

#include <limits>
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
} // namespace

DirectoryMount::DirectoryMount(const std::filesystem::path &source) {
  std::error_code ec;
  root_ = std::filesystem::canonical(source, ec);
  if (ec || !std::filesystem::is_directory(root_))
    throw FilesystemError(ErrorCode::NotDirectory,
                          "Mount is not a local directory: " + source.string());
  nodes_.emplace("", Node{FileInfo{"", FileType::Directory, 0}, root_});

  std::filesystem::recursive_directory_iterator it(root_, ec), end;
  if (ec)
    throw FilesystemError(ErrorCode::IOError,
                          "Cannot enumerate mount directory: " + root_.string());
  for (; it != end; it.increment(ec)) {
    if (ec)
      throw FilesystemError(ErrorCode::IOError, "Cannot enumerate mount");
    const auto id = normalizePath(
        it->path().lexically_relative(root_).generic_string());
    const auto key = lookupKey(id);
    const auto real = std::filesystem::canonical(it->path(), ec);
    if (ec || !contained(real, root_))
      throw FilesystemError(ErrorCode::InvalidPath,
                            "Mount entry escapes its directory: " + id);
    if (std::filesystem::is_symlink(it->symlink_status(ec)) &&
        std::filesystem::is_directory(it->status(ec)))
      throw FilesystemError(ErrorCode::UnsupportedMount,
                            "Directory symlink is unsupported: " + id);
    const bool directory = it->is_directory(ec);
    const bool regular = it->is_regular_file(ec);
    if (ec || (!directory && !regular))
      throw FilesystemError(ErrorCode::UnsupportedMount,
                            "Unsupported mount entry: " + id);
    std::int64_t size = 0;
    if (regular) {
      const auto actualSize = it->file_size(ec);
      if (ec || actualSize > static_cast<std::uintmax_t>(INT64_MAX))
        throw FilesystemError(ErrorCode::IOError,
                              "Invalid file size: " + id);
      size = static_cast<std::int64_t>(actualSize);
    }
    Node entry{FileInfo{id, directory ? FileType::Directory : FileType::Regular,
                        size}, it->path()};
    if (!nodes_.emplace(key, std::move(entry)).second)
      throw FilesystemError(ErrorCode::InvalidPath,
                            "Ambiguous case-insensitive mount entry: " + id);
  }
  if (ec)
    throw FilesystemError(ErrorCode::IOError, "Cannot enumerate mount");
}

#ifdef __EMSCRIPTEN__
void DirectoryMount::refreshFromBrowser() const {
  // A virtual folder can be mounted after game startup. The browser changes
  // this generation only after a successful filesystem import.
  const int current = EM_ASM_INT({
    return Module["KotonohaVfsGeneration"] | 0;
  });
  if (current == observedBrowserGeneration_)
    return;
  DirectoryMount updated(root_);
  nodes_ = std::move(updated.nodes_);
  observedBrowserGeneration_ = current;
}
#endif

bool DirectoryMount::exists(std::string_view path) const {
#ifdef __EMSCRIPTEN__
  refreshFromBrowser();
#endif
  return nodes_.count(lookupKey(path, true)) != 0;
}

FileInfo DirectoryMount::stat(std::string_view path) const {
#ifdef __EMSCRIPTEN__
  refreshFromBrowser();
#endif
  const auto found = nodes_.find(lookupKey(path, true));
  if (found == nodes_.end())
    throw FilesystemError(ErrorCode::NotFound, "File not found in mount");
  return found->second.info;
}

std::vector<FileInfo> DirectoryMount::list(std::string_view directory) const {
#ifdef __EMSCRIPTEN__
  refreshFromBrowser();
#endif
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

std::unique_ptr<FileHandle>
DirectoryMount::open(std::string_view path) const {
#ifdef __EMSCRIPTEN__
  refreshFromBrowser();
#endif
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
