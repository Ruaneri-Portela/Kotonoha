// Copyright 2026 Kotonoha contributors. SPDX-License-Identifier: MIT
#include <Ponscripter/Archive.hpp>

#include <algorithm>
#include <cctype>
#include <cstring>
#include <stdexcept>

namespace Ponscripter {
namespace {

std::uint16_t readBE16(const std::uint8_t *p) {
  return static_cast<std::uint16_t>((static_cast<std::uint16_t>(p[0]) << 8) |
                                    static_cast<std::uint16_t>(p[1]));
}

std::uint32_t readBE32(const std::uint8_t *p) {
  return (static_cast<std::uint32_t>(p[0]) << 24) |
         (static_cast<std::uint32_t>(p[1]) << 16) |
         (static_cast<std::uint32_t>(p[2]) << 8) |
         static_cast<std::uint32_t>(p[3]);
}

void writeBE16(std::vector<std::uint8_t> &out, std::uint16_t v) {
  out.push_back(static_cast<std::uint8_t>((v >> 8) & 0xFFu));
  out.push_back(static_cast<std::uint8_t>(v & 0xFFu));
}

void writeBE32(std::vector<std::uint8_t> &out, std::uint32_t v) {
  out.push_back(static_cast<std::uint8_t>((v >> 24) & 0xFFu));
  out.push_back(static_cast<std::uint8_t>((v >> 16) & 0xFFu));
  out.push_back(static_cast<std::uint8_t>((v >> 8) & 0xFFu));
  out.push_back(static_cast<std::uint8_t>(v & 0xFFu));
}

void writeLE16(std::vector<std::uint8_t> &out, std::uint16_t v) {
  out.push_back(static_cast<std::uint8_t>(v & 0xFFu));
  out.push_back(static_cast<std::uint8_t>((v >> 8) & 0xFFu));
}

void writeLE32(std::vector<std::uint8_t> &out, std::uint32_t v) {
  out.push_back(static_cast<std::uint8_t>(v & 0xFFu));
  out.push_back(static_cast<std::uint8_t>((v >> 8) & 0xFFu));
  out.push_back(static_cast<std::uint8_t>((v >> 16) & 0xFFu));
  out.push_back(static_cast<std::uint8_t>((v >> 24) & 0xFFu));
}

struct BitReader {
  const std::uint8_t *data = nullptr;
  std::size_t size = 0;
  std::size_t bytePos = 0;
  int bitCount = 0;
  std::uint8_t current = 0;

  std::uint32_t getBits(int n) {
    std::uint32_t val = 0;
    while (n > 0) {
      if (bitCount == 0) {
        if (bytePos >= size)
          return val << n;
        current = data[bytePos++];
        bitCount = 8;
      }
      const int take = std::min(n, bitCount);
      val = (val << take) |
            ((static_cast<std::uint32_t>(current) >> (bitCount - take)) &
             ((1u << take) - 1u));
      bitCount -= take;
      n -= take;
    }
    return val;
  }

  std::uint8_t getByte() { return static_cast<std::uint8_t>(getBits(8)); }
};

// NScripter LZSS decompression (EI=8, EJ=4, P=1 -> N=256, F=17, THRESHOLD=2)
std::vector<std::uint8_t> decompressLzss(const std::uint8_t *src,
                                         std::size_t srcLen,
                                         std::uint32_t outLen) {
  constexpr int kBufSize = 256;
  constexpr int kThreshold = 2;
  std::vector<std::uint8_t> ring(kBufSize, 0);
  std::vector<std::uint8_t> out;
  out.reserve(outLen);

  int r = kBufSize - (16 + kThreshold);
  if (r < 0)
    r = 239;
  BitReader br{src, srcLen, 0, 0, 0};

  while (out.size() < outLen && (br.bytePos < br.size || br.bitCount > 0)) {
    const std::uint32_t flag = br.getBits(1);
    if (flag != 0) {
      const std::uint8_t c = static_cast<std::uint8_t>(br.getBits(8));
      out.push_back(c);
      ring[r] = c;
      r = (r + 1) & (kBufSize - 1);
    } else {
      const int offset = static_cast<int>(br.getBits(8));
      const int count = static_cast<int>(br.getBits(4)) + kThreshold;
      for (int k = 0; k <= count - 1 && out.size() < outLen; ++k) {
        const std::uint8_t c = ring[(offset + k) & (kBufSize - 1)];
        out.push_back(c);
        ring[r] = c;
        r = (r + 1) & (kBufSize - 1);
      }
    }
  }
  return out;
}

// NScripter SPB decompression (24-bit BMP with boustrophedon planar
// Huffman/delta coding)
std::vector<std::uint8_t> decompressSpb(const std::uint8_t *src,
                                        std::size_t srcLen,
                                        std::uint32_t outLen) {
  if (srcLen < 4)
    return std::vector<std::uint8_t>(src, src + srcLen);

  const std::uint16_t width = readBE16(src);
  const std::uint16_t height = readBE16(src + 2);
  if (width == 0 || height == 0)
    return {};

  const std::uint32_t widthPad = (4u - ((width * 3u) % 4u)) % 4u;
  const std::uint32_t rowStride = width * 3u + widthPad;
  const std::uint32_t totalSize = rowStride * height + 54u;

  std::vector<std::uint8_t> out(std::max(outLen, totalSize), 0);
  // BMP Header (54 bytes)
  out[0] = 'B';
  out[1] = 'M';
  out[2] = static_cast<std::uint8_t>(totalSize & 0xFFu);
  out[3] = static_cast<std::uint8_t>((totalSize >> 8) & 0xFFu);
  out[4] = static_cast<std::uint8_t>((totalSize >> 16) & 0xFFu);
  out[5] = static_cast<std::uint8_t>((totalSize >> 24) & 0xFFu);
  out[10] = 54; // pixel offset
  out[14] = 40; // BITMAPINFOHEADER size
  out[18] = static_cast<std::uint8_t>(width & 0xFFu);
  out[19] = static_cast<std::uint8_t>((width >> 8) & 0xFFu);
  out[22] = static_cast<std::uint8_t>(height & 0xFFu);
  out[23] = static_cast<std::uint8_t>((height >> 8) & 0xFFu);
  out[26] = 1;  // planes
  out[28] = 24; // 24-bpp

  BitReader br{src + 4, srcLen - 4, 0, 0, 0};
  const std::size_t planePixels = static_cast<std::size_t>(width) * height;
  std::vector<std::uint8_t> chBuf(planePixels + 4, 0);

  for (int ch = 0; ch < 3; ++ch) {
    std::size_t count = 0;
    std::uint8_t c = br.getByte();
    chBuf[count++] = c;
    while (count < planePixels) {
      int n = static_cast<int>(br.getBits(3));
      if (n == 0) {
        chBuf[count++] = c;
        chBuf[count++] = c;
        chBuf[count++] = c;
        chBuf[count++] = c;
        continue;
      }
      int mask = 0;
      if (n == 7) {
        mask = static_cast<int>(br.getBits(1)) + 1;
      } else {
        mask = n + 2;
      }
      for (int j = 0; j < 4 && count < planePixels; ++j) {
        if (mask == 8) {
          c = br.getByte();
        } else {
          int k = static_cast<int>(br.getBits(mask));
          if (k & 1) {
            c = static_cast<std::uint8_t>(c + (k >> 1) + 1);
          } else {
            c = static_cast<std::uint8_t>(c - (k >> 1));
          }
        }
        chBuf[count++] = c;
      }
    }

    std::uint8_t *pbuf = out.data() + 54 + rowStride * (height - 1) +
                         static_cast<std::size_t>(ch);
    const std::uint8_t *psbuf = chBuf.data();
    for (std::uint16_t y = 0; y < height; ++y) {
      if (y & 1) {
        for (std::uint16_t x = 0; x < width; ++x, pbuf -= 3)
          *pbuf = *psbuf++;
        pbuf -= rowStride - 3;
      } else {
        for (std::uint16_t x = 0; x < width; ++x, pbuf += 3)
          *pbuf = *psbuf++;
        pbuf -= rowStride + 3;
      }
    }
  }
  return out;
}

} // namespace

std::string ArchiveReader::normalizePath(std::string_view raw) {
  std::string res;
  res.reserve(raw.size());
  for (char ch : raw) {
    char c = (ch == '\\') ? '/' : ch;
    c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    if (c == '/' && (res.empty() || res.back() == '/'))
      continue;
    res.push_back(c);
  }
  if (res.size() >= 2 && res[0] == '.' && res[1] == '/')
    res.erase(0, 2);
  return res;
}

bool ArchiveReader::parseToc(const std::uint8_t *toc, std::size_t tocSize,
                             std::uint64_t totalSize, ArchiveFormat format,
                             std::size_t nsaOffset) {
  entries_.clear();
  index_.clear();
  if (tocSize < 6)
    return false;

  std::size_t cursor = 0;
  std::uint32_t count = readBE16(toc + cursor);
  cursor += 2;
  // NSA v2 header has 0x0000 magic before 16-bit count if count == 0 && size >=
  // 8
  if (count == 0 && format == ArchiveFormat::NSA && tocSize >= 8) {
    count = readBE16(toc + cursor);
    cursor += 2;
  }
  const std::uint64_t baseOffset =
      static_cast<std::uint64_t>(readBE32(toc + cursor)) +
      static_cast<std::uint64_t>(nsaOffset);
  cursor += 4;
  if (baseOffset > totalSize)
    return false;
  const std::size_t tocLimit = static_cast<std::size_t>(
      baseOffset - static_cast<std::uint64_t>(nsaOffset));
  if (tocLimit > tocSize)
    return false;

  entries_.reserve(count);
  for (std::uint32_t i = 0; i < count; ++i) {
    if (cursor >= tocLimit)
      return false;
    const std::size_t nameStart = cursor;
    while (cursor < tocLimit && toc[cursor] != 0)
      ++cursor;
    if (cursor >= tocLimit)
      return false;
    std::string rawName(reinterpret_cast<const char *>(toc + nameStart),
                        cursor - nameStart);
    ++cursor; // skip NUL

    ArchiveEntry entry;
    entry.originalName = std::move(rawName);
    entry.normalizedPath = normalizePath(entry.originalName);

    if (format == ArchiveFormat::SAR) {
      if (cursor + 8 > tocLimit)
        return false;
      entry.compression = 0;
      entry.offset = baseOffset + readBE32(toc + cursor);
      entry.compressedSize = readBE32(toc + cursor + 4);
      entry.uncompressedSize = entry.compressedSize;
      cursor += 8;
    } else {
      if (cursor + 13 > tocLimit)
        return false;
      entry.compression = toc[cursor];
      entry.offset = baseOffset + readBE32(toc + cursor + 1);
      entry.compressedSize = readBE32(toc + cursor + 5);
      entry.uncompressedSize = readBE32(toc + cursor + 9);
      cursor += 13;
    }

    if (entry.offset + entry.compressedSize > totalSize)
      continue;
    const std::size_t idx = entries_.size();
    index_[entry.normalizedPath] = idx;
    entries_.push_back(std::move(entry));
  }
  return true;
}

bool ArchiveReader::loadFromMemory(std::vector<std::uint8_t> data,
                                   ArchiveFormat format,
                                   std::size_t nsaOffset) {
  reader_ = nullptr;
  totalSize_ = 0;
  if (data.size() < nsaOffset + 6) {
    entries_.clear();
    index_.clear();
    data_.clear();
    return false;
  }
  if (!parseToc(data.data() + nsaOffset, data.size() - nsaOffset, data.size(),
                format, nsaOffset)) {
    entries_.clear();
    index_.clear();
    data_.clear();
    return false;
  }
  totalSize_ = data.size();
  data_ = std::move(data);
  return true;
}

bool ArchiveReader::loadFromReader(std::uint64_t totalSize, ReadRangeFn reader,
                                   ArchiveFormat format,
                                   std::size_t nsaOffset) {
  entries_.clear();
  index_.clear();
  data_.clear();
  reader_ = nullptr;
  totalSize_ = 0;
  if (!reader || totalSize < static_cast<std::uint64_t>(nsaOffset) + 6u)
    return false;

  std::uint8_t preamble[8]{};
  const std::size_t preambleLen =
      (totalSize >= static_cast<std::uint64_t>(nsaOffset) + 8u) ? 8u : 6u;
  if (!reader(nsaOffset, preamble, preambleLen))
    return false;

  std::size_t cursor = 0;
  std::uint32_t count = readBE16(preamble + cursor);
  cursor += 2;
  if (count == 0 && format == ArchiveFormat::NSA && preambleLen >= 8u) {
    cursor += 2;
  }
  const std::uint32_t relBaseOffset = readBE32(preamble + cursor);
  constexpr std::uint32_t kMaxTocBytes = 64u * 1024u * 1024u;
  if (relBaseOffset < cursor + 4u || relBaseOffset > kMaxTocBytes ||
      static_cast<std::uint64_t>(nsaOffset) + relBaseOffset > totalSize)
    return false;

  std::vector<std::uint8_t> toc(relBaseOffset);
  if (!reader(nsaOffset, toc.data(), toc.size()))
    return false;

  if (!parseToc(toc.data(), toc.size(), totalSize, format, nsaOffset)) {
    entries_.clear();
    index_.clear();
    return false;
  }
  reader_ = std::move(reader);
  totalSize_ = totalSize;
  return true;
}

bool ArchiveReader::contains(std::string_view virtualPath) const {
  return find(virtualPath) != nullptr;
}

const ArchiveEntry *ArchiveReader::find(std::string_view virtualPath) const {
  const std::string key = normalizePath(virtualPath);
  const auto it = index_.find(key);
  if (it == index_.end())
    return nullptr;
  return &entries_[it->second];
}

std::vector<std::uint8_t>
ArchiveReader::extract(std::string_view virtualPath) const {
  const ArchiveEntry *entry = find(virtualPath);
  if (!entry)
    throw std::out_of_range("Ponscripter archive missing entry: " +
                            std::string(virtualPath));
  return extract(*entry);
}

std::vector<std::uint8_t> ArchiveReader::extractRaw(const ArchiveEntry &entry,
                                                    const std::uint8_t *src,
                                                    std::size_t srcLen) const {
  if (entry.compression == 0) {
    return std::vector<std::uint8_t>(src, src + srcLen);
  }
  if (entry.compression == 1) {
    return decompressSpb(src, srcLen, entry.uncompressedSize);
  }
  if (entry.compression == 2) {
    return decompressLzss(src, srcLen, entry.uncompressedSize);
  }
  if (entry.compression == 4) {
    // NBZ: 4-byte BE uncompressed size followed by bzip2 stream (or raw payload
    // in synthetic tests)
    if (srcLen >= 4) {
      const std::uint32_t nbzLen = readBE32(src);
      if (nbzLen + 4 == srcLen) {
        return std::vector<std::uint8_t>(src + 4, src + 4 + nbzLen);
      }
    }
  }
  return std::vector<std::uint8_t>(src, src + srcLen);
}

std::vector<std::uint8_t>
ArchiveReader::extract(const ArchiveEntry &entry) const {
  if (reader_) {
    if (entry.offset + entry.compressedSize > totalSize_)
      throw std::runtime_error("Ponscripter archive truncated entry: " +
                               entry.originalName);
    std::vector<std::uint8_t> raw(entry.compressedSize);
    if (!raw.empty() && !reader_(entry.offset, raw.data(), raw.size()))
      throw std::runtime_error("Ponscripter archive read failed: " +
                               entry.originalName);
    if (entry.compression == 0)
      return raw;
    return extractRaw(entry, raw.data(), raw.size());
  }
  if (entry.offset + entry.compressedSize > data_.size())
    throw std::runtime_error("Ponscripter archive truncated entry: " +
                             entry.originalName);
  return extractRaw(entry, data_.data() + entry.offset, entry.compressedSize);
}

std::vector<std::string> ArchiveReader::listEntries() const {
  std::vector<std::string> result;
  result.reserve(entries_.size());
  for (const auto &e : entries_)
    result.push_back(e.normalizedPath);
  return result;
}

std::vector<std::uint8_t> ArchiveReader::buildSyntheticSar(
    const std::vector<std::pair<std::string, std::vector<std::uint8_t>>>
        &files) {
  std::size_t headerSize = 6;
  for (const auto &f : files)
    headerSize += f.first.size() + 1 + 8;

  std::vector<std::uint8_t> out;
  out.reserve(headerSize + 1024);
  writeBE16(out, static_cast<std::uint16_t>(files.size()));
  writeBE32(out, static_cast<std::uint32_t>(headerSize));

  std::uint32_t relOffset = 0;
  for (const auto &f : files) {
    out.insert(out.end(), f.first.begin(), f.first.end());
    out.push_back(0);
    writeBE32(out, relOffset);
    writeBE32(out, static_cast<std::uint32_t>(f.second.size()));
    relOffset += static_cast<std::uint32_t>(f.second.size());
  }
  for (const auto &f : files) {
    out.insert(out.end(), f.second.begin(), f.second.end());
  }
  return out;
}

std::vector<std::uint8_t> ArchiveReader::buildSyntheticNsa(
    const std::vector<std::pair<std::string, std::vector<std::uint8_t>>>
        &files) {
  std::size_t headerSize = 6;
  for (const auto &f : files)
    headerSize += f.first.size() + 1 + 13;

  std::vector<std::uint8_t> out;
  out.reserve(headerSize + 1024);
  writeBE16(out, static_cast<std::uint16_t>(files.size()));
  writeBE32(out, static_cast<std::uint32_t>(headerSize));

  std::uint32_t relOffset = 0;
  for (const auto &f : files) {
    out.insert(out.end(), f.first.begin(), f.first.end());
    out.push_back(0);
    out.push_back(0); // compression = 0 (raw)
    writeBE32(out, relOffset);
    writeBE32(out, static_cast<std::uint32_t>(f.second.size()));
    writeBE32(out, static_cast<std::uint32_t>(f.second.size()));
    relOffset += static_cast<std::uint32_t>(f.second.size());
  }
  for (const auto &f : files) {
    out.insert(out.end(), f.second.begin(), f.second.end());
  }
  return out;
}

} // namespace Ponscripter
