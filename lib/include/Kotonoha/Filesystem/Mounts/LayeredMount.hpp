#pragma once
#include <Kotonoha/Filesystem/IMount.hpp>
#include <cstdint>
#include <memory>
#include <vector>
namespace Kotonoha::Filesystem {
class LayeredMount final : public IMount {
  struct Layer {
    std::shared_ptr<IMount> mount;
    int priority = 0;
    std::uint64_t order = 0;
    bool overlay = false;
  };
  std::vector<Layer> layers_;
  std::uint64_t sequence_ = 0;
  std::uint64_t generation_ = 0;
  bool blocked(std::size_t layerIndex, const std::string &path) const;
  const Layer *visible(const std::string &path, bool overlayOnly) const;
public:
  explicit LayeredMount(std::shared_ptr<IMount> base);
  void addOverlay(std::shared_ptr<IMount> overlay, int priority = 100);
  std::uint64_t generation() const noexcept { return generation_; }
  bool isKnownRegularFile(std::string_view path) const override;
  bool isKnownDirectory(std::string_view path) const override;
  bool exists(std::string_view path) const override;
  FileInfo stat(std::string_view path) const override;
  std::vector<FileInfo> list(std::string_view directory) const override;
  std::unique_ptr<FileHandle> open(std::string_view path) const override;
  std::unique_ptr<FileHandle> tryOpenLogicalOverride(std::string_view id) const;
};
} // namespace Kotonoha::Filesystem
