// Copyright 2026 Kotonoha contributors. SPDX-License-Identifier: MIT
#include <Artemis/PfsArchive.hpp>

#include <algorithm>
#include <cctype>
#include <cstring>

namespace Artemis {
namespace {

constexpr std::uint32_t kMaxTableBytes = 64u * 1024u * 1024u;
constexpr std::uint32_t kMaxEntryCount = 200000u;
constexpr std::uint32_t kMaxNameBytes = 4096u;

std::uint32_t readU32LE(const std::uint8_t *p) {
  return static_cast<std::uint32_t>(p[0]) |
         (static_cast<std::uint32_t>(p[1]) << 8) |
         (static_cast<std::uint32_t>(p[2]) << 16) |
         (static_cast<std::uint32_t>(p[3]) << 24);
}

std::uint32_t rol32(std::uint32_t value, unsigned bits) {
  return (value << bits) | (value >> (32u - bits));
}

bool readExact(Kotonoha::Filesystem::FileHandle &handle, void *dst,
               std::size_t count) {
  auto *out = static_cast<std::uint8_t *>(dst);
  std::size_t remaining = count;
  while (remaining > 0) {
    const auto got = handle.read(out, remaining);
    if (got <= 0)
      return false;
    out += static_cast<std::size_t>(got);
    remaining -= static_cast<std::size_t>(got);
  }
  return true;
}

class PfsEntryStream final : public Kotonoha::AssetStream {
public:
  PfsEntryStream(std::shared_ptr<const PfsArchive> archive, PfsEntry entry)
      : archive_(std::move(archive)), entry_(std::move(entry)) {}

  std::uint64_t size() const override { return entry_.size; }
  std::uint64_t tell() const override { return pos_; }

  std::size_t read(Uint8 *buffer, std::size_t count) override {
    if (!buffer || count == 0 || pos_ >= entry_.size)
      return 0;
    const std::size_t got = archive_->readAt(entry_, pos_, buffer, count);
    pos_ += got;
    return got;
  }

  bool seek(std::uint64_t position) override {
    if (position > entry_.size)
      return false;
    pos_ = position;
    return true;
  }

private:
  std::shared_ptr<const PfsArchive> archive_;
  PfsEntry entry_;
  std::uint64_t pos_ = 0;
};

} // namespace

std::array<std::uint8_t, 20> PfsArchive::sha1(const std::uint8_t *data,
                                              std::size_t size) {
  std::uint32_t h0 = 0x67452301u;
  std::uint32_t h1 = 0xEFCDAB89u;
  std::uint32_t h2 = 0x98BADCFEu;
  std::uint32_t h3 = 0x10325476u;
  std::uint32_t h4 = 0xC3D2E1F0u;

  auto processBlock = [&](const std::uint8_t block[64]) {
    std::uint32_t w[80];
    for (int i = 0; i < 16; ++i) {
      w[i] = (static_cast<std::uint32_t>(block[i * 4 + 0]) << 24) |
             (static_cast<std::uint32_t>(block[i * 4 + 1]) << 16) |
             (static_cast<std::uint32_t>(block[i * 4 + 2]) << 8) |
             static_cast<std::uint32_t>(block[i * 4 + 3]);
    }
    for (int i = 16; i < 80; ++i) {
      w[i] = rol32(w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16], 1);
    }

    std::uint32_t a = h0;
    std::uint32_t b = h1;
    std::uint32_t c = h2;
    std::uint32_t d = h3;
    std::uint32_t e = h4;

    for (int i = 0; i < 80; ++i) {
      std::uint32_t f = 0;
      std::uint32_t k = 0;
      if (i < 20) {
        f = (b & c) | ((~b) & d);
        k = 0x5A827999u;
      } else if (i < 40) {
        f = b ^ c ^ d;
        k = 0x6ED9EBA1u;
      } else if (i < 60) {
        f = (b & c) | (b & d) | (c & d);
        k = 0x8F1BBCDCu;
      } else {
        f = b ^ c ^ d;
        k = 0xCA62C1D6u;
      }
      const std::uint32_t temp = rol32(a, 5) + f + e + k + w[i];
      e = d;
      d = c;
      c = rol32(b, 30);
      b = a;
      a = temp;
    }

    h0 += a;
    h1 += b;
    h2 += c;
    h3 += d;
    h4 += e;
  };

  std::size_t offset = 0;
  while (offset + 64 <= size) {
    processBlock(data + offset);
    offset += 64;
  }

  std::uint8_t tail[128];
  std::memset(tail, 0, sizeof(tail));
  const std::size_t rem = size - offset;
  if (rem > 0)
    std::memcpy(tail, data + offset, rem);
  tail[rem] = 0x80u;

  const std::size_t paddedLen = (rem + 1 + 8 <= 64) ? 64 : 128;
  const std::uint64_t bitLen = static_cast<std::uint64_t>(size) * 8ull;
  for (int i = 0; i < 8; ++i) {
    tail[paddedLen - 1 - i] =
        static_cast<std::uint8_t>((bitLen >> (i * 8)) & 0xFFu);
  }
  processBlock(tail);
  if (paddedLen == 128)
    processBlock(tail + 64);

  std::array<std::uint8_t, 20> out{};
  const std::uint32_t hs[5] = {h0, h1, h2, h3, h4};
  for (int i = 0; i < 5; ++i) {
    out[i * 4 + 0] = static_cast<std::uint8_t>((hs[i] >> 24) & 0xFFu);
    out[i * 4 + 1] = static_cast<std::uint8_t>((hs[i] >> 16) & 0xFFu);
    out[i * 4 + 2] = static_cast<std::uint8_t>((hs[i] >> 8) & 0xFFu);
    out[i * 4 + 3] = static_cast<std::uint8_t>(hs[i] & 0xFFu);
  }
  return out;
}

std::string PfsArchive::normalizePath(std::string_view path) {
  std::string out;
  out.reserve(path.size());
  for (char c : path) {
    if (c == '\\')
      c = '/';
    if (c == '/' && (out.empty() || out.back() == '/'))
      continue;
    out.push_back(c);
  }
  while (!out.empty() && out.back() == '/')
    out.pop_back();
  return out;
}

std::string PfsArchive::toLowerAscii(std::string_view text) {
  std::string out(text);
  std::transform(out.begin(), out.end(), out.begin(), [](unsigned char c) {
    return static_cast<char>(std::tolower(c));
  });
  return out;
}

std::shared_ptr<PfsArchive>
PfsArchive::open(std::unique_ptr<Kotonoha::Filesystem::FileHandle> handle,
                 std::string label) {
  if (!handle)
    return nullptr;

  const std::int64_t fileSize = handle->size();
  if (fileSize < 11)
    return nullptr;

  if (handle->seek(0, Kotonoha::Filesystem::SeekOrigin::Begin) != 0)
    return nullptr;

  std::uint8_t header[7];
  if (!readExact(*handle, header, sizeof(header)))
    return nullptr;

  if (header[0] != 'p' || header[1] != 'f')
    return nullptr;

  const int version =
      (header[2] >= '0' && header[2] <= '9') ? (header[2] - '0') : 8;
  const std::uint32_t tableLen = readU32LE(header + 3);
  if (tableLen < 4 || tableLen > kMaxTableBytes ||
      static_cast<std::int64_t>(7u + tableLen) > fileSize) {
    return nullptr;
  }

  std::vector<std::uint8_t> table(tableLen);
  if (!readExact(*handle, table.data(), tableLen))
    return nullptr;

  auto archive = std::shared_ptr<PfsArchive>(new PfsArchive());
  archive->handle_ = std::move(handle);
  archive->label_ = std::move(label);
  archive->version_ = version;
  archive->encrypted_ = (version >= 8);
  archive->xorKey_ = sha1(table.data(), table.size());

  std::size_t pos = 0;
  const std::uint32_t entryCount = readU32LE(table.data() + pos);
  pos += 4;
  if (entryCount > kMaxEntryCount)
    return nullptr;

  archive->entries_.reserve(entryCount);
  archive->indexByLower_.reserve(entryCount);

  for (std::uint32_t i = 0; i < entryCount; ++i) {
    if (pos + 4 > table.size())
      return nullptr;
    const std::uint32_t nameLen = readU32LE(table.data() + pos);
    pos += 4;
    if (nameLen > kMaxNameBytes || pos + nameLen + 12 > table.size())
      return nullptr;

    std::string rawName(reinterpret_cast<const char *>(table.data() + pos),
                        nameLen);
    pos += nameLen;

    // 4-byte reserved field + 4-byte offset + 4-byte data length.
    pos += 4;
    const std::uint32_t offset = readU32LE(table.data() + pos);
    pos += 4;
    const std::uint32_t dataLen = readU32LE(table.data() + pos);
    pos += 4;

    if (static_cast<std::uint64_t>(offset) +
            static_cast<std::uint64_t>(dataLen) >
        static_cast<std::uint64_t>(fileSize)) {
      continue;
    }

    PfsEntry entry;
    entry.path = normalizePath(rawName);
    entry.offset = offset;
    entry.size = dataLen;
    if (entry.path.empty())
      continue;

    const std::string lower = toLowerAscii(entry.path);
    const std::size_t idx = archive->entries_.size();
    archive->entries_.push_back(std::move(entry));
    archive->indexByLower_[lower] = idx;
  }

  return archive;
}

const PfsEntry *PfsArchive::find(std::string_view normalizedLowerPath) const {
  auto it = indexByLower_.find(std::string(normalizedLowerPath));
  if (it == indexByLower_.end())
    return nullptr;
  return &entries_[it->second];
}

std::size_t PfsArchive::readAt(const PfsEntry &entry, std::uint64_t relOffset,
                               std::uint8_t *dst, std::size_t count) const {
  if (!handle_ || !dst || count == 0 || relOffset >= entry.size)
    return 0;

  const std::uint64_t avail = entry.size - relOffset;
  const std::size_t toRead =
      static_cast<std::size_t>(std::min<std::uint64_t>(count, avail));

  std::size_t totalRead = 0;
  {
    std::lock_guard<std::mutex> lock(ioMutex_);
    const std::int64_t target =
        static_cast<std::int64_t>(entry.offset + relOffset);
    if (handle_->seek(target, Kotonoha::Filesystem::SeekOrigin::Begin) !=
        target) {
      return 0;
    }
    while (totalRead < toRead) {
      const auto got = handle_->read(dst + totalRead, toRead - totalRead);
      if (got <= 0)
        break;
      totalRead += static_cast<std::size_t>(got);
    }
  }

  if (encrypted_ && totalRead > 0) {
    for (std::size_t i = 0; i < totalRead; ++i) {
      dst[i] ^= xorKey_[static_cast<std::size_t>((relOffset + i) % 20ull)];
    }
  }
  return totalRead;
}

std::vector<std::uint8_t> PfsArchive::readEntry(const PfsEntry &entry) const {
  std::vector<std::uint8_t> out(static_cast<std::size_t>(entry.size));
  if (out.empty())
    return out;
  const std::size_t got = readAt(entry, 0, out.data(), out.size());
  out.resize(got);
  return out;
}

std::unique_ptr<Kotonoha::AssetStream>
PfsArchive::openStream(const PfsEntry &entry) const {
  return std::make_unique<PfsEntryStream>(shared_from_this(), entry);
}

} // namespace Artemis
