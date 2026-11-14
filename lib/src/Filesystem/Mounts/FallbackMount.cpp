#include <Kotonoha/Filesystem/Mounts/FallbackMount.hpp>
#include <Kotonoha/Filesystem/Path.hpp>
#include <Kotonoha/Filesystem/Sources/FetchAssetProvider.hpp>
#include <algorithm>
#include <map>
#include <stdexcept>
namespace Kotonoha::Filesystem {
FallbackMount::FallbackMount(std::shared_ptr<IMount> local,
                             std::shared_ptr<IMount> remote)
    : local_(std::move(local)), remote_(std::move(remote)) {
  if (!local_ && !remote_)
    throw std::invalid_argument("FallbackMount needs a source");
}
const IMount *FallbackMount::select(const std::string &path) const {
  // Without a manifest there is no authoritative remote file size to compare.
  // Prefer a valid local file immediately instead of probing every local asset
  // over HTTP merely to determine whether the remote copy differs.
  const bool localExists = local_ && local_->exists(path);
  if (localExists) {
    if (remote_ && remote_->preferLocalWhenPresent())
      return local_.get();
  }
  const bool haveRemote = remote_ && remote_->exists(path);
  if (localExists) {
    const auto candidate = local_->stat(path);
    if (!haveRemote)
      return local_.get();
    const auto expected = remote_->stat(path);
    if (candidate.type == expected.type &&
        (candidate.type == FileType::Directory ||
         candidate.size == expected.size))
      return local_.get();
  }
  return haveRemote ? remote_.get() : nullptr;
}
bool FallbackMount::isKnownRegularFile(std::string_view path) const {
  const auto key = normalizePath(path, true);
  const bool localExists = local_ && local_->exists(key);
  if (localExists) {
    const auto localInfo = local_->stat(key);
    const auto direct = std::dynamic_pointer_cast<FetchAssetProvider>(remote_);
    // In manifest-free mode any local entry takes precedence, including a
    // local directory. No HTTP discovery can occur in this metadata query.
    if (direct && direct->manifestFree())
      return localInfo.type == FileType::Regular;
    if (localInfo.type == FileType::Regular)
      return true;
  }
  return remote_ && remote_->isKnownRegularFile(key);
}
bool FallbackMount::exists(std::string_view path) const {
  return select(normalizePath(path, true)) != nullptr;
}
FileInfo FallbackMount::stat(std::string_view path) const {
  const auto key = normalizePath(path, true);
  const auto *source = select(key);
  if (!source)
    throw FilesystemError(ErrorCode::NotFound, "Asset not in local or remote mount: " + key);
  return source->stat(key);
}
std::vector<FileInfo> FallbackMount::list(std::string_view dir) const {
  const auto key = normalizePath(dir, true);
  if (stat(key).type != FileType::Directory)
    throw FilesystemError(ErrorCode::NotDirectory, "Not a game-assets directory: " + key);
  std::map<std::string, FileInfo> result;
  for (const auto *source : {remote_.get(), local_.get()}) {
    if (!source || !source->exists(key) ||
        source->stat(key).type != FileType::Directory)
      continue;
    for (const auto &entry : source->list(key)) {
      if (select(normalizePath(entry.path)) == source)
        result[lookupKey(entry.path)] = entry;
    }
  }
  std::vector<FileInfo> out;
  out.reserve(result.size());
  for (const auto &[name, entry] : result)
    out.push_back(entry);
  return out;
}
std::unique_ptr<FileHandle> FallbackMount::open(std::string_view path) const {
  const auto key = normalizePath(path);
  const auto *source = select(key);
  if (!source)
    throw FilesystemError(ErrorCode::NotFound, "Asset not in local or remote mount: " + key);
  if (source->stat(key).type != FileType::Regular)
    throw FilesystemError(ErrorCode::IsDirectory, "Cannot open directory: " + key);
  return source->open(key);
}
} // namespace Kotonoha::Filesystem
