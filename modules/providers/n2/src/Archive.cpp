// Copyright 2026 Kotonoha contributors. SPDX-License-Identifier: MIT
// Original C++17 reader based on publicly documented AFS/NPA structures.
#include <N2System/Archive.hpp>
#include <algorithm>
#include <cstring>
#include <limits>
#include <stdexcept>
#include <utility>
namespace N2System {
namespace {
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
Archive::Archive(std::vector<std::uint8_t> bytes) : bytes_(std::move(bytes)) {
  if (bytes_.size() >= 4 && bytes_[0] == 'A' && bytes_[1] == 'F' &&
      bytes_[2] == 'S' && bytes_[3] == 0) {
    format_ = ArchiveFormat::AFS;
    parseAfs();
  } else if (bytes_.size() >= 7 && bytes_[0] == 'N' && bytes_[1] == 'P' &&
             bytes_[2] == 'A' && bytes_[3] == 1 && bytes_[4] == 0 &&
             bytes_[5] == 0 && bytes_[6] == 0) {
    format_ = ArchiveFormat::NPA;
    parseNpa();
  } else {
    throw std::runtime_error("N2System: unsupported archive magic");
  }
}
void Archive::check(std::uint64_t offset, std::uint64_t size) const {
  if (offset > bytes_.size() || size > bytes_.size() - offset)
    throw std::runtime_error("N2System: archive data exceeds bounds");
}
std::uint32_t Archive::u32(std::size_t p) const {
  check(p, 4);
  return std::uint32_t(bytes_[p]) | (std::uint32_t(bytes_[p + 1]) << 8) |
         (std::uint32_t(bytes_[p + 2]) << 16) |
         (std::uint32_t(bytes_[p + 3]) << 24);
}
std::uint64_t Archive::u64(std::size_t p) const {
  return std::uint64_t(u32(p)) | (std::uint64_t(u32(p + 4)) << 32);
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
  check(0, 8);
  const auto count = u32(4);
  if (count > 1000000)
    throw std::runtime_error("N2System: excessive AFS entry count");
  const std::uint64_t tableEnd = 8ull + std::uint64_t(count) * 8;
  check(tableEnd, 4);
  const auto names = u32(static_cast<std::size_t>(tableEnd));
  if (names)
    check(names, std::uint64_t(count) * 48);
  for (std::uint32_t i = 0; i < count; ++i) {
    const auto at = static_cast<std::size_t>(8ull + std::uint64_t(i) * 8);
    const auto offset = u32(at), size = u32(at + 4);
    std::string name = "entry/" + std::to_string(i);
    if (names) {
      const auto begin = static_cast<std::size_t>(std::uint64_t(names) +
                                                  std::uint64_t(i) * 48);
      const auto end =
          std::find(bytes_.begin() + begin, bytes_.begin() + begin + 32, 0);
      if (end != bytes_.begin() + begin)
        name.assign(bytes_.begin() + begin, end);
    }
    add({std::move(name), offset, size});
  }
}
void Archive::parseNpa() {
  check(0, 41);
  const auto first = u32(7), second = u32(11);
  if (bytes_[15])
    throw std::runtime_error("N2System: compressed NPA unsupported");
  if (bytes_[16])
    throw std::runtime_error("N2System: encrypted NPA unsupported");
  const auto count = u32(17), directories = u32(21), files = u32(25);
  if (count > 1000000 || std::uint64_t(directories) + files != count)
    throw std::runtime_error("N2System: inconsistent NPA counts");
  if (u64(29) != 0)
    throw std::runtime_error("N2System: bad NPA reserved field");
  const std::uint64_t start = 41ull + u32(37);
  check(start, 0);
  std::size_t pos = 41;
  std::uint32_t actualFiles = 0;
  for (std::uint32_t i = 0; i < count; ++i) {
    const auto nameLen = u32(pos);
    pos += 4;
    if (nameLen == 0 || nameLen > 1024 * 1024)
      throw std::runtime_error("N2System: invalid NPA name length");
    check(pos, std::uint64_t(nameLen) + 17);
    std::string name;
    name.reserve(nameLen);
    for (std::uint32_t j = 0; j < nameLen; ++j)
      name.push_back(static_cast<char>(
          std::uint8_t(bytes_[pos + j] + nameKey(j, i, first, second))));
    pos += nameLen;
    const auto type = bytes_[pos++];
    (void)u32(pos);
    pos += 4;
    const auto offset = u32(pos);
    pos += 4;
    const auto packed = u32(pos);
    pos += 4;
    const auto unpacked = u32(pos);
    pos += 4;
    if (type == 2) {
      ++actualFiles;
      if (packed != unpacked)
        throw std::runtime_error("N2System: compressed NPA entry unsupported");
      add({std::move(name), start + offset, unpacked});
    }
  }
  if (pos > start || actualFiles != files)
    throw std::runtime_error("N2System: inconsistent NPA table");
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
  const auto begin = bytes_.begin() + static_cast<std::size_t>(entry.offset);
  return {begin, begin + static_cast<std::size_t>(entry.size)};
}
std::size_t Archive::readAt(const ArchiveEntry &entry, std::uint64_t offset,
                            std::uint8_t *buffer, std::size_t capacity) const {
  check(entry.offset, entry.size);
  if (offset > entry.size)
    return 0;
  const auto length = static_cast<std::size_t>(
      std::min<std::uint64_t>(entry.size - offset, capacity));
  if (length && !buffer)
    throw std::invalid_argument("N2System: null archive stream buffer");
  if (length)
    std::memcpy(buffer, bytes_.data() + entry.offset + offset, length);
  return length;
}
std::vector<std::uint8_t> Archive::read(std::string_view path) const {
  const auto *entry = find(path);
  if (!entry)
    throw std::out_of_range("N2System: missing asset " + std::string(path));
  return read(*entry);
}
} // namespace N2System
