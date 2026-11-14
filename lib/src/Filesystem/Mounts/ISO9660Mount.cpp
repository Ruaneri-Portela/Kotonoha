#include <Kotonoha/Filesystem/Mounts/ISO9660Mount.hpp>
#include <Kotonoha/Filesystem/Path.hpp>
#include <Kotonoha/core/DiscImage.hpp>

#include <algorithm>
#include <cstring>
#include <limits>
#include <stdexcept>

namespace Kotonoha::Filesystem {
namespace {

class ISOFileHandle final : public FileHandle {
  std::shared_ptr<const Kotonoha::DiscImage> disc_;
  Kotonoha::DiscImage::Entry entry_;
  std::uint64_t cursor_ = 0;

public:
  ISOFileHandle(std::shared_ptr<const Kotonoha::DiscImage> disc,
                const Kotonoha::DiscImage::Entry &entry)
      : disc_(std::move(disc)), entry_(entry) {
    if (entry_.size > static_cast<std::uint64_t>(INT64_MAX))
      throw FilesystemError(ErrorCode::IOError,
                            "ISO entry exceeds signed 64-bit length");
  }

  std::int64_t read(void *destination, std::size_t count) override {
    if (!count)
      return 0;
    if (!destination)
      return -1;
    if (cursor_ >= entry_.size)
      return 0;
    const auto chunk = static_cast<std::size_t>(
        std::min<std::uint64_t>({entry_.size - cursor_, count, 1024u * 1024u}));
    std::vector<Uint8> bytes;
    if (!disc_->readInto(entry_, cursor_, chunk, bytes) ||
        bytes.size() != chunk)
      return -1;
    std::memcpy(destination, bytes.data(), chunk);
    cursor_ += chunk;
    return static_cast<std::int64_t>(chunk);
  }

  std::int64_t seek(std::int64_t offset, SeekOrigin origin) override {
    const auto base = origin == SeekOrigin::Begin
                          ? std::uint64_t{0}
                          : origin == SeekOrigin::Current ? cursor_ : entry_.size;
    std::uint64_t next;
    if (offset < 0) {
      const auto magnitude =
          static_cast<std::uint64_t>(-(offset + 1)) + 1;
      if (magnitude > base)
        return -1;
      next = base - magnitude;
    } else {
      if (static_cast<std::uint64_t>(offset) > entry_.size - base)
        return -1;
      next = base + static_cast<std::uint64_t>(offset);
    }
    if (next > entry_.size)
      return -1;
    cursor_ = next;
    return static_cast<std::int64_t>(cursor_);
  }

  std::int64_t tell() const override {
    return static_cast<std::int64_t>(cursor_);
  }
  std::int64_t size() const override {
    return static_cast<std::int64_t>(entry_.size);
  }
};

std::string parentName(const std::string &name) {
  const auto slash = name.find_last_of('/');
  return slash == std::string::npos ? "" : name.substr(0, slash);
}

} // namespace

ISO9660Mount::ISO9660Mount(const std::filesystem::path &image) {
  auto disc = std::make_shared<Kotonoha::DiscImage>();
  std::string error;
  if (!disc->tryOpen(image, &error))
    throw FilesystemError(ErrorCode::CorruptImage,
                          "Unsupported/corrupt ISO image: " + error);
  disc_ = std::move(disc);
  nodes_.emplace("", FileInfo{"", FileType::Directory, 0});

  const auto addDirectory = [&](const std::string &name) {
    auto path = normalizePath(name);
    if (!nodes_.emplace(lookupKey(path), FileInfo{path, FileType::Directory, 0})
             .second)
      throw FilesystemError(ErrorCode::CorruptImage,
                            "Duplicate ISO directory: " + path);
  };
  for (const auto &path : disc_->directories())
    addDirectory(path);
  for (const auto &entry : disc_->files()) {
    const auto name = normalizePath(entry.path);
    if (entry.size > static_cast<std::uint64_t>(INT64_MAX))
      throw FilesystemError(ErrorCode::CorruptImage,
                            "ISO file too large: " + name);
    if (!nodes_.emplace(
             lookupKey(name),
             FileInfo{name, FileType::Regular,
                      static_cast<std::int64_t>(entry.size)}).second)
      throw FilesystemError(ErrorCode::CorruptImage,
                            "Duplicate ISO file: " + name);
  }
}

bool ISO9660Mount::exists(std::string_view path) const {
  return nodes_.count(lookupKey(path, true)) != 0;
}

FileInfo ISO9660Mount::stat(std::string_view path) const {
  const auto it = nodes_.find(lookupKey(path, true));
  if (it == nodes_.end())
    throw FilesystemError(ErrorCode::NotFound, "ISO entry not found");
  return it->second;
}

std::vector<FileInfo> ISO9660Mount::list(std::string_view directory) const {
  const auto root = lookupKey(directory, true);
  const auto node = nodes_.find(root);
  if (node == nodes_.end())
    throw FilesystemError(ErrorCode::NotFound, "ISO directory not found");
  if (node->second.type != FileType::Directory)
    throw FilesystemError(ErrorCode::NotDirectory, "Not an ISO directory");
  std::vector<FileInfo> entries;
  for (const auto &[key, entry] : nodes_)
    if (!key.empty() && key != root &&
        parentName(key) == root)
      entries.push_back(entry);
  return entries;
}

std::unique_ptr<FileHandle> ISO9660Mount::open(std::string_view path) const {
  const auto name = normalizePath(path);
  const auto info = stat(name);
  if (info.type != FileType::Regular)
    throw FilesystemError(ErrorCode::IsDirectory, "Not an ISO file");
  const auto *entry = disc_->find(name);
  if (!entry)
    throw FilesystemError(ErrorCode::NotFound, "ISO entry not found: " + name);
  return std::make_unique<ISOFileHandle>(disc_, *entry);
}

} // namespace Kotonoha::Filesystem
