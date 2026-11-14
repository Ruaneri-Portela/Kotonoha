#pragma once
#include <Kotonoha/Filesystem/FileHandle.hpp>
#include <cstdint>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace Kotonoha::Filesystem {
enum class FileType { Regular, Directory };
enum class ErrorCode {
  NotFound, NotDirectory, IsDirectory, InvalidPath,
  PermissionDenied, UnsupportedMount, CorruptImage, IOError,
  WouldBlock, NetworkError, RangeUnsupported
};
class FilesystemError : public std::runtime_error {
  ErrorCode code_;
public:
  FilesystemError(ErrorCode code, const std::string &message)
      : std::runtime_error(message), code_(code) {}
  ErrorCode code() const noexcept { return code_; }
};
struct FileInfo {
  std::string path;
  FileType type = FileType::Regular;
  std::int64_t size = -1;
};
class IMount {
public:
  virtual ~IMount() = default;
  // This is a metadata-only, non-networking check for ancestor shadowing.
  // Normal local mounts can use their indexed exists/stat implementation;
  // HTTP mounts override it to inspect known metadata without starting HEAD.
  virtual bool isKnownRegularFile(std::string_view path) const {
    return exists(path) && stat(path).type == FileType::Regular;
  }
  virtual bool isKnownDirectory(std::string_view path) const {
    return exists(path) && stat(path).type == FileType::Directory;
  }
  // An unindexed HTTP backend cannot validate size/type against local
  // files; prefer a user-supplied entry rather than probing the server.
  virtual bool preferLocalWhenPresent() const noexcept { return false; }
  virtual bool exists(std::string_view path) const = 0;
  virtual FileInfo stat(std::string_view path) const = 0;
  virtual std::vector<FileInfo> list(std::string_view directory) const = 0;
  virtual std::unique_ptr<FileHandle> open(std::string_view path) const = 0;
};
} // namespace Kotonoha::Filesystem
