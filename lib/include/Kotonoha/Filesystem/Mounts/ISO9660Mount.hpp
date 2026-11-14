#pragma once
#include <Kotonoha/Filesystem/IMount.hpp>
#include <filesystem>
#include <map>
#include <memory>
namespace Kotonoha { class DiscImage; }
namespace Kotonoha::Filesystem {
class ISO9660Mount final : public IMount {
  std::shared_ptr<const Kotonoha::DiscImage> disc_;
  std::map<std::string, FileInfo> nodes_;
public:
  explicit ISO9660Mount(const std::filesystem::path &image);
  bool exists(std::string_view path) const override;
  FileInfo stat(std::string_view path) const override;
  std::vector<FileInfo> list(std::string_view directory) const override;
  std::unique_ptr<FileHandle> open(std::string_view path) const override;
};
} // namespace Kotonoha::Filesystem
