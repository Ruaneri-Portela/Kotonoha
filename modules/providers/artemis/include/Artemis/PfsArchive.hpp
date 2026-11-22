// Copyright 2026 Kotonoha contributors. SPDX-License-Identifier: MIT
#pragma once

#include <Kotonoha/Filesystem/FileHandle.hpp>
#include <Kotonoha/core/AssetProvider.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace Artemis {

struct PfsEntry {
  std::string path; // Normalized UTF-8 path using '/'
  std::uint64_t offset = 0;
  std::uint64_t size = 0;
};

// Streaming reader for Artemis Engine PFS archives (.pfs, .pfs.000..999).
// Supports unencrypted (pf6 and earlier) and SHA-1 XOR-encrypted (pf8+) packs.
class PfsArchive : public std::enable_shared_from_this<PfsArchive> {
public:
  static std::array<std::uint8_t, 20> sha1(const std::uint8_t *data,
                                           std::size_t size);

  // Opens and parses the directory table from a Kotonoha VFS FileHandle.
  // Returns nullptr if the file is not a valid PFS archive.
  static std::shared_ptr<PfsArchive>
  open(std::unique_ptr<Kotonoha::Filesystem::FileHandle> handle,
       std::string label = {});

  int version() const { return version_; }
  bool encrypted() const { return encrypted_; }
  const std::string &label() const { return label_; }
  const std::array<std::uint8_t, 20> &xorKey() const { return xorKey_; }

  const std::vector<PfsEntry> &entries() const { return entries_; }
  const PfsEntry *find(std::string_view normalizedLowerPath) const;

  // Thread-safe O(1) random-access read with on-the-fly pf8 XOR decryption.
  std::size_t readAt(const PfsEntry &entry, std::uint64_t relOffset,
                     std::uint8_t *dst, std::size_t count) const;

  // Reads an entire entry into memory (decrypting if pf8+).
  std::vector<std::uint8_t> readEntry(const PfsEntry &entry) const;

  // Opens a seekable, zero-copy streaming AssetStream backed by this archive.
  std::unique_ptr<Kotonoha::AssetStream>
  openStream(const PfsEntry &entry) const;

  static std::string normalizePath(std::string_view path);
  static std::string toLowerAscii(std::string_view text);

private:
  PfsArchive() = default;

  std::unique_ptr<Kotonoha::Filesystem::FileHandle> handle_;
  mutable std::mutex ioMutex_;
  std::string label_;
  int version_ = 0;
  bool encrypted_ = false;
  std::array<std::uint8_t, 20> xorKey_{};
  std::vector<PfsEntry> entries_;
  std::unordered_map<std::string, std::size_t> indexByLower_;
};

} // namespace Artemis
