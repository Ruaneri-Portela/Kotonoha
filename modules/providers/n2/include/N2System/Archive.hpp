// Copyright 2026 Kotonoha contributors. SPDX-License-Identifier: MIT
#pragma once
#include <cstddef>
#include <cstdint>
#include <map>
#include <string>
#include <string_view>
#include <vector>
namespace N2System {
enum class ArchiveFormat { AFS, NPA };
struct ArchiveEntry {
  std::string name;
  std::uint64_t offset = 0, size = 0;
};
// Read-only, in-memory AFS and uncompressed/unencrypted NPA reader.
// Legacy filename bytes are preserved; lookup folds ASCII and slashes.
class Archive {
public:
  explicit Archive(std::vector<std::uint8_t> bytes);
  ArchiveFormat format() const { return format_; }
  const std::vector<ArchiveEntry>& entries() const { return entries_; }
  const ArchiveEntry* find(std::string_view name) const;
  bool contains(std::string_view name) const;
  std::vector<std::uint8_t> read(std::string_view name) const;
  std::vector<std::uint8_t> read(const ArchiveEntry& entry) const;
  // Import standard AFS "name,index" aliases; parsed transactionally.
  // Call while configuring mounts, before concurrent runtime reads.
  void addAfsNames(std::string_view ini);
  std::vector<std::string> names() const;
  // Read a range directly from the archive without copying the whole asset.
  std::size_t readAt(const ArchiveEntry& entry, std::uint64_t offset,
                     std::uint8_t* buffer, std::size_t capacity) const;
  static std::string normalize(std::string_view name);
private:
  void parseAfs();
  void parseNpa();
  void check(std::uint64_t offset, std::uint64_t size) const;
  std::uint32_t u32(std::size_t offset) const;
  std::uint64_t u64(std::size_t offset) const;
  void add(ArchiveEntry entry);
  std::vector<std::uint8_t> bytes_;
  ArchiveFormat format_ = ArchiveFormat::AFS;
  std::vector<ArchiveEntry> entries_;
  std::map<std::string,std::size_t> index_;
};
} // namespace N2System
