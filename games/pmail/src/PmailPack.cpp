#include "PmailPack.hpp"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <fstream>
#include <iostream>

namespace Kotonoha::PmailPort {

namespace {
std::string lower(std::string s) {
  std::transform(s.begin(), s.end(), s.begin(),
                 [](unsigned char c) { return char(std::tolower(c)); });
  return s;
}
uint32_t rd32(const uint8_t *p) {
  return uint32_t(p[0]) | (uint32_t(p[1]) << 8) | (uint32_t(p[2]) << 16) |
         (uint32_t(p[3]) << 24);
}
} // namespace

bool PmailPack::decodeLz(std::vector<uint8_t> &dst, size_t dstSize,
                         const uint8_t *src, size_t srcSize) {
  std::cout << "[PmailPack] decodeLz dst=" << dstSize << " src=" << srcSize
            << std::endl;
  dst.assign(dstSize, 0);
  uint8_t ring[0x1000] = {};
  std::memset(ring, 0, sizeof(ring));
  size_t si = 0, di = 0, ri = 0xFEE;
  uint8_t ctl = 0; uint8_t bit = 0;
  while (di < dstSize) {
    if (bit == 0) {
      if (si >= srcSize) {
        std::cout << "[PmailPack] decodeLz fail flag" << std::endl;
        return false;
      }
      ctl = src[si++];
      bit = 0x80;
    }
    if (ctl & bit) {
      if (si + 2 > srcSize) {
        std::cout << "[PmailPack] decodeLz fail match" << std::endl;
        return false;
      }
      uint8_t lo = src[si++];
      uint8_t hi = src[si++];
      uint16_t off = (uint16_t(hi) << 4) | (uint16_t(lo) >> 4);
      uint16_t len = uint16_t(lo & 0x0F) + 3;
      for (uint16_t k = 0; k < len && di < dstSize; ++k) {
        uint8_t b = ring[off & 0xFFF];
        dst[di++] = b;
        ring[ri & 0xFFF] = b;
        ri = (ri + 1) & 0xFFF;
        off = (off + 1) & 0xFFF;
      }
    } else {
      if (si >= srcSize) {
        std::cout << "[PmailPack] decodeLz fail lit" << std::endl;
        return false;
      }
      uint8_t b = src[si++];
      dst[di++] = b;
      ring[ri & 0xFFF] = b;
      ri = (ri + 1) & 0xFFF;
    }
    bit >>= 1;
  }
  std::cout << "[PmailPack] decodeLz ok" << std::endl;
  return true;
}

bool PmailPack::open(const std::string &path) {
  path_ = path;
  entries_.clear();
  ok_ = false;
  std::ifstream in(path, std::ios::binary | std::ios::ate);
  if (!in)
    return false;
  const auto sz = in.tellg();
  if (sz < 12)
    return false;
  in.seekg(-12, std::ios::end);
  uint8_t foot[12];
  if (!in.read(reinterpret_cast<char *>(foot), 12))
    return false;
  auto x = [&](int i) { return rd32(foot + i) ^ 0xF0F0F0F0u; };
  const uint32_t compIndexSize = x(0);
  const uint32_t uncompIndexSize = x(4);
  // x(8) unknown for now
  if (compIndexSize == 0 || compIndexSize + 12u > uint64_t(sz))
    return false;
  const std::streamoff indexOff = sz - std::streamoff(12) - std::streamoff(compIndexSize);
  if (indexOff < 0)
    return false;
  in.seekg(indexOff, std::ios::beg);
  std::vector<uint8_t> comp(compIndexSize);
  if (!in.read(reinterpret_cast<char *>(comp.data()), comp.size()))
    return false;

  std::vector<uint8_t> index;
  bool decoded = false;
  if (uncompIndexSize > 0 && uncompIndexSize < (32u << 20)) {
    decoded = decodeLz(index, uncompIndexSize, comp.data(), comp.size());
    if (!decoded) {
      // try with slack
      decoded = decodeLz(index, uncompIndexSize + 0x400, comp.data(), comp.size());
    }
  }
  if (!decoded) {
    std::cerr << "[PmailPack] Failed to decode index: " << path << std::endl;
    // Fallback: keep raw comp as index bytes for best-effort scan
    index = comp;
  } else {
    std::cout << "[PmailPack] Decoded index: " << path << " -> " << index.size()
              << " bytes" << std::endl;
  }

  ok_ = parseIndex(index);
  if (!ok_ || entries_.empty()) {
    std::cerr << "[PmailPack] Warning: no entries parsed from " << path << std::endl;
  } else {
    std::cout << "[PmailPack] Parsed " << entries_.size() << " entries from " << path
              << std::endl;
  }
  return ok_;
}

bool PmailPack::parseIndex(const std::vector<uint8_t> &idx) {
  entries_.clear();
  if (idx.size() < 0x50)
    return false;

  // Best-effort: scan for plausible 0x50-byte entry records containing a
  // null-terminated ASCII name near offset +4 (observed in script.dat).
  // Record layout guess (from RE and decoded data): [0..3] header/flags,
  // [4..] name, [0x44] offset, [0x48] size, [0x4c] unpacked.
  const size_t rec = 0x50;
  auto tryAdd = [&](size_t base) {
    if (base + rec > idx.size()) return;
    const uint8_t *e = idx.data() + base;
    // name at +4
    size_t n0 = base + 4;
    size_t n1 = n0;
    while (n1 < base + 0x44 && n1 < idx.size() && idx[n1] != 0 &&
           idx[n1] >= 0x20 && idx[n1] < 0x7f)
      ++n1;
    if (n1 <= n0 || n1 >= base + 0x44) return;
    std::string name(reinterpret_cast<const char *>(idx.data() + n0), n1 - n0);
    if (name.find('.') == std::string::npos) return; // expect extension
    PackEntry pe;
    pe.name = name;
    pe.id = uint16_t(e[0] | (uint16_t(e[1]) << 8));
    pe.flags1 = e[2];
    pe.flags2 = e[3];
    pe.offset = rd32(e + 0x44);
    pe.size = rd32(e + 0x48);
    pe.unpacked = rd32(e + 0x4c);
    // basic sanity
    if (pe.offset == 0 && pe.size == 0) return;
    // avoid obvious garbage
    if (pe.size > (512u << 20)) return;
    entries_.push_back(std::move(pe));
  };

  // Try aligned scan
  for (size_t b = 0; b + rec <= idx.size(); b += rec)
    tryAdd(b);

  if (entries_.empty()) {
    // Fallback: scan any position where a plausible filename appears at +4
    for (size_t b = 0; b + rec <= idx.size(); ++b) {
      const uint8_t *e = idx.data() + b;
      if (e[4] < '0' && e[4] < 'A') continue;
      // quick check for "."
      bool hasDot = false;
      for (size_t i = 4; i < 0x30; ++i) {
        uint8_t c = e[i];
        if (c == 0) break;
        if (c == '.') { hasDot = true; break; }
        if (c < 0x20 || c >= 0x7f) { hasDot = false; break; }
      }
      if (!hasDot) continue;
      tryAdd(b);
    }
  }

  // Deduplicate by lowercase name keeping first
  std::vector<PackEntry> uniq;
  for (auto &e : entries_) {
    bool dup = false;
    for (auto &u : uniq) if (lower(u.name) == lower(e.name)) { dup = true; break; }
    if (!dup) uniq.push_back(std::move(e));
  }
  entries_.swap(uniq);
  return !entries_.empty();
}

std::vector<uint8_t> PmailPack::read(const std::string &name) const {
  std::string want = lower(name);
  for (const auto &e : entries_) {
    if (lower(e.name) == want)
      return read(e);
  }
  // try adding .obj
  if (want.find('.') == std::string::npos) {
    want += ".obj";
    for (const auto &e : entries_) if (lower(e.name) == want) return read(e);
  }
  return {};
}

std::vector<uint8_t> PmailPack::read(const PackEntry &e) const {
  std::ifstream in(path_, std::ios::binary);
  if (!in)
    return {};
  in.seekg(e.offset, std::ios::beg);
  if (!in)
    return {};
  std::vector<uint8_t> stored(e.size);
  if (e.size == 0)
    return {};
  if (!in.read(reinterpret_cast<char *>(stored.data()), stored.size()))
    return {};

  // Script records with flag 1 store an LZ stream without a size prefix. The
  // index's final word gives its decoded length (start.obj: 2728 -> 6184).
  if ((e.flags2 & 0x01) && e.unpacked > 0 && e.unpacked <= (64u << 20)) {
    std::vector<uint8_t> out;
    if (decodeLz(out, e.unpacked, stored.data(), stored.size()))
      return out;
    std::cerr << "[PmailPack] LZ decode failed for " << e.name << std::endl;
    return {};
  }

  // Some records use a four-byte big-endian decoded-length prefix.
  if ((e.flags2 & 0x02) && stored.size() > 4) {
    uint32_t usz = (uint32_t(stored[0]) << 24) | (uint32_t(stored[1]) << 16) |
                   (uint32_t(stored[2]) << 8) | uint32_t(stored[3]);
    if (usz == 0 || usz > (64u << 20))
      usz = e.unpacked ? e.unpacked : (e.size * 4);
    std::vector<uint8_t> out;
    if (decodeLz(out, usz, stored.data() + 4, stored.size() - 4))
      return out;
    std::cerr << "[PmailPack] LZ decode failed for " << e.name << std::endl;
    return {};
  }
  // Uncompressed record.
  return stored;
}

} // namespace Kotonoha::PmailPort
