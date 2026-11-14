#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace Kotonoha::PmailPort {

struct PackEntry {
  std::string name;
  uint16_t id = 0;         // script object ID in index record
  uint32_t offset = 0;     // file offset of entry data
  uint32_t size = 0;       // stored size in pack
  uint32_t unpacked = 0;   // unpacked size (0 if not compressed/unknown)
  uint8_t flags1 = 0;      // byte[2]
  uint8_t flags2 = 0;      // byte[3] (bit0 => LZ stream, bit1 => prefixed LZ)
};

class PmailPack {
public:
  bool open(const std::string &path);
  // Parse the same archive bytes already loaded by the virtual filesystem.
  // The caller owns data for the lifetime of PmailPack.
  bool openBytes(const std::vector<uint8_t> &data,
                 const std::string &logicalPath);
  const std::vector<PackEntry> &entries() const { return entries_; }
  std::vector<uint8_t> read(const std::string &name) const;
  std::vector<uint8_t> read(const PackEntry &e) const;
  bool ok() const { return ok_; }

  static bool decodeLz(std::vector<uint8_t> &dst, size_t dstSize,
                       const uint8_t *src, size_t srcSize);

private:
  bool parseIndex(const std::vector<uint8_t> &indexData);

  std::string path_;
  const std::vector<uint8_t> *backing_ = nullptr;
  std::vector<uint8_t> ownedBacking_;
  std::vector<PackEntry> entries_;
  bool ok_ = false;
};

} // namespace Kotonoha::PmailPort
