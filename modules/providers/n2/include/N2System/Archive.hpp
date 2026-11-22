// Copyright 2026 Kotonoha contributors. SPDX-License-Identifier: MIT
#pragma once
#include <cstddef>
#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <string_view>
#include <vector>
namespace N2System {
enum class ArchiveFormat { AFS, NPA, SG_NPA };
struct ArchiveEntry {
  std::string name;
  std::uint64_t offset = 0, size = 0;
};

// Abstract random-access byte source for streaming archive reads without
// loading multi-hundred-megabyte archives into memory.
class ArchiveSource {
public:
  virtual ~ArchiveSource() = default;
  virtual std::uint64_t size() const = 0;
  virtual std::size_t readAt(std::uint64_t offset, std::uint8_t *buffer,
                             std::size_t count) const = 0;
};

// Read-only streaming AFS, classic NPA\x01, and Steins;Gate SG-NPA reader.
// Legacy filename bytes are preserved; lookup folds ASCII and slashes.
class Archive {
public:
  explicit Archive(std::vector<std::uint8_t> bytes);
  explicit Archive(std::shared_ptr<ArchiveSource> source);

  ArchiveFormat format() const { return format_; }
  const std::vector<ArchiveEntry> &entries() const { return entries_; }
  const ArchiveEntry *find(std::string_view name) const;
  bool contains(std::string_view name) const;
  std::vector<std::uint8_t> read(std::string_view name) const;
  std::vector<std::uint8_t> read(const ArchiveEntry &entry) const;
  // Import standard AFS "name,index" aliases; parsed transactionally.
  // Call while configuring mounts, before concurrent runtime reads.
  void addAfsNames(std::string_view ini);
  std::vector<std::string> names() const;
  // Read a range directly from the archive without copying the whole asset.
  // For SG-NPA entries, performs O(1) seekable XOR decryption in place.
  std::size_t readAt(const ArchiveEntry &entry, std::uint64_t offset,
                     std::uint8_t *buffer, std::size_t capacity) const;
  static std::string normalize(std::string_view name);

private:
  void init();
  void parseAfs();
  void parseNpa();
  void parseSgNpa();
  void check(std::uint64_t offset, std::uint64_t size) const;
  std::vector<std::uint8_t> readSource(std::uint64_t offset,
                                       std::size_t size) const;
  void add(ArchiveEntry entry);

  std::shared_ptr<ArchiveSource> source_;
  std::uint64_t totalSize_ = 0;
  ArchiveFormat format_ = ArchiveFormat::AFS;
  std::vector<ArchiveEntry> entries_;
  std::map<std::string, std::size_t> index_;
};
} // namespace N2System
