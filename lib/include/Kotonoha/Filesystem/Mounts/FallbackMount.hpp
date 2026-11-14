#pragma once
#include <Kotonoha/Filesystem/IMount.hpp>
#include <memory>
namespace Kotonoha::Filesystem {
// Prefer a valid local entry. When a manifest-backed remote source reports
// an entry and its local counterpart has the wrong type/size, use the remote
// entry. Neither absence nor corrupt local content is silently treated as EOF.
// Invalid local paths are never sent to the network.
class FallbackMount final : public IMount {
  std::shared_ptr<IMount> local_;
  std::shared_ptr<IMount> remote_;
  const IMount *select(const std::string &path) const;
public:
  FallbackMount(std::shared_ptr<IMount> local,
                std::shared_ptr<IMount> remote);
  bool exists(std::string_view path) const override;
  FileInfo stat(std::string_view path) const override;
  std::vector<FileInfo> list(std::string_view dir) const override;
  std::unique_ptr<FileHandle> open(std::string_view path) const override;
};
} // namespace Kotonoha::Filesystem
