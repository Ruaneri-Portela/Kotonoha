// CMP archive container: filesystem slices and the Pack directory reader.
// Byte codecs and image decoders live in modules/formats, re-exported by the
// SummerRadish/Pack.hpp umbrella.
#include <Kotonoha/Filesystem/FileHandle.hpp>
#include <Kotonoha/Filesystem/Filesystem.hpp>
#include <SummerRadish/Pack.hpp>
#include <algorithm>
#include <array>
#include <cstring>
#include <limits>
#include <stdexcept>
#include <utility>
namespace SummerRadish {
namespace {
void require(bool ok, const char *message) {
  if (!ok)
    throw std::runtime_error(message);
}
size_t readSliceInto(Kotonoha::Filesystem::Filesystem &filesystem,
                     const std::string &id, size_t offset, void *dst,
                     size_t size) {
  auto handle = filesystem.open(id);
  const auto length = handle->size();
  require(length >= 0 && uint64_t(offset) <= uint64_t(length) &&
              uint64_t(size) <= uint64_t(length) - offset,
          "Truncated CMP pack");
  require(handle->seek(static_cast<int64_t>(offset),
                       Kotonoha::Filesystem::SeekOrigin::Begin) ==
              static_cast<int64_t>(offset),
          "Cannot seek CMP pack");
  size_t position = 0;
  auto *out = static_cast<uint8_t *>(dst);
  while (position != size) {
    const auto read = handle->read(out + position, size - position);
    require(read > 0, "Cannot read/truncated CMP pack");
    position += static_cast<size_t>(read);
  }
  return position;
}
Bytes readSlice(Kotonoha::Filesystem::Filesystem &filesystem,
                const std::string &id, size_t offset, size_t size) {
  require(size <= (256u << 20), "CMP entry exceeds allocation limit");
  Bytes bytes(size);
  if (size > 0)
    readSliceInto(filesystem, id, offset, bytes.data(), size);
  return bytes;
}
class CmpSliceFileHandle final : public Kotonoha::Filesystem::FileHandle {
  std::unique_ptr<Kotonoha::Filesystem::FileHandle> base_;
  int64_t baseOffset_ = 0;
  int64_t size_ = 0;
  int64_t cursor_ = 0;

public:
  CmpSliceFileHandle(std::unique_ptr<Kotonoha::Filesystem::FileHandle> base,
                     uint32_t offset, uint32_t size)
      : base_(std::move(base)), baseOffset_(offset), size_(size) {
    require(base_ != nullptr, "CMP slice base handle is null");
    const auto total = base_->size();
    require(total >= 0 && baseOffset_ + size_ <= total,
            "CMP slice exceeds archive bounds");
    require(base_->seek(baseOffset_, Kotonoha::Filesystem::SeekOrigin::Begin) ==
                baseOffset_,
            "Cannot seek CMP slice");
  }
  int64_t read(void *buffer, size_t count) override {
    if (cursor_ >= size_ || count == 0)
      return 0;
    const size_t avail = static_cast<size_t>(size_ - cursor_);
    const size_t toRead = std::min(count, avail);
    if (base_->tell() != baseOffset_ + cursor_) {
      if (base_->seek(baseOffset_ + cursor_,
                      Kotonoha::Filesystem::SeekOrigin::Begin) !=
          baseOffset_ + cursor_)
        return -1;
    }
    const int64_t got = base_->read(buffer, toRead);
    if (got > 0)
      cursor_ += got;
    return got;
  }
  int64_t seek(int64_t offset,
               Kotonoha::Filesystem::SeekOrigin origin) override {
    int64_t target = 0;
    switch (origin) {
    case Kotonoha::Filesystem::SeekOrigin::Begin:
      target = offset;
      break;
    case Kotonoha::Filesystem::SeekOrigin::Current:
      target = cursor_ + offset;
      break;
    case Kotonoha::Filesystem::SeekOrigin::End:
      target = size_ + offset;
      break;
    }
    if (target < 0 || target > size_)
      return -1;
    if (base_->seek(baseOffset_ + target,
                    Kotonoha::Filesystem::SeekOrigin::Begin) !=
        baseOffset_ + target)
      return -1;
    cursor_ = target;
    return cursor_;
  }
  int64_t tell() const override { return cursor_; }
  int64_t size() const override { return size_; }
};
class CmpMemoryFileHandle final : public Kotonoha::Filesystem::FileHandle {
  Bytes data_;
  int64_t cursor_ = 0;

public:
  explicit CmpMemoryFileHandle(Bytes data) : data_(std::move(data)) {}
  int64_t read(void *buffer, size_t count) override {
    if (cursor_ >= static_cast<int64_t>(data_.size()) || count == 0)
      return 0;
    const size_t avail = data_.size() - static_cast<size_t>(cursor_);
    const size_t toRead = std::min(count, avail);
    std::memcpy(buffer, data_.data() + cursor_, toRead);
    cursor_ += static_cast<int64_t>(toRead);
    return static_cast<int64_t>(toRead);
  }
  int64_t seek(int64_t offset,
               Kotonoha::Filesystem::SeekOrigin origin) override {
    int64_t target = 0;
    switch (origin) {
    case Kotonoha::Filesystem::SeekOrigin::Begin:
      target = offset;
      break;
    case Kotonoha::Filesystem::SeekOrigin::Current:
      target = cursor_ + offset;
      break;
    case Kotonoha::Filesystem::SeekOrigin::End:
      target = static_cast<int64_t>(data_.size()) + offset;
      break;
    }
    if (target < 0 || target > static_cast<int64_t>(data_.size()))
      return -1;
    cursor_ = target;
    return cursor_;
  }
  int64_t tell() const override { return cursor_; }
  int64_t size() const override { return static_cast<int64_t>(data_.size()); }
};
} // namespace
Pack::Pack(const std::filesystem::path &file, CmpKeyProfile profile)
    : Pack(Kotonoha::Filesystem::Filesystem::mount(
               (file.has_parent_path() ? file.parent_path()
                                       : std::filesystem::path("."))
                   .string()),
           file.filename().string(), profile) {}

Pack::Pack(std::shared_ptr<Kotonoha::Filesystem::Filesystem> mounted,
           std::string id, CmpKeyProfile requestedProfile)
    : filesystem(std::move(mounted)), sourceId(std::move(id)),
      detectedProfile(requestedProfile) {
  require(filesystem != nullptr, "CMP filesystem is null");
  const auto info = filesystem->stat(sourceId);
  const auto length = info.size;
  require(info.type == Kotonoha::Filesystem::FileType::Regular &&
              length >= 12 && length <= UINT32_MAX,
          "Invalid CMP size");
  auto footer =
      readSlice(*filesystem, sourceId, static_cast<size_t>(length - 8), 8);

  // Summer Radish Vacation 1 & 2, Lost M, and MEO/INSTALL.cmp each use a
  // distinct 16-byte XOR key.
  constexpr std::array<uint8_t, 16> key1{0x0f, 0x25, 0x3e, 0x5c, 0x2a, 0x4b,
                                         0x77, 0x90, 0x05, 0x8a, 0x8e, 0x46,
                                         0xeb, 0x3d, 0x11, 0x43};
  constexpr std::array<uint8_t, 16> key2{0x6c, 0x14, 0xf2, 0x03, 0xe3, 0x62,
                                         0x32, 0xac, 0x03, 0x04, 0xac, 0xf2,
                                         0xd3, 0x84, 0xf8, 0xca};
  constexpr std::array<uint8_t, 16> keyLostM{0x67, 0xd8, 0x16, 0x28, 0x21, 0x05,
                                             0x50, 0x0a, 0x6c, 0xc3, 0x89, 0x71,
                                             0xdb, 0x5c, 0x03, 0xce};
  constexpr std::array<uint8_t, 16> key0{};
  struct CandidateKey {
    CmpKeyProfile profile;
    const std::array<uint8_t, 16> *key;
  };
  const std::array<CandidateKey, 4> allKeys = {{
      {CmpKeyProfile::SummerRadish1, &key1},
      {CmpKeyProfile::SummerRadish2, &key2},
      {CmpKeyProfile::LostM, &keyLostM},
      {CmpKeyProfile::MeoZeroKey, &key0},
  }};

  std::vector<uint8_t> data;
  size_t start = 0;
  bool parsed = false;
  std::string lastError;
  for (const auto &candidate : allKeys) {
    if (requestedProfile != CmpKeyProfile::Auto &&
        candidate.profile != requestedProfile)
      continue;
    const auto *key = candidate.key;
    bool signatureOk = true;
    for (size_t i = 0; i < 4; ++i) {
      if ((footer[i] ^ (*key)[i]) != uint8_t("PACK"[i])) {
        signatureOk = false;
        break;
      }
    }
    if (!signatureOk)
      continue;
    size_t candidateStart = word(footer, 4);
    if (candidateStart > length - 12)
      continue;
    auto encoded = readSlice(*filesystem, sourceId, candidateStart,
                             static_cast<size_t>(length - 8 - candidateStart));
    size_t raw = word(encoded, 0);
    if (raw > (16u << 20))
      continue;
    for (size_t i = 4; i < encoded.size(); ++i)
      encoded[i] ^= (*key)[(i - 4) % 16];
    try {
      data = decompress(encoded, raw, 4);
      start = candidateStart;
      detectedProfile = candidate.profile;
      parsed = true;
      break;
    } catch (const std::exception &error) {
      lastError = error.what();
      continue;
    }
  }
  require(parsed, "Invalid CMP signature/key or undecryptable directory");

  size_t p = 0;
  std::vector<Entry> list;
  while (p < data.size()) {
    require(data.size() - p >= 10, "Truncated CMP directory entry");
    Entry e;
    e.offset = word(data, p);
    size_t units = data[p + 4];
    e.compressed = data[p + 5] != 0;
    e.name = utf16(data, p + 10, units);
    p += 10 + 2 * units;
    require(e.offset <= start, "CMP entry outside data area");
    if (!list.empty()) {
      require(e.offset >= list.back().offset, "Unordered CMP entries");
      list.back().size = e.offset - list.back().offset;
    }
    if (e.name.empty()) {
      require(p == data.size(), "Unexpected CMP sentinel");
      break;
    }
    list.push_back(std::move(e));
  }
  if (!list.empty() && list.back().size == 0) {
    const size_t lastSize = start - list.back().offset;
    require(lastSize <= std::numeric_limits<uint32_t>::max(),
            "CMP entry exceeds 4 GiB size limit");
    list.back().size = static_cast<uint32_t>(lastSize);
  }
  for (auto &e : list)
    require(directory.emplace(canonical(e.name), e).second,
            "Duplicate CMP entry");
}
bool Pack::contains(const std::string &name) const {
  return directory.count(canonical(name)) != 0;
}
const Entry &Pack::entry(const std::string &name) const {
  auto it = directory.find(canonical(name));
  if (it == directory.end())
    throw std::runtime_error("CMP asset missing: " + name);
  return it->second;
}
Bytes Pack::read(const std::string &name) const {
  const auto &e = entry(name);
  auto d = readSlice(*filesystem, sourceId, e.offset, e.size);
  return e.compressed ? decompress(d, word(d, 0), 4) : d;
}
Bytes Pack::readRange(const std::string &name, size_t offset,
                      size_t size) const {
  const auto &e = entry(name);
  if (e.compressed) {
    auto full = read(name);
    require(offset <= full.size() && size <= full.size() - offset,
            "CMP entry range out of bounds");
    return Bytes(full.begin() + offset, full.begin() + offset + size);
  }
  require(offset <= e.size && size <= e.size - offset,
          "CMP entry range out of bounds");
  return readSlice(*filesystem, sourceId, e.offset + offset, size);
}
size_t Pack::readEntry(const Entry &e, void *dst, size_t offset,
                       size_t size) const {
  if (size == 0)
    return 0;
  require(dst != nullptr, "CMP readEntry destination is null");
  if (e.compressed) {
    auto d = readSlice(*filesystem, sourceId, e.offset, e.size);
    auto full = decompress(d, word(d, 0), 4);
    require(offset <= full.size() && size <= full.size() - offset,
            "CMP entry range out of bounds");
    std::memcpy(dst, full.data() + offset, size);
    return size;
  }
  require(offset <= e.size && size <= e.size - offset,
          "CMP entry range out of bounds");
  return readSliceInto(*filesystem, sourceId, e.offset + offset, dst, size);
}
std::unique_ptr<Kotonoha::Filesystem::FileHandle>
Pack::openEntryStream(const std::string &name) const {
  const auto &e = entry(name);
  if (e.compressed)
    return std::make_unique<CmpMemoryFileHandle>(read(name));
  return std::make_unique<CmpSliceFileHandle>(filesystem->open(sourceId),
                                              e.offset, e.size);
}
} // namespace SummerRadish
