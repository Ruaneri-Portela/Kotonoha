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
  PermissionDenied, UnsupportedMount, CorruptImage, IOError
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
  virtual bool exists(std::string_view path) const = 0;
  virtual FileInfo stat(std::string_view path) const = 0;
  virtual std::vector<FileInfo> list(std::string_view directory) const = 0;
  virtual std::unique_ptr<FileHandle> open(std::string_view path) const = 0;
};
} // namespace Kotonoha::Filesystem
