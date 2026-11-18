#pragma once

#include <Kotonoha/Filesystem/Filesystem.hpp>

#include <initializer_list>
#include <cstddef>
#include <vector>
#include <optional>
#include <string>
#include <string_view>

namespace Kotonoha::Filesystem {

// Probe a known filename without relying on directory enumeration. HTTP(S)
// Fetch mounts cannot list undiscovered remote files, but can resolve known
// relative paths through HEAD/Range metadata requests.
inline bool isGameSourceRegularFile(const Filesystem &source,
                                    std::string_view path) {
  return source.exists(path) && source.stat(path).type == FileType::Regular;
}

// Locate a group of required regular files in a mounted game source.
// The lookup uses the same case-insensitive, layered virtual filesystem as
// the asset providers, including ISO images. A directory with the right name
// is NOT accepted as a required file. I/O exceptions intentionally propagate.
inline std::optional<std::string> findRequiredFilesPrefix(
    const Filesystem &source,
    std::initializer_list<std::string_view> required,
    std::initializer_list<std::string_view> prefixes = {""}) {
  if (required.size() == 0)
    return std::nullopt;
  for (const auto prefix : prefixes) {
    bool complete = true;
    for (const auto name : required) {
      if (name.empty()) {
        complete = false;
        break;
      }
      const std::string id = std::string(prefix) + std::string(name);
      if (!source.exists(id) ||
          source.stat(id).type != FileType::Regular) {
        complete = false;
        break;
      }
    }
    if (complete)
      return std::string(prefix);
  }
  return std::nullopt;
}


// Enumerate regular files through the public VFS, including nested folders.
// DirectoryMount, ISO and overlay mounts expose only immediate children from
// list(directory). This bounded traversal keeps the callers independent of
// physical filesystem APIs and avoids treating folder names as archives.
// Depth 0 means only root files, and default depth 4 covers common nss/ and
// data/archive/ layouts without descending unlimited trees.
inline std::vector<FileInfo> listGameSourceFiles(
    const Filesystem &source, unsigned maxDepth = 4,
    std::size_t maxEntries = 65536) {
  struct PendingDirectory {
    std::string path;
    unsigned depth;
  };
  std::vector<PendingDirectory> pending{{"", 0}};
  std::vector<FileInfo> files;
  std::size_t examined = 0;
  for (std::size_t cursor = 0; cursor < pending.size(); ++cursor) {
    for (const auto &entry : source.list(pending[cursor].path)) {
      if (++examined > maxEntries)
        throw FilesystemError(ErrorCode::IOError,
                              "Game source exceeds bounded VFS traversal");
      if (entry.type == FileType::Regular)
        files.push_back(entry);
      else if (entry.type == FileType::Directory &&
               pending[cursor].depth < maxDepth)
        pending.push_back({entry.path, pending[cursor].depth + 1});
    }
  }
  return files;
}

} // namespace Kotonoha::Filesystem
