#pragma once

#include <Kotonoha/Filesystem/FileHandle.hpp>
#include <Kotonoha/Filesystem/Filesystem.hpp>

#include <cstdint>
#include <filesystem>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace Kotonoha::Providers {

// Reader for Overflow Large PonPon's GCPackFile (.DAT) archive format.
// Sector-aligned (512B), 256-bucket index at 0x000..0x3FF, 16-byte records at
// 0x400.. with 6-bit packed ASCII filenames and u32 startSector.
class GcPackArchive {
public:
  struct Entry {
    std::string name; // Uppercase canonical "STEM.EXT"
    uint64_t offset = 0;
    uint64_t size = 0;
    uint32_t startSector = 0;
  };

  GcPackArchive() = default;

  // Open an archive from a Kotonoha VFS FileHandle (streams reads on demand).
  bool open(std::unique_ptr<Kotonoha::Filesystem::FileHandle> file);

  // Open an archive from an in-memory byte buffer (for unit tests).
  bool openBytes(std::vector<uint8_t> bytes);

  // Quick header/sentinel probe without retaining the handle.
  static bool probe(Kotonoha::Filesystem::FileHandle &file);
  static bool probeBytes(const uint8_t *data, size_t size);

  // Decode 12-byte 6-bit packed filename into "STEM.EXT".
  static std::string decodePackedName(const uint8_t packed[12]);

  // Encode "STEM.EXT" into 12-byte 6-bit packed filename.
  static void encodePackedName(const std::string &name, uint8_t out[12]);

  bool isOpen() const { return !entries_.empty(); }
  const std::vector<Entry> &entries() const { return entries_; }

  std::optional<Entry> find(const std::string &name) const;
  bool contains(const std::string &name) const;
  std::vector<uint8_t> read(const std::string &name) const;

private:
  bool parseIndex(const uint8_t *indexData, size_t indexSize,
                  uint64_t totalFileSize);
  static std::string normalizeKey(const std::string &name);

  mutable std::mutex mutex_;
  std::unique_ptr<Kotonoha::Filesystem::FileHandle> file_;
  std::vector<uint8_t> memory_;
  std::vector<Entry> entries_;
  std::unordered_map<std::string, size_t> lookup_;
};

// Detects whether a mounted filesystem or directory contains Large PonPon
// (MAINSYS.INI + GCPackFile PACK/script.dat).
bool isLargePonPonSource(const Kotonoha::Filesystem::Filesystem &fs);
bool isLargePonPonDirectory(const std::filesystem::path &directory);

} // namespace Kotonoha::Providers
