// Copyright 2026 Kotonoha contributors. SPDX-License-Identifier: MIT
#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace Ponscripter {

enum class ArchiveFormat : std::uint8_t { SAR = 0, NSA = 1 };

struct ArchiveEntry {
  std::string originalName;
  std::string normalizedPath; // forward-slash, lowercased
  std::uint8_t compression = 0; // 0=none, 1=SPB, 2=LZSS, 4=NBZ
  std::uint64_t offset = 0;     // absolute byte offset inside archive
  std::uint32_t compressedSize = 0;
  std::uint32_t uncompressedSize = 0;
};

class ArchiveReader {
public:
  using ReadRangeFn =
      std::function<bool(std::uint64_t offset, std::uint8_t *dst,
                         std::size_t size)>;

  ArchiveReader() = default;

  bool loadFromMemory(std::vector<std::uint8_t> data, ArchiveFormat format,
                      std::size_t nsaOffset = 0);
  // Indexes only the SAR/NSA table of contents and streams entries on demand
  // through `reader`, avoiding loading multi-hundred-MB archives into RAM.
  bool loadFromReader(std::uint64_t totalSize, ReadRangeFn reader,
                      ArchiveFormat format, std::size_t nsaOffset = 0);

  bool contains(std::string_view virtualPath) const;
  const ArchiveEntry *find(std::string_view virtualPath) const;
  std::vector<std::uint8_t> extract(std::string_view virtualPath) const;
  std::vector<std::uint8_t> extract(const ArchiveEntry &entry) const;
  std::vector<std::string> listEntries() const;

  static std::string normalizePath(std::string_view raw);

  // Helper to build synthetic SAR/NSA archives in tests.
  static std::vector<std::uint8_t>
  buildSyntheticSar(const std::vector<std::pair<std::string, std::vector<std::uint8_t>>> &files);
  static std::vector<std::uint8_t>
  buildSyntheticNsa(const std::vector<std::pair<std::string, std::vector<std::uint8_t>>> &files);

private:
  bool parseToc(const std::uint8_t *toc, std::size_t tocSize,
                std::uint64_t totalSize, ArchiveFormat format,
                std::size_t nsaOffset);
  std::vector<std::uint8_t> extractRaw(const ArchiveEntry &entry,
                                       const std::uint8_t *src,
                                       std::size_t srcLen) const;

  std::vector<std::uint8_t> data_;
  ReadRangeFn reader_;
  std::uint64_t totalSize_ = 0;
  std::vector<ArchiveEntry> entries_;
  std::unordered_map<std::string, std::size_t> index_;
};

} // namespace Ponscripter
