#pragma once
#include <cstddef>
#include <cstdint>

namespace Kotonoha::Filesystem {
enum class SeekOrigin { Begin, Current, End };

// Read-only owning cursor; 0 means EOF, -1 means I/O error.
class FileHandle {
public:
  virtual ~FileHandle() = default;
  virtual std::int64_t read(void *destination, std::size_t count) = 0;
  virtual std::int64_t seek(std::int64_t offset, SeekOrigin origin) = 0;
  virtual std::int64_t tell() const = 0;
  virtual std::int64_t size() const = 0;
};
} // namespace Kotonoha::Filesystem
