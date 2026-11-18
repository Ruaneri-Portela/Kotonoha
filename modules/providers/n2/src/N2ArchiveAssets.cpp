// Copyright 2026 Kotonoha contributors. SPDX-License-Identifier: MIT
#include <Kotonoha/Filesystem/GameSourceProbe.hpp>
#include <N2System/N2ArchiveAssets.hpp>
#include <algorithm>
#include <cctype>
#include <set>
#include <stdexcept>
#include <utility>

namespace N2System {
namespace {
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
  vfs_.reset();
  if (!rootDir.empty()) {
    rootDirectory_ = std::filesystem::path(rootDir);
    try {
      vfs_ = Kotonoha::Filesystem::Filesystem::mount(std::string(rootDir));
      for (const auto &info :
           Kotonoha::Filesystem::listGameSourceFiles(*vfs_)) {
        if (info.type != Kotonoha::Filesystem::FileType::Regular)
          continue;
        std::string lower = info.path;
        std::transform(lower.begin(), lower.end(), lower.begin(),
                       [](unsigned char c) { return char(std::tolower(c)); });
        if (lower.size() > 4 &&
            (lower.compare(lower.size() - 4, 4, ".npa") == 0 ||
             lower.compare(lower.size() - 4, 4, ".afs") == 0)) {
          try {
            auto handle = vfs_->open(info.path);
            if (handle)
              mount(Kotonoha::Filesystem::readAll(*handle));
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

void N2ArchiveAssets::addAfsNames(std::string_view ini) {
  if (archives_.empty())
    throw std::runtime_error("N2System: mount an AFS archive first");
  archives_.back()->addAfsNames(ini);
}

bool N2ArchiveAssets::containsAsset(const std::string &path) const {
  for (auto it = archives_.rbegin(); it != archives_.rend(); ++it)
    if ((*it)->contains(path))
      return true;
  if (vfs_) {
    const auto norm = normalizeVfsId(path);
    if (!norm.empty() && vfs_->exists(norm) &&
        vfs_->stat(norm).type == Kotonoha::Filesystem::FileType::Regular)
      return true;
  }
  return false;
}

std::vector<Uint8> N2ArchiveAssets::readAsset(const std::string &path) const {
  for (auto it = archives_.rbegin(); it != archives_.rend(); ++it)
    if ((*it)->contains(path))
      return (*it)->read(path);
  if (vfs_) {
    const auto norm = normalizeVfsId(path);
    if (!norm.empty() && vfs_->exists(norm) &&
        vfs_->stat(norm).type == Kotonoha::Filesystem::FileType::Regular) {
      auto handle = vfs_->open(norm);
      if (handle)
        return Kotonoha::Filesystem::readAll(*handle);
    }
  }
  throw std::out_of_range("N2System: missing asset " + path);
}

std::vector<std::string>
N2ArchiveAssets::listAsset(const std::string &prefix) const {
  std::set<std::string> names;
  const auto lookup = Archive::normalize(prefix);
  for (const auto &archive : archives_)
    for (const auto &name : archive->names())
      if (name.compare(0, lookup.size(), lookup) == 0)
        names.insert(name);
  if (vfs_) {
    for (const auto &entry : Kotonoha::Filesystem::listGameSourceFiles(*vfs_)) {
      if (entry.type != Kotonoha::Filesystem::FileType::Regular)
        continue;
      const auto norm = Archive::normalize(entry.path);
      if (norm.compare(0, lookup.size(), lookup) == 0)
        names.insert(norm);
    }
  }
  return {names.begin(), names.end()};
}

std::unique_ptr<Kotonoha::AssetStream>
N2ArchiveAssets::openStream(const std::string &path) const {
  for (auto it = archives_.rbegin(); it != archives_.rend(); ++it) {
    const auto *entry = (*it)->find(path);
    if (entry)
      return std::make_unique<ArchiveStream>(*it, *entry);
  }
  if (vfs_) {
    const auto norm = normalizeVfsId(path);
    if (!norm.empty() && vfs_->exists(norm) &&
        vfs_->stat(norm).type == Kotonoha::Filesystem::FileType::Regular) {
      auto handle = vfs_->open(norm);
      if (handle)
        return std::make_unique<VfsStream>(std::move(handle));
    }
  }
  return nullptr;
}

} // namespace N2System
