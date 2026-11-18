// Copyright 2026 Kotonoha contributors. SPDX-License-Identifier: MIT
#include <N2System/N2ArchiveAssets.hpp>
#include <algorithm>
#include <fstream>
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
} // namespace

N2ArchiveAssets::N2ArchiveAssets(std::string_view rootDir) {
  setRootDirectory(rootDir);
}

void N2ArchiveAssets::setRootDirectory(std::string_view rootDir) {
  if (!rootDir.empty()) {
    rootDirectory_ = std::filesystem::path(rootDir);
  } else {
    rootDirectory_.clear();
  }
}

std::filesystem::path N2ArchiveAssets::resolveFsPath(std::string_view path) const {
  if (rootDirectory_.empty())
    return {};
  std::string normPath(path);
  for (char &c : normPath) {
    if (c == '\\') c = '/';
  }
  auto target = rootDirectory_ / normPath;
  if (std::filesystem::is_regular_file(target))
    return target;
  return {};
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

  if (!resolveFsPath(path).empty())
    return true;

  return false;
}

std::vector<Uint8> N2ArchiveAssets::readAsset(const std::string &path) const {
  for (auto it = archives_.rbegin(); it != archives_.rend(); ++it)
    if ((*it)->contains(path))
      return (*it)->read(path);

  auto fsPath = resolveFsPath(path);
  if (!fsPath.empty()) {
    std::ifstream ifs(fsPath, std::ios::binary);
    if (ifs) {
      return std::vector<Uint8>((std::istreambuf_iterator<char>(ifs)),
                                std::istreambuf_iterator<char>());
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

  if (!rootDirectory_.empty() && std::filesystem::is_directory(rootDirectory_)) {
    for (const auto &entry : std::filesystem::recursive_directory_iterator(rootDirectory_)) {
      if (entry.is_regular_file()) {
        auto rel = std::filesystem::relative(entry.path(), rootDirectory_).generic_string();
        if (rel.compare(0, lookup.size(), lookup) == 0)
          names.insert(rel);
      }
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
  return nullptr;
}

} // namespace N2System
