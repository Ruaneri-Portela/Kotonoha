#include <Kotonoha/Filesystem/Mounts/FallbackMount.hpp>
#include <Kotonoha/Filesystem/Path.hpp>
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
  const bool haveRemote = remote_ && remote_->exists(path);
  if (local_ && local_->exists(path)) {
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
