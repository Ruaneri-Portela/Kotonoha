#pragma once

// LucaSystem (Visual Art's / Key) .PAK archive reader.
//
// The .PAK container (PARAM.PAK, SCRIPT.PAK, SYSSE.PAK and the image
// archives) format is decoded here following wetor/LuckSystem (pak.go).
//
// Layout:
//   Header (36 bytes):
//     uint32 headerLength   total size of the header region
//     uint32 fileCount      number of entries
//     uint32 idStart        first entry id
//     uint32 blockSize      allocation unit; stored offsets are in blocks
//     uint32 unk2..unk5
//     uint32 flags          bit 9 (512) => entries have names
//   Scan forward from offset 32 reading uint32 until a value equals
//   headerLength / blockSize; that position is the offset table.
//   Then fileCount * 8 bytes of (offset uint32, length uint32) pairs
//   (offsets are in block units, multiply by blockSize).
//   If named, a pointer at offsetTable - 4 leads to null-terminated
//   file names, one per entry, in order.
//   File data lives at offset * blockSize.

#include <Kotonoha/core/AssetProvider.hpp>

#include <cstdint>
#include <filesystem>
#include <map>
#include <string>
#include <vector>

namespace LucaSystem {

// A single entry inside a LucaSystem .PAK archive.
struct PakEntry {
  std::string name;
  uint32_t id = 0;
  uint32_t index = 0;
  uint64_t offset = 0; // actual file offset (offset * blockSize)
  uint32_t length = 0;
};

// Reader for one LucaSystem .PAK archive.
class PakArchive {
public:
  explicit PakArchive(std::filesystem::path path);

  const std::filesystem::path &path() const { return path_; }
  uint32_t fileCount() const { return header_.fileCount; }
  uint32_t idStart() const { return header_.idStart; }
  uint32_t blockSize() const { return header_.blockSize; }
  bool named() const { return named_; }

  const std::vector<PakEntry> &entries() const { return entries_; }

  // Look up an entry by name / id / index. Returns nullptr when missing.
  const PakEntry *findByName(std::string_view name) const;
  const PakEntry *findById(uint32_t id) const;
  const PakEntry *findByIndex(uint32_t index) const;

  // Read an entry's complete contents.
  std::vector<std::uint8_t> read(const PakEntry &entry) const;
  std::vector<std::uint8_t> read(std::string_view name) const;

private:
  struct Header {
    uint32_t headerLength = 0;
    uint32_t fileCount = 0;
    uint32_t idStart = 0;
    uint32_t blockSize = 0;
    uint32_t unk2 = 0, unk3 = 0, unk4 = 0, unk5 = 0;
    uint32_t flags = 0;
  };

  std::filesystem::path path_;
  // Only the header is retained. Multi-gigabyte voice/image PAKs are read
  // lazily when an entry is requested.
  std::vector<std::uint8_t> headerData_;
  Header header_;
  size_t offsetPos = 0;
  bool named_ = false;
  std::vector<PakEntry> entries_;
  std::map<std::string, size_t> byName_;
  std::map<uint32_t, size_t> byId_;
  void load();
};

// AssetProvider that mounts a LucaSystem game directory and resolves logical
// asset ids to entries across the mounted .PAK archives (PARAM.PAK,
// SCRIPT.PAK, SYSSE.PAK and any image archives found in the directory).
class LucaSystemAssets : public Kotonoha::AssetProvider {
public:
  LucaSystemAssets(std::filesystem::path directory,
                   std::filesystem::path overlayDirectory);

  std::string name() const override { return "LucaSystem"; }
  bool containsAsset(const std::string &id) const override;
  std::vector<std::uint8_t> readAsset(const std::string &id) const override;
  std::vector<std::uint8_t> readAsset(uint32_t id) const;
  std::vector<std::uint8_t> readImageAsset(uint32_t id) const;
  std::vector<std::string>
  listAsset(const std::string &prefix = {}) const override;
  std::string localPathAsset(const std::string &id) const override;

  const PakArchive *scriptPak() const { return scriptPak_.get(); }
  const PakArchive *paramPak() const { return paramPak_.get(); }
  const PakArchive *syssePak() const { return syssePak_.get(); }

private:
  std::filesystem::path directory_;
  std::unique_ptr<PakArchive> paramPak_;
  std::unique_ptr<PakArchive> scriptPak_;
  std::unique_ptr<PakArchive> syssePak_;
  // All mounted archives are kept alive here so that idToPak_ pointers stay
  // valid (image archives are not referenced by a named slot).
  std::vector<std::unique_ptr<PakArchive>> archives_;
  // Logical id -> owning archive. Multiple archives may reuse ids; the first
  // mount wins (PARAM.PAK, then SCRIPT.PAK, then SYSSE.PAK, then images).
  std::map<std::string, const PakArchive *> idToPak_;
  std::map<uint32_t, const PakArchive *> numericIdToPak_;
  // Numeric ids are only unique inside a resource class. In particular AIR
  // reuses low ids in SCRIPT, fonts, BGCG and VOICE, so drawing must never use
  // the generic first-mounted numeric map.
  std::map<uint32_t, const PakArchive *> imageIdToPak_;
  void mountPak(const std::filesystem::path &file);
};

} // namespace LucaSystem
