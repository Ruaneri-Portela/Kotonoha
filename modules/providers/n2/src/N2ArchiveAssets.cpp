// Copyright 2026 Kotonoha contributors. SPDX-License-Identifier: MIT
#include <Kotonoha/Filesystem/GameSourceProbe.hpp>
#include <N2System/N2ArchiveAssets.hpp>
#include <algorithm>
#include <cctype>
#include <mutex>
#include <set>
#include <stdexcept>
#include <utility>

namespace N2System {
namespace {

class VfsArchiveSource final : public ArchiveSource {
public:
  explicit VfsArchiveSource(
      std::unique_ptr<Kotonoha::Filesystem::FileHandle> handle)
      : handle_(std::move(handle)) {
    const auto s = handle_ ? handle_->size() : 0;
    size_ = s > 0 ? static_cast<std::uint64_t>(s) : 0;
  }

  std::uint64_t size() const override { return size_; }

  std::size_t readAt(std::uint64_t offset, std::uint8_t *buffer,
                     std::size_t count) const override {
    if (!handle_ || offset >= size_ || count == 0 || !buffer)
      return 0;
    std::lock_guard<std::mutex> lock(mutex_);
    if (handle_->seek(static_cast<std::int64_t>(offset),
                      Kotonoha::Filesystem::SeekOrigin::Begin) < 0)
      return 0;
    const auto got = handle_->read(buffer, count);
    return got > 0 ? static_cast<std::size_t>(got) : 0;
  }

private:
  mutable std::mutex mutex_;
  std::unique_ptr<Kotonoha::Filesystem::FileHandle> handle_;
  std::uint64_t size_ = 0;
};

class ArchiveStream final : public Kotonoha::AssetStream {
public:
  ArchiveStream(std::shared_ptr<const Archive> archive, ArchiveEntry entry)
      : archive_(std::move(archive)), entry_(std::move(entry)) {}

  std::uint64_t size() const override { return entry_.size; }
  std::uint64_t tell() const override { return cursor_; }
  std::size_t read(Uint8 *buffer, std::size_t count) override {
    const auto received = archive_->readAt(entry_, cursor_, buffer, count);
    cursor_ += received;
    return received;
  }
  bool seek(std::uint64_t position) override {
    if (position > entry_.size)
      return false;
    cursor_ = position;
    return true;
  }

private:
  std::shared_ptr<const Archive> archive_;
  ArchiveEntry entry_;
  std::uint64_t cursor_ = 0;
};

class VfsStream final : public Kotonoha::AssetStream {
public:
  explicit VfsStream(std::unique_ptr<Kotonoha::Filesystem::FileHandle> handle)
      : handle_(std::move(handle)) {}

  std::uint64_t size() const override {
    const auto s = handle_->size();
    return s > 0 ? static_cast<std::uint64_t>(s) : 0;
  }
  std::uint64_t tell() const override {
    const auto t = handle_->tell();
    return t > 0 ? static_cast<std::uint64_t>(t) : 0;
  }
  std::size_t read(Uint8 *buffer, std::size_t count) override {
    const auto received = handle_->read(buffer, count);
    return received > 0 ? static_cast<std::size_t>(received) : 0;
  }
  bool seek(std::uint64_t position) override {
    return handle_->seek(static_cast<std::int64_t>(position),
                         Kotonoha::Filesystem::SeekOrigin::Begin) >= 0;
  }

private:
  std::unique_ptr<Kotonoha::Filesystem::FileHandle> handle_;
};

std::string normalizeVfsId(std::string_view raw) {
  std::string out(raw);
  std::replace(out.begin(), out.end(), '\\', '/');
  while (!out.empty() && out.front() == '/')
    out.erase(out.begin());
  return out;
}

} // namespace

N2ArchiveAssets::N2ArchiveAssets(std::string_view rootDir,
                                 std::filesystem::path overlayDir) {
  if (!overlayDir.empty())
    setOverlay(std::move(overlayDir));
  setRootDirectory(rootDir);
}

void N2ArchiveAssets::setRootDirectory(std::string_view rootDir) {
  // Changing the source must not retain indexed archives from a prior game.
  archives_.clear();
  looseFiles_.clear();
  vfs_.reset();
  if (!rootDir.empty()) {
    rootDirectory_ = std::filesystem::path(rootDir);
    try {
      vfs_ = Kotonoha::Filesystem::Filesystem::mount(std::string(rootDir));
      for (const auto &info :
           Kotonoha::Filesystem::listGameSourceFiles(*vfs_)) {
        if (info.type != Kotonoha::Filesystem::FileType::Regular)
          continue;
        std::string lower = Archive::normalize(normalizeVfsId(info.path));
        looseFiles_.emplace(lower, info.path);
        if (lower.size() > 4 &&
            (lower.compare(lower.size() - 4, 4, ".npa") == 0 ||
             lower.compare(lower.size() - 4, 4, ".afs") == 0)) {
          try {
            auto handle = vfs_->open(info.path);
            if (handle)
              mount(std::make_shared<VfsArchiveSource>(std::move(handle)));
          } catch (...) {
          }
        }
      }
    } catch (...) {
      vfs_.reset();
    }
  } else {
    rootDirectory_.clear();
  }
}

void N2ArchiveAssets::mount(std::vector<std::uint8_t> data) {
  auto archive = std::make_shared<Archive>(std::move(data));
  archives_.push_back(std::move(archive));
}

void N2ArchiveAssets::mount(std::shared_ptr<ArchiveSource> source) {
  auto archive = std::make_shared<Archive>(std::move(source));
  archives_.push_back(std::move(archive));
}

void N2ArchiveAssets::addAfsNames(std::string_view ini) {
  if (archives_.empty())
    throw std::runtime_error("N2System: mount an AFS archive first");
  archives_.back()->addAfsNames(ini);
}

std::string N2ArchiveAssets::resolveLoosePath(std::string_view path) const {
  if (!vfs_)
    return {};
  const auto norm = normalizeVfsId(path);
  if (norm.empty())
    return {};
  const auto lower = Archive::normalize(norm);
  if (auto it = looseFiles_.find(lower); it != looseFiles_.end())
    return it->second;
  if (vfs_->exists(norm) &&
      vfs_->stat(norm).type == Kotonoha::Filesystem::FileType::Regular)
    return norm;
  return {};
}

bool N2ArchiveAssets::containsAsset(const std::string &path) const {
  const auto norm = normalizeVfsId(path);
  for (auto it = archives_.rbegin(); it != archives_.rend(); ++it)
    if ((*it)->contains(norm))
      return true;
  return !resolveLoosePath(norm).empty();
}

std::vector<Uint8> N2ArchiveAssets::readAsset(const std::string &path) const {
  const auto norm = normalizeVfsId(path);
  for (auto it = archives_.rbegin(); it != archives_.rend(); ++it)
    if ((*it)->contains(norm))
      return (*it)->read(norm);
  const auto loose = resolveLoosePath(norm);
  if (!loose.empty()) {
    auto handle = vfs_->open(loose);
    if (handle)
      return Kotonoha::Filesystem::readAll(*handle);
  }
  throw std::out_of_range("N2System: missing asset " + path);
}

std::vector<std::string>
N2ArchiveAssets::listAsset(const std::string &prefix) const {
  std::set<std::string> names;
  const auto lookup = Archive::normalize(normalizeVfsId(prefix));
  for (const auto &archive : archives_)
    for (const auto &name : archive->names())
      if (name.compare(0, lookup.size(), lookup) == 0)
        names.insert(name);
  for (const auto &[norm, actual] : looseFiles_) {
    (void)actual;
    if (norm.compare(0, lookup.size(), lookup) == 0)
      names.insert(norm);
  }
  return {names.begin(), names.end()};
}

std::unique_ptr<Kotonoha::AssetStream>
N2ArchiveAssets::openStream(const std::string &path) const {
  const auto norm = normalizeVfsId(path);
  for (auto it = archives_.rbegin(); it != archives_.rend(); ++it) {
    const auto *entry = (*it)->find(norm);
    if (entry)
      return std::make_unique<ArchiveStream>(*it, *entry);
  }
  const auto loose = resolveLoosePath(norm);
  if (!loose.empty()) {
    auto handle = vfs_->open(loose);
    if (handle)
      return std::make_unique<VfsStream>(std::move(handle));
  }
  return nullptr;
}

} // namespace N2System
