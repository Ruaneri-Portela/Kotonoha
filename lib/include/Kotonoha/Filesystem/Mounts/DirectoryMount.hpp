#pragma once
#include <Kotonoha/Filesystem/IMount.hpp>
#include <filesystem>
#include <map>
namespace Kotonoha::Filesystem {
class DirectoryMount final : public IMount {
  struct Node {
    FileInfo info;
    std::filesystem::path physical;
  };
  std::filesystem::path root_;
  mutable std::map<std::string, Node> nodes_;
#ifdef __EMSCRIPTEN__
  mutable int observedBrowserGeneration_ = 0;
  // Browser folder imports invalidate the immutable constructor-time index.
  void refreshFromBrowser() const;
#endif
public:
  explicit DirectoryMount(const std::filesystem::path &root);
  bool exists(std::string_view path) const override;
  FileInfo stat(std::string_view path) const override;
  std::vector<FileInfo> list(std::string_view directory) const override;
  std::unique_ptr<FileHandle> open(std::string_view path) const override;
};
} // namespace Kotonoha::Filesystem
