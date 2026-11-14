#pragma once

#include <Kotonoha/Filesystem/IMount.hpp>

#include <filesystem>
#include <functional>
#include <map>

namespace Kotonoha::Filesystem {

// Read-only V7/USTAR/PAX/GNU-longname TAR mount. Entries are streamed by
// offsets; the full archive is never unpacked or loaded into RAM.
class TarMount final : public IMount {
  struct Node {
    FileInfo info;
    std::uint64_t offset = 0;
  };
  std::function<std::unique_ptr<FileHandle>()> source_;
  std::map<std::string, Node> nodes_;

public:
  explicit TarMount(const std::filesystem::path &archive);
  explicit TarMount(std::function<std::unique_ptr<FileHandle>()> openSource);
  bool exists(std::string_view path) const override;
  FileInfo stat(std::string_view path) const override;
  std::vector<FileInfo> list(std::string_view directory) const override;
  std::unique_ptr<FileHandle> open(std::string_view path) const override;
};

} // namespace Kotonoha::Filesystem
