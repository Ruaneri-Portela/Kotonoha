// Copyright 2026 Kotonoha contributors. SPDX-License-Identifier: MIT
// Original C++17 reader based on publicly documented AFS/NPA/SG-NPA structures.
#include <N2System/Archive.hpp>
#include <algorithm>
#include <array>
#include <cstring>
#include <limits>
#include <stdexcept>
#include <utility>

namespace N2System {
namespace {

// Repeating 8-byte XOR key for Steins;Gate SG-NPA: ~("BUCKTICK").
constexpr std::array<std::uint8_t, 8> kSgNpaKey = {0xBD, 0xAA, 0xBC, 0xB4,
                                                   0xAB, 0xB6, 0xBC, 0xB4};

class MemoryArchiveSource final : public ArchiveSource {
public:
  explicit MemoryArchiveSource(std::vector<std::uint8_t> bytes)
      : bytes_(std::move(bytes)) {}

  std::uint64_t size() const override { return bytes_.size(); }

  std::size_t readAt(std::uint64_t offset, std::uint8_t *buffer,
                     std::size_t count) const override {
    if (offset >= bytes_.size() || count == 0)
      return 0;
    const auto avail = static_cast<std::size_t>(
        std::min<std::uint64_t>(bytes_.size() - offset, count));
    if (avail && buffer)
      std::memcpy(buffer, bytes_.data() + offset, avail);
    return avail;
  }

private:
  std::vector<std::uint8_t> bytes_;
};

std::uint32_t readLe32(const std::uint8_t *p) {
  return std::uint32_t(p[0]) | (std::uint32_t(p[1]) << 8) |
         (std::uint32_t(p[2]) << 16) | (std::uint32_t(p[3]) << 24);
}

std::uint64_t readLe64(const std::uint8_t *p) {
  return std::uint64_t(readLe32(p)) | (std::uint64_t(readLe32(p + 4)) << 32);
}

void appendUtf8(std::string &out, std::uint32_t cp) {
  if (cp <= 0x7Fu) {
    out.push_back(static_cast<char>(cp));
  } else if (cp <= 0x7FFu) {
    out.push_back(static_cast<char>(0xC0u | (cp >> 6)));
    out.push_back(static_cast<char>(0x80u | (cp & 0x3Fu)));
  } else if (cp <= 0xFFFFu) {
    out.push_back(static_cast<char>(0xE0u | (cp >> 12)));
    out.push_back(static_cast<char>(0x80u | ((cp >> 6) & 0x3Fu)));
    out.push_back(static_cast<char>(0x80u | (cp & 0x3Fu)));
  } else if (cp <= 0x10FFFFu) {
    out.push_back(static_cast<char>(0xF0u | (cp >> 18)));
    out.push_back(static_cast<char>(0x80u | ((cp >> 12) & 0x3Fu)));
    out.push_back(static_cast<char>(0x80u | ((cp >> 6) & 0x3Fu)));
    out.push_back(static_cast<char>(0x80u | (cp & 0x3Fu)));
  }
}

std::string decodeUtf16LeToUtf8(const std::uint8_t *data, std::size_t byteLen) {
  std::string out;
  out.reserve(byteLen);
  std::size_t i = 0;
  while (i + 1 < byteLen) {
    const std::uint16_t u1 =
        static_cast<std::uint16_t>(data[i] | (std::uint16_t(data[i + 1]) << 8));
    i += 2;
    if (u1 == 0)
      break;
    if (u1 >= 0xD800u && u1 <= 0xDBFFu && i + 1 < byteLen) {
      const std::uint16_t u2 = static_cast<std::uint16_t>(
          data[i] | (std::uint16_t(data[i + 1]) << 8));
      if (u2 >= 0xDC00u && u2 <= 0xDFFFu) {
        i += 2;
        const std::uint32_t cp =
            0x10000u + ((std::uint32_t(u1 - 0xD800u) << 10) | (u2 - 0xDC00u));
        appendUtf8(out, cp);
        continue;
      }
    }
    appendUtf8(out, u1);
  }
  return out;
}

// Reject traversal and ambiguous aliases before adding them to the index.
// Normalize only ASCII lettercase and separators; legacy multibyte name
// bytes remain untouched.
bool safeArchiveName(std::string_view value) {
  if (value.empty() || value.front() == '/' || value.back() == '/')
    return false;
  std::size_t begin = 0;
  while (begin < value.size()) {
    const auto end = value.find('/', begin);
    const auto part =
        value.substr(begin, end == std::string_view::npos ? value.size() - begin
                                                          : end - begin);
    if (part.empty() || part == "." || part == "..")
      return false;
    for (unsigned char c : part)
      if (c < 0x20 || c == 0x7f || c == ':')
        return false;
    if (end == std::string_view::npos)
      break;
    begin = end + 1;
  }
  return true;
}

std::uint8_t nameKey(std::uint32_t character, std::uint32_t entry,
                     std::uint32_t first, std::uint32_t second) {
  const std::uint32_t keys = first * second;
  std::uint32_t value = 0xfcu * character;
  for (int shift : {24, 16, 8, 0})
    value -= shift ? keys >> shift : keys & 255u;
  for (int shift : {24, 16, 8, 0})
    value -= shift ? entry >> shift : entry;
  return static_cast<std::uint8_t>(value);
}

} // namespace

Archive::Archive(std::vector<std::uint8_t> bytes)
    : source_(std::make_shared<MemoryArchiveSource>(std::move(bytes))) {
  init();
}

Archive::Archive(std::shared_ptr<ArchiveSource> source)
    : source_(std::move(source)) {
  if (!source_)
    throw std::invalid_argument("N2System: null archive source");
  init();
}

void Archive::init() {
  totalSize_ = source_->size();
  std::uint8_t magic[8] = {};
  const auto got = source_->readAt(0, magic, sizeof(magic));
  if (got >= 4 && magic[0] == 'A' && magic[1] == 'F' && magic[2] == 'S' &&
      magic[3] == 0) {
    format_ = ArchiveFormat::AFS;
    parseAfs();
  } else if (got >= 7 && magic[0] == 'N' && magic[1] == 'P' &&
             magic[2] == 'A' && magic[3] == 1 && magic[4] == 0 &&
             magic[5] == 0 && magic[6] == 0) {
    format_ = ArchiveFormat::NPA;
    parseNpa();
  } else if (got >= 8) {
    const std::uint32_t hdrSize = readLe32(magic);
    if (hdrSize >= 4 && hdrSize <= 64u * 1024u * 1024u &&
        std::uint64_t(4) + hdrSize <= totalSize_) {
      format_ = ArchiveFormat::SG_NPA;
      parseSgNpa();
      return;
    }
    throw std::runtime_error("N2System: unsupported archive magic");
  } else {
    throw std::runtime_error("N2System: unsupported archive magic");
  }
}

void Archive::check(std::uint64_t offset, std::uint64_t size) const {
  if (offset > totalSize_ || size > totalSize_ - offset)
    throw std::runtime_error("N2System: archive data exceeds bounds");
}

std::vector<std::uint8_t> Archive::readSource(std::uint64_t offset,
                                              std::size_t size) const {
  check(offset, size);
  std::vector<std::uint8_t> out(size);
  if (size > 0) {
    const auto got = source_->readAt(offset, out.data(), size);
    if (got != size)
      throw std::runtime_error("N2System: short read from archive source");
  }
  return out;
}

std::string Archive::normalize(std::string_view name) {
  std::string result;
  result.reserve(name.size());
  for (unsigned char c : name) {
    if (c == '\\')
      c = '/';
    if (c >= 'A' && c <= 'Z')
      c = static_cast<unsigned char>(c + ('a' - 'A'));
    result.push_back(static_cast<char>(c));
  }
  return result;
}

void Archive::add(ArchiveEntry entry) {
  check(entry.offset, entry.size);
  entry.name = normalize(entry.name);
  if (!safeArchiveName(entry.name))
    throw std::runtime_error("N2System: unsafe path in archive");
  if (!index_.emplace(entry.name, entries_.size()).second)
    throw std::runtime_error("N2System: duplicate entry " + entry.name);
  entries_.push_back(std::move(entry));
}

void Archive::parseAfs() {
  const auto hdr = readSource(0, 8);
  const auto count = readLe32(hdr.data() + 4);
  if (count > 1000000)
    throw std::runtime_error("N2System: excessive AFS entry count");
  const std::uint64_t tableBytes = std::uint64_t(count) * 8 + 4;
  check(8, tableBytes);
  const auto table = readSource(8, static_cast<std::size_t>(tableBytes));
  const auto namesOffset = readLe32(table.data() + std::size_t(count) * 8);
  std::vector<std::uint8_t> nameTable;
  if (namesOffset) {
    const std::uint64_t nameBytes = std::uint64_t(count) * 48;
    check(namesOffset, nameBytes);
    nameTable = readSource(namesOffset, static_cast<std::size_t>(nameBytes));
  }
  for (std::uint32_t i = 0; i < count; ++i) {
    const auto at = static_cast<std::size_t>(i) * 8;
    const auto offset = readLe32(table.data() + at);
    const auto size = readLe32(table.data() + at + 4);
    std::string name = "entry/" + std::to_string(i);
    if (!nameTable.empty()) {
      const auto begin = static_cast<std::size_t>(i) * 48;
      const auto end = std::find(nameTable.begin() + begin,
                                 nameTable.begin() + begin + 32, 0);
      if (end != nameTable.begin() + begin)
        name.assign(nameTable.begin() + begin, end);
    }
    add({std::move(name), offset, size});
  }
}

void Archive::parseNpa() {
  const auto hdr = readSource(0, 41);
  const auto first = readLe32(hdr.data() + 7);
  const auto second = readLe32(hdr.data() + 11);
  if (hdr[15])
    throw std::runtime_error("N2System: compressed NPA unsupported");
  if (hdr[16])
    throw std::runtime_error("N2System: encrypted NPA unsupported");
  const auto count = readLe32(hdr.data() + 17);
  const auto directories = readLe32(hdr.data() + 21);
  const auto files = readLe32(hdr.data() + 25);
  if (count > 1000000 || std::uint64_t(directories) + files != count)
    throw std::runtime_error("N2System: inconsistent NPA counts");
  if (readLe64(hdr.data() + 29) != 0)
    throw std::runtime_error("N2System: bad NPA reserved field");
  const std::uint32_t tableSize = readLe32(hdr.data() + 37);
  const std::uint64_t start = 41ull + tableSize;
  check(start, 0);
  const auto table = readSource(41, tableSize);
  std::size_t pos = 0;
  std::uint32_t actualFiles = 0;
  for (std::uint32_t i = 0; i < count; ++i) {
    if (pos + 4 > table.size())
      throw std::runtime_error("N2System: truncated NPA table");
    const auto nameLen = readLe32(table.data() + pos);
    pos += 4;
    if (nameLen == 0 || nameLen > 1024 * 1024 ||
        pos + nameLen + 17 > table.size())
      throw std::runtime_error("N2System: invalid NPA name length");
    std::string name;
    name.reserve(nameLen);
    for (std::uint32_t j = 0; j < nameLen; ++j)
      name.push_back(static_cast<char>(
          std::uint8_t(table[pos + j] + nameKey(j, i, first, second))));
    pos += nameLen;
    const auto type = table[pos++];
    (void)readLe32(table.data() + pos);
    pos += 4;
    const auto offset = readLe32(table.data() + pos);
    pos += 4;
    const auto packed = readLe32(table.data() + pos);
    pos += 4;
    const auto unpacked = readLe32(table.data() + pos);
    pos += 4;
    if (type == 2) {
      ++actualFiles;
      if (packed != unpacked)
        throw std::runtime_error("N2System: compressed NPA entry unsupported");
      add({std::move(name), start + offset, unpacked});
    }
  }
  if (pos > tableSize || actualFiles != files)
    throw std::runtime_error("N2System: inconsistent NPA table");
}

void Archive::parseSgNpa() {
  const auto szBuf = readSource(0, 4);
  const std::uint32_t hdrSize = readLe32(szBuf.data());
  if (hdrSize < 4)
    throw std::runtime_error("N2System: truncated SG-NPA header");
  auto hdr = readSource(4, hdrSize);
  for (std::size_t i = 0; i < hdr.size(); ++i)
    hdr[i] ^= kSgNpaKey[i & 7u];

  const std::uint32_t count = readLe32(hdr.data());
  if (count > 1000000)
    throw std::runtime_error("N2System: excessive SG-NPA entry count");

  std::size_t pos = 4;
  entries_.reserve(count);
  for (std::uint32_t i = 0; i < count; ++i) {
    if (pos + 4 > hdr.size())
      throw std::runtime_error("N2System: truncated SG-NPA entry");
    const std::uint32_t nameLen = readLe32(hdr.data() + pos);
    pos += 4;
    if (nameLen == 0 || nameLen > 1024 * 1024 ||
        pos + nameLen + 12 > hdr.size())
      throw std::runtime_error("N2System: invalid SG-NPA entry name length");

    const std::uint8_t *nameBytes = hdr.data() + pos;
    pos += nameLen;

    const std::uint32_t entrySize = readLe32(hdr.data() + pos);
    pos += 4;
    const std::uint64_t entryOffset = readLe64(hdr.data() + pos);
    pos += 8;

    bool isUtf16 = false;
    if (nameLen >= 2 && (nameLen % 2 == 0)) {
      for (std::size_t j = 0; j < nameLen; ++j) {
        if (nameBytes[j] == 0) {
          isUtf16 = true;
          break;
        }
      }
    }
    std::string name =
        isUtf16
            ? decodeUtf16LeToUtf8(nameBytes, nameLen)
            : std::string(reinterpret_cast<const char *>(nameBytes), nameLen);
    add({std::move(name), entryOffset, entrySize});
  }
}

void Archive::addAfsNames(std::string_view ini) {
  if (format_ != ArchiveFormat::AFS)
    throw std::invalid_argument(
        "N2System: AFS filename table on non-AFS archive");
  std::size_t position = 0;
  auto nextLine = [&]() -> std::string_view {
    if (position >= ini.size())
      throw std::runtime_error("N2System: truncated AFS filename table");
    const auto end = ini.find('\n', position);
    const auto finish = end == std::string_view::npos ? ini.size() : end;
    auto line = ini.substr(position, finish - position);
    position = end == std::string_view::npos ? ini.size() : end + 1;
    if (!line.empty() && line.back() == '\r')
      line.remove_suffix(1);
    return line;
  };
  auto decimal = [](std::string_view value) -> std::size_t {
    if (value.empty())
      throw std::runtime_error("N2System: missing AFS name index");
    std::size_t result = 0;
    for (char ch : value) {
      if (ch < '0' || ch > '9' ||
          result > (std::numeric_limits<std::size_t>::max() -
                    static_cast<unsigned>(ch - '0')) /
                       10)
        throw std::runtime_error("N2System: malformed AFS name index");
      result = result * 10 + static_cast<unsigned>(ch - '0');
    }
    return result;
  };
  const auto count = decimal(nextLine());
  if (count > entries_.size())
    throw std::runtime_error("N2System: AFS alias count exceeds entry count");
  auto newIndex = index_; // no mutation if the table is malformed
  for (std::size_t n = 0; n < count; ++n) {
    const auto line = nextLine();
    const auto comma = line.find(',');
    if (comma == std::string_view::npos)
      throw std::runtime_error("N2System: malformed AFS name record");
    const auto name = normalize(line.substr(0, comma));
    const auto ordinal = decimal(line.substr(comma + 1));
    if (ordinal >= entries_.size() || !safeArchiveName(name))
      throw std::runtime_error("N2System: unsafe AFS alias or invalid index");
    const auto [it, inserted] = newIndex.emplace(name, ordinal);
    if (!inserted && it->second != ordinal)
      throw std::runtime_error("N2System: conflicting AFS filename alias");
  }
  index_.swap(newIndex);
}

std::vector<std::string> Archive::names() const {
  std::vector<std::string> names;
  names.reserve(index_.size());
  for (const auto &entry : index_)
    names.push_back(entry.first);
  return names;
}

const ArchiveEntry *Archive::find(std::string_view path) const {
  const auto found = index_.find(normalize(path));
  return found == index_.end() ? nullptr : &entries_[found->second];
}

bool Archive::contains(std::string_view path) const {
  return find(path) != nullptr;
}

std::vector<std::uint8_t> Archive::read(const ArchiveEntry &entry) const {
  check(entry.offset, entry.size);
  std::vector<std::uint8_t> out(static_cast<std::size_t>(entry.size));
  if (entry.size > 0) {
    const auto got = readAt(entry, 0, out.data(), out.size());
    if (got != out.size())
      throw std::runtime_error("N2System: short read from archive entry");
  }
  return out;
}

std::size_t Archive::readAt(const ArchiveEntry &entry, std::uint64_t offset,
                            std::uint8_t *buffer, std::size_t capacity) const {
  check(entry.offset, entry.size);
  if (offset >= entry.size || capacity == 0)
    return 0;
  const auto length = static_cast<std::size_t>(
      std::min<std::uint64_t>(entry.size - offset, capacity));
  if (length && !buffer)
    throw std::invalid_argument("N2System: null archive stream buffer");
  const auto got = source_->readAt(entry.offset + offset, buffer, length);
  if (format_ == ArchiveFormat::SG_NPA) {
    for (std::size_t i = 0; i < got; ++i)
      buffer[i] ^= kSgNpaKey[(offset + i) & 7u];
  }
  return got;
}

std::vector<std::uint8_t> Archive::read(std::string_view path) const {
  const auto *entry = find(path);
  if (!entry)
    throw std::out_of_range("N2System: missing asset " + std::string(path));
  return read(*entry);
}

} // namespace N2System
