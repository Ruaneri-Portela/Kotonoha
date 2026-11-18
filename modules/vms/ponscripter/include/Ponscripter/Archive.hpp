// Copyright 2026 Kotonoha contributors. SPDX-License-Identifier: MIT
#pragma once

#include <cstddef>
#include <cstdint>
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
  ArchiveReader() = default;

  bool loadFromMemory(std::vector<std::uint8_t> data, ArchiveFormat format,
                      std::size_t nsaOffset = 0);

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
  std::vector<std::uint8_t> data_;
  std::vector<ArchiveEntry> entries_;
  std::unordered_map<std::string, std::size_t> index_;
};

} // namespace Ponscripter
