// Copyright 2026 Kotonoha contributors. SPDX-License-Identifier: MIT
#include <Kotonoha/Filesystem/GameSourceProbe.hpp>
#include <Ponscripter/PonscripterAssets.hpp>
#include <SDL3/SDL.h>

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <mutex>
#include <set>
#include <stdexcept>
#include <utility>

namespace Ponscripter {
namespace {

class MemoryStream final : public Kotonoha::AssetStream {
public:
  explicit MemoryStream(std::vector<std::uint8_t> bytes)
      : bytes_(std::move(bytes)) {}

  std::uint64_t size() const override { return bytes_.size(); }
  std::uint64_t tell() const override { return cursor_; }
  std::size_t read(Uint8 *buffer, std::size_t count) override {
    if (cursor_ >= bytes_.size())
      return 0;
    const std::size_t avail =
        std::min<std::size_t>(count, bytes_.size() - cursor_);
    std::copy_n(bytes_.data() + cursor_, avail, buffer);
    cursor_ += avail;
    return avail;
  }
  bool seek(std::uint64_t position) override {
    if (position > bytes_.size())
      return false;
    cursor_ = static_cast<std::size_t>(position);
    return true;
  }

private:
  std::vector<std::uint8_t> bytes_;
  std::size_t cursor_ = 0;
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

} // namespace

PonscripterAssets::PonscripterAssets(std::string_view rootDir,
                                     std::filesystem::path overlayDir) {
  if (!overlayDir.empty())
    setOverlay(std::move(overlayDir));
  setRootDirectory(rootDir);
}

void PonscripterAssets::setRootDirectory(std::string_view rootDir) {
  archives_.clear();
  caseIndex_.clear();
  vfs_.reset();
  clearCache();

  if (rootDir.empty()) {
    rootDirectory_.clear();
    return;
  }

  rootDirectory_ = std::filesystem::path(rootDir);
  try {
    vfs_ = Kotonoha::Filesystem::Filesystem::mount(std::string(rootDir));
    for (const auto &info : Kotonoha::Filesystem::listGameSourceFiles(*vfs_)) {
      if (info.type != Kotonoha::Filesystem::FileType::Regular)
        continue;
      const std::string norm = ArchiveReader::normalizePath(info.path);
      caseIndex_[norm] = info.path;

      // Auto-mount arc.nsa, arc1.nsa..arc9.nsa, or arc.sar if present
      const bool isKnownArchive =
          norm == "arc.nsa" || norm == "arc.sar" ||
          (norm.size() == 8 && norm.rfind("arc", 0) == 0 && norm[3] >= '1' &&
           norm[3] <= '9' && norm.substr(4) == ".nsa");
      if (isKnownArchive) {
        try {
          const ArchiveFormat fmt =
              (norm == "arc.sar") ? ArchiveFormat::SAR : ArchiveFormat::NSA;
          mountArchiveFromVfs(info.path, fmt, 0);
        } catch (const std::exception &error) {
          SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                      "PONscripter archive %s unavailable: %s",
                      info.path.c_str(), error.what());
        } catch (...) {
        }
      }
    }
    // The manifest-free browser backend cannot enumerate unknown filenames.
    // Discover common loose scripts and archives by their canonical paths.
    // Indexed directory/ISO sources have already been handled above.
    for (const char *known :
         {"00.utf", "0.utf", "nscript.dat", "pscript.dat", "00.txt", "0.txt",
          "arc.nsa", "arc1.nsa", "arc2.nsa", "arc3.nsa", "arc4.nsa", "arc5.nsa",
          "arc6.nsa", "arc7.nsa", "arc8.nsa", "arc9.nsa", "arc.sar"}) {
      if (caseIndex_.count(known) || !vfs_->exists(known) ||
          vfs_->stat(known).type != Kotonoha::Filesystem::FileType::Regular)
        continue;
      caseIndex_[known] = known;
      const std::string name(known);
      if (name.size() < 4 || (name.substr(name.size() - 4) != ".nsa" &&
                              name.substr(name.size() - 4) != ".sar"))
        continue;
      try {
        mountArchiveFromVfs(known,
                            name.substr(name.size() - 4) == ".sar"
                                ? ArchiveFormat::SAR
                                : ArchiveFormat::NSA,
                            0);
      } catch (const std::exception &error) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                    "PONscripter HTTP archive %s unavailable: %s", known,
                    error.what());
      }
    }
  } catch (const std::exception &error) {
    SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                "PONscripter VFS mount failed: %s", error.what());
    vfs_.reset();
  }
}

void PonscripterAssets::mountArchive(std::vector<std::uint8_t> data,
                                     ArchiveFormat format,
                                     std::size_t nsaOffset) {
  auto reader = std::make_shared<ArchiveReader>();
  if (reader->loadFromMemory(std::move(data), format, nsaOffset)) {
    archives_.push_back(std::move(reader));
  }
}

void PonscripterAssets::mountArchiveFromVfs(const std::string &vfsPath,
                                            ArchiveFormat format,
                                            std::size_t nsaOffset) {
  if (!vfs_)
    return;
  auto handle = std::shared_ptr<Kotonoha::Filesystem::FileHandle>(
      vfs_->open(vfsPath).release());
  if (!handle)
    return;
  const std::int64_t totalSize = handle->size();
  if (totalSize < 0)
    return;
  auto ioMutex = std::make_shared<std::mutex>();
  ArchiveReader::ReadRangeFn readRange =
      [handle = std::move(handle), ioMutex = std::move(ioMutex)](
          std::uint64_t offset, std::uint8_t *dst, std::size_t size) -> bool {
    if (size == 0)
      return true;
    std::lock_guard<std::mutex> lock(*ioMutex);
    if (handle->seek(static_cast<std::int64_t>(offset),
                     Kotonoha::Filesystem::SeekOrigin::Begin) < 0)
      return false;
    std::size_t cursor = 0;
    while (cursor < size) {
      const auto got = handle->read(dst + cursor, size - cursor);
      if (got <= 0)
        return false;
      cursor += static_cast<std::size_t>(got);
    }
    return true;
  };
  auto reader = std::make_shared<ArchiveReader>();
  if (reader->loadFromReader(static_cast<std::uint64_t>(totalSize),
                             std::move(readRange), format, nsaOffset)) {
    archives_.push_back(std::move(reader));
  }
}

std::string
PonscripterAssets::resolveActualPath(std::string_view requestedPath) const {
  const std::string norm = ArchiveReader::normalizePath(requestedPath);
  if (norm.empty())
    return {};

  // 1. Loose VFS file (case-insensitive)
  const auto it = caseIndex_.find(norm);
  if (it != caseIndex_.end())
    return it->second;
  // An unindexed HTTP source may expose the requested loose asset only after
  // a HEAD probe. Do not require it to appear in listGameSourceFiles().
  if (vfs_ && vfs_->exists(norm) &&
      vfs_->stat(norm).type == Kotonoha::Filesystem::FileType::Regular)
    return norm;

  // 2. Mounted NSA/SAR archive
  for (auto ait = archives_.rbegin(); ait != archives_.rend(); ++ait) {
    if ((*ait)->contains(norm))
      return norm;
  }

  // 3. SayaNoUta_PT-BR audio typo fallback: `sound/<subpath>` ->
  // `voice/<subpath>`
  if (norm.rfind("sound/", 0) == 0) {
    const std::string fallbackNorm = "voice/" + norm.substr(6);
    const auto vit = caseIndex_.find(fallbackNorm);
    if (vit != caseIndex_.end())
      return vit->second;
    if (vfs_ && vfs_->exists(fallbackNorm) &&
        vfs_->stat(fallbackNorm).type ==
            Kotonoha::Filesystem::FileType::Regular)
      return fallbackNorm;
    for (auto ait = archives_.rbegin(); ait != archives_.rend(); ++ait) {
      if ((*ait)->contains(fallbackNorm))
        return fallbackNorm;
    }
  }

  return {};
}

std::vector<std::pair<std::string, std::string>>
PonscripterAssets::discoverScripts() const {
  std::vector<std::pair<std::string, std::string>> scripts;

  // Binary-verified PONscripter script search order:
  // 0.utf, 00.utf .. 99.utf (then 0.txt, 00.txt .. 99.txt)
  std::set<std::string> seenNorms;
  for (const char *ext : {"utf", "txt"}) {
    for (int i = 0; i <= 99; ++i) {
      char buf1[32];
      char buf2[32];
      std::snprintf(buf1, sizeof(buf1), "%d.%s", i, ext);
      std::snprintf(buf2, sizeof(buf2), "%02d.%s", i, ext);
      for (const char *cand : {buf1, buf2}) {
        const std::string norm = ArchiveReader::normalizePath(cand);
        if (seenNorms.count(norm))
          continue;
        if (containsAsset(cand)) {
          seenNorms.insert(norm);
          const auto bytes = readAsset(cand);
          std::string text(reinterpret_cast<const char *>(bytes.data()),
                           bytes.size());
          scripts.emplace_back(cand, std::move(text));
        }
      }
    }
    if (!scripts.empty())
      break;
  }
  return scripts;
}

bool PonscripterAssets::containsAsset(const std::string &path) const {
  return !resolveActualPath(path).empty();
}

std::vector<Uint8> PonscripterAssets::readAsset(const std::string &path) const {
  const std::string norm = ArchiveReader::normalizePath(path);
  const auto it = caseIndex_.find(norm);
  if (it != caseIndex_.end() && vfs_) {
    auto handle = vfs_->open(it->second);
    if (handle)
      return Kotonoha::Filesystem::readAll(*handle);
  }
  if (it == caseIndex_.end() && vfs_ && vfs_->exists(norm) &&
      vfs_->stat(norm).type == Kotonoha::Filesystem::FileType::Regular) {
    if (auto handle = vfs_->open(norm))
      return Kotonoha::Filesystem::readAll(*handle);
  }

  for (auto ait = archives_.rbegin(); ait != archives_.rend(); ++ait) {
    if ((*ait)->contains(norm))
      return (*ait)->extract(norm);
  }

  if (norm.rfind("sound/", 0) == 0) {
    const std::string fallbackNorm = "voice/" + norm.substr(6);
    const auto vit = caseIndex_.find(fallbackNorm);
    if (vit != caseIndex_.end() && vfs_) {
      auto handle = vfs_->open(vit->second);
      if (handle)
        return Kotonoha::Filesystem::readAll(*handle);
    }
    if (vit == caseIndex_.end() && vfs_ && vfs_->exists(fallbackNorm) &&
        vfs_->stat(fallbackNorm).type ==
            Kotonoha::Filesystem::FileType::Regular) {
      if (auto handle = vfs_->open(fallbackNorm))
        return Kotonoha::Filesystem::readAll(*handle);
    }
    for (auto ait = archives_.rbegin(); ait != archives_.rend(); ++ait) {
      if ((*ait)->contains(fallbackNorm))
        return (*ait)->extract(fallbackNorm);
    }
  }

  throw std::out_of_range("PonscripterAssets: missing asset " + path);
}

std::vector<std::string>
PonscripterAssets::listAsset(const std::string &prefix) const {
  std::set<std::string> names;
  const std::string lookup = ArchiveReader::normalizePath(prefix);
  for (const auto &[norm, actual] : caseIndex_) {
    (void)actual;
    if (norm.compare(0, lookup.size(), lookup) == 0)
      names.insert(norm);
  }
  for (const auto &arc : archives_) {
    for (const auto &entry : arc->listEntries()) {
      if (entry.compare(0, lookup.size(), lookup) == 0)
        names.insert(entry);
    }
  }
  return {names.begin(), names.end()};
}

std::string PonscripterAssets::localPathAsset(const std::string &path) const {
  if (rootDirectory_.empty())
    return {};
  const std::string norm = ArchiveReader::normalizePath(path);
  const auto it = caseIndex_.find(norm);
  if (it != caseIndex_.end()) {
    const auto candidate = rootDirectory_ / it->second;
    if (std::filesystem::exists(candidate))
      return candidate.string();
  }
  if (norm.rfind("sound/", 0) == 0) {
    const std::string fallbackNorm = "voice/" + norm.substr(6);
    const auto vit = caseIndex_.find(fallbackNorm);
    if (vit != caseIndex_.end()) {
      const auto candidate = rootDirectory_ / vit->second;
      if (std::filesystem::exists(candidate))
        return candidate.string();
    }
  }
  return {};
}

std::unique_ptr<Kotonoha::AssetStream>
PonscripterAssets::openStream(const std::string &path) const {
  const std::string norm = ArchiveReader::normalizePath(path);
  const auto it = caseIndex_.find(norm);
  if (it != caseIndex_.end() && vfs_) {
    auto handle = vfs_->open(it->second);
    if (handle)
      return std::make_unique<VfsStream>(std::move(handle));
  }
  if (it == caseIndex_.end() && vfs_ && vfs_->exists(norm) &&
      vfs_->stat(norm).type == Kotonoha::Filesystem::FileType::Regular) {
    if (auto handle = vfs_->open(norm))
      return std::make_unique<VfsStream>(std::move(handle));
  }
  for (auto ait = archives_.rbegin(); ait != archives_.rend(); ++ait) {
    if ((*ait)->contains(norm))
      return std::make_unique<MemoryStream>((*ait)->extract(norm));
  }
  if (norm.rfind("sound/", 0) == 0) {
    const std::string fallbackNorm = "voice/" + norm.substr(6);
    const auto vit = caseIndex_.find(fallbackNorm);
    if (vit != caseIndex_.end() && vfs_) {
      auto handle = vfs_->open(vit->second);
      if (handle)
        return std::make_unique<VfsStream>(std::move(handle));
    }
    if (vit == caseIndex_.end() && vfs_ && vfs_->exists(fallbackNorm) &&
        vfs_->stat(fallbackNorm).type ==
            Kotonoha::Filesystem::FileType::Regular) {
      if (auto handle = vfs_->open(fallbackNorm))
        return std::make_unique<VfsStream>(std::move(handle));
    }
    for (auto ait = archives_.rbegin(); ait != archives_.rend(); ++ait) {
      if ((*ait)->contains(fallbackNorm))
        return std::make_unique<MemoryStream>((*ait)->extract(fallbackNorm));
    }
  }
  return nullptr;
}

} // namespace Ponscripter
