#pragma once
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace ZeroNoTsukaima {

// Read-only access to the 2048-byte-sector data track of the disc. Accepts a
// MODE1/2048 .bin, a MODE1/2352 .bin or a plain .iso.
class Disc {
public:
  struct Entry {
    std::string name; // ISO name without the ";1" version suffix
    uint32_t lba = 0;
    uint32_t size = 0;
  };

  Disc();
  ~Disc();
  Disc(const Disc &) = delete;
  Disc &operator=(const Disc &) = delete;

  bool Open(const std::string &path, std::string *error);
  const std::vector<Entry> &Files() const { return files; }
  const Entry *Find(const std::string &name) const;
  bool Read(const Entry &entry, std::vector<uint8_t> *out) const;
  bool ReadRange(const Entry &entry, uint64_t offset, uint64_t size,
                 std::vector<uint8_t> *out) const;
  std::string VolumeLabel() const { return label; }

private:
  struct File;
  std::unique_ptr<File> file;
  std::vector<Entry> files;
  std::string label;
  uint32_t sectorSize = 2048;
  uint32_t dataOffset = 0;

  bool ReadSectors(uint32_t lba, uint32_t count, std::vector<uint8_t> *out) const;
  bool Walk(uint32_t lba, uint32_t size, const std::string &prefix,
            std::string *error);
};

// NAME.HD is an array of u32 entry sizes; NAME.BIN stores entry i at the next
// 2048-byte boundary after entry i-1.
struct ArchiveEntry {
  uint64_t offset = 0;
  uint32_t size = 0;
};

std::vector<ArchiveEntry> ParseArchiveIndex(const std::vector<uint8_t> &hd);

} // namespace ZeroNoTsukaima
