#pragma once
#include <Kotonoha/Filesystem/Mounts/LayeredMount.hpp>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <vector>
namespace Kotonoha::Filesystem {
class Filesystem final {
  std::shared_ptr<LayeredMount> layers_;
  explicit Filesystem(std::shared_ptr<IMount> base);
public:
  static std::shared_ptr<Filesystem> mount(std::string source);
  void addOverlay(std::string source, int priority = 100);
  std::uint64_t generation() const noexcept;
  bool exists(std::string_view path) const;
  FileInfo stat(std::string_view path) const;
  std::vector<FileInfo> list(std::string_view directory = {}) const;
  std::unique_ptr<FileHandle> open(std::string_view path) const;
  std::unique_ptr<FileHandle> tryOpenLogicalOverride(std::string_view id) const;
};
std::vector<std::uint8_t> readAll(FileHandle &file,
                                  std::size_t maxBytes = 1024u * 1024u * 1024u);
} // namespace Kotonoha::Filesystem
