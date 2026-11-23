#include <Pmail/GcPack.hpp>

#include <algorithm>
#include <cctype>
#include <cstring>

namespace Kotonoha::Providers {

namespace {

uint32_t readU32LE(const uint8_t *p) {
  return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) |
         (static_cast<uint32_t>(p[2]) << 16) |
         (static_cast<uint32_t>(p[3]) << 24);
}

bool isSentinel(const uint8_t packed[12]) {
  for (int i = 0; i < 12; ++i) {
    if (packed[i] != 0xFF)
      return false;
  }
  return true;
}

std::string findFileCaseInsensitive(const Kotonoha::Filesystem::Filesystem &fs,
                                    const std::string &dir,
                                    const std::string &targetUpper) {
  try {
    for (const auto &info : fs.list(dir)) {
      if (info.type != Kotonoha::Filesystem::FileType::Regular)
        continue;
      std::string base = info.path;
      const auto slash = base.find_last_of('/');
      if (slash != std::string::npos)
        base = base.substr(slash + 1);
      std::string upper = base;
      std::transform(upper.begin(), upper.end(), upper.begin(),
                     [](unsigned char c) { return char(std::toupper(c)); });
      if (upper == targetUpper)
        return info.path;
    }
  } catch (const std::exception &) {
  }
  return {};
}

std::string findDirCaseInsensitive(const Kotonoha::Filesystem::Filesystem &fs,
                                   const std::string &dir,
                                   const std::string &targetUpper) {
  try {
    for (const auto &info : fs.list(dir)) {
      if (info.type != Kotonoha::Filesystem::FileType::Directory)
        continue;
      std::string base = info.path;
      const auto slash = base.find_last_of('/');
      if (slash != std::string::npos)
        base = base.substr(slash + 1);
      std::string upper = base;
      std::transform(upper.begin(), upper.end(), upper.begin(),
                     [](unsigned char c) { return char(std::toupper(c)); });
      if (upper == targetUpper)
        return info.path;
    }
  } catch (const std::exception &) {
  }
  return {};
}

} // namespace

std::string GcPackArchive::normalizeKey(const std::string &name) {
  std::string out;
  out.reserve(name.size());
  for (unsigned char c : name) {
    if (c == '\\')
      c = '/';
    out.push_back(static_cast<char>(std::toupper(c)));
  }
  const auto slash = out.find_last_of('/');
  if (slash != std::string::npos)
    out = out.substr(slash + 1);
  return out;
}

std::string GcPackArchive::decodePackedName(const uint8_t packed[12]) {
  std::string stem;
  std::string ext;
  stem.reserve(12);
  ext.reserve(4);
  for (int g = 0; g < 4; ++g) {
    const uint32_t v = (static_cast<uint32_t>(packed[g * 3]) << 16) |
                       (static_cast<uint32_t>(packed[g * 3 + 1]) << 8) |
                       static_cast<uint32_t>(packed[g * 3 + 2]);
    std::string &dst = (g < 3) ? stem : ext;
    for (int shift : {18, 12, 6, 0}) {
      const uint32_t code = (v >> shift) & 0x3F;
      if (code != 0)
        dst.push_back(static_cast<char>(code + 0x20));
    }
  }
  if (!ext.empty())
    return stem + "." + ext;
  return stem;
}

void GcPackArchive::encodePackedName(const std::string &name, uint8_t out[12]) {
  std::memset(out, 0, 12);
  std::string upper = normalizeKey(name);
  std::string stem = upper;
  std::string ext;
  const auto dot = upper.find('.');
  if (dot != std::string::npos) {
    stem = upper.substr(0, dot);
    ext = upper.substr(dot + 1);
  }
  uint8_t codes[16] = {};
  for (size_t i = 0; i < stem.size() && i < 12; ++i)
    codes[i] =
        static_cast<uint8_t>((static_cast<uint8_t>(stem[i]) - 0x20) & 0x3F);
  for (size_t i = 0; i < ext.size() && i < 4; ++i)
    codes[12 + i] =
        static_cast<uint8_t>((static_cast<uint8_t>(ext[i]) - 0x20) & 0x3F);

  for (int g = 0; g < 4; ++g) {
    const uint32_t v = (static_cast<uint32_t>(codes[g * 4 + 0]) << 18) |
                       (static_cast<uint32_t>(codes[g * 4 + 1]) << 12) |
                       (static_cast<uint32_t>(codes[g * 4 + 2]) << 6) |
                       static_cast<uint32_t>(codes[g * 4 + 3]);
    out[g * 3 + 0] = static_cast<uint8_t>((v >> 16) & 0xFF);
    out[g * 3 + 1] = static_cast<uint8_t>((v >> 8) & 0xFF);
    out[g * 3 + 2] = static_cast<uint8_t>(v & 0xFF);
  }
}

bool GcPackArchive::parseIndex(const uint8_t *indexData, size_t indexSize,
                               uint64_t totalFileSize) {
  entries_.clear();
  lookup_.clear();
  if (!indexData || indexSize < 0x410)
    return false;

  const uint32_t headerSectors = readU32LE(indexData);
  const uint64_t declaredIndexBytes = static_cast<uint64_t>(headerSectors) << 9;
  if (headerSectors < 3 || declaredIndexBytes > indexSize ||
      declaredIndexBytes > totalFileSize) {
    return false;
  }

  struct RawRecord {
    std::string name;
    uint32_t startSector = 0;
  };
  std::vector<RawRecord> raw;
  bool foundSentinel = false;
  uint32_t sentinelSector = 0;

  for (size_t pos = 0x400; pos + 16 <= declaredIndexBytes; pos += 16) {
    const uint8_t *rec = indexData + pos;
    const uint32_t sector = readU32LE(rec + 12);
    if (isSentinel(rec)) {
      foundSentinel = true;
      sentinelSector = sector;
      break;
    }
    raw.push_back({decodePackedName(rec), sector});
  }

  if (!foundSentinel || raw.empty())
    return false;

  const uint64_t sentinelOffset = static_cast<uint64_t>(sentinelSector) << 9;
  if (sentinelOffset > totalFileSize)
    return false;

  entries_.reserve(raw.size());
  for (size_t i = 0; i < raw.size(); ++i) {
    const uint32_t nextSector =
        (i + 1 < raw.size()) ? raw[i + 1].startSector : sentinelSector;
    if (nextSector < raw[i].startSector)
      return false;
    Entry e;
    e.name = raw[i].name;
    e.startSector = raw[i].startSector;
    e.offset = static_cast<uint64_t>(raw[i].startSector) << 9;
    e.size = (static_cast<uint64_t>(nextSector) << 9) - e.offset;
    if (e.offset + e.size > totalFileSize)
      return false;
    lookup_[normalizeKey(e.name)] = entries_.size();
    entries_.push_back(std::move(e));
  }
  return !entries_.empty();
}

bool GcPackArchive::open(
    std::unique_ptr<Kotonoha::Filesystem::FileHandle> file) {
  std::lock_guard lock(mutex_);
  entries_.clear();
  lookup_.clear();
  memory_.clear();
  file_.reset();
  if (!file)
    return false;

  const uint64_t totalSize = file->size();
  if (totalSize < 0x600)
    return false;

  uint8_t firstFour[4] = {};
  if (file->seek(0, Kotonoha::Filesystem::SeekOrigin::Begin) < 0 ||
      file->read(firstFour, 4) != 4)
    return false;

  const uint32_t headerSectors = readU32LE(firstFour);
  const uint64_t indexBytes = static_cast<uint64_t>(headerSectors) << 9;
  if (headerSectors < 3 || indexBytes > totalSize ||
      indexBytes > 16 * 1024 * 1024)
    return false;

  std::vector<uint8_t> indexBuf(static_cast<size_t>(indexBytes));
  if (file->seek(0, Kotonoha::Filesystem::SeekOrigin::Begin) < 0 ||
      file->read(indexBuf.data(), indexBuf.size()) != indexBuf.size()) {
    return false;
  }

  if (!parseIndex(indexBuf.data(), indexBuf.size(), totalSize))
    return false;

  file_ = std::move(file);
  return true;
}

bool GcPackArchive::openBytes(std::vector<uint8_t> bytes) {
  std::lock_guard lock(mutex_);
  entries_.clear();
  lookup_.clear();
  file_.reset();
  memory_.clear();
  if (bytes.size() < 0x600)
    return false;

  const uint32_t headerSectors = readU32LE(bytes.data());
  const uint64_t indexBytes = static_cast<uint64_t>(headerSectors) << 9;
  if (headerSectors < 3 || indexBytes > bytes.size())
    return false;

  if (!parseIndex(bytes.data(), static_cast<size_t>(indexBytes), bytes.size()))
    return false;

  memory_ = std::move(bytes);
  return true;
}

bool GcPackArchive::probe(Kotonoha::Filesystem::FileHandle &file) {
  const uint64_t totalSize = file.size();
  if (totalSize < 0x600)
    return false;
  uint8_t firstFour[4] = {};
  if (file.seek(0, Kotonoha::Filesystem::SeekOrigin::Begin) < 0 ||
      file.read(firstFour, 4) != 4)
    return false;
  const uint32_t headerSectors = readU32LE(firstFour);
  const uint64_t indexBytes = static_cast<uint64_t>(headerSectors) << 9;
  if (headerSectors < 3 || indexBytes > totalSize ||
      indexBytes > 16 * 1024 * 1024)
    return false;

  std::vector<uint8_t> indexBuf(static_cast<size_t>(indexBytes));
  if (file.seek(0, Kotonoha::Filesystem::SeekOrigin::Begin) < 0 ||
      file.read(indexBuf.data(), indexBuf.size()) != indexBuf.size()) {
    return false;
  }
  GcPackArchive probeArchive;
  return probeArchive.parseIndex(indexBuf.data(), indexBuf.size(), totalSize);
}

bool GcPackArchive::probeBytes(const uint8_t *data, size_t size) {
  if (!data || size < 0x600)
    return false;
  const uint32_t headerSectors = readU32LE(data);
  const uint64_t indexBytes = static_cast<uint64_t>(headerSectors) << 9;
  if (headerSectors < 3 || indexBytes > size)
    return false;
  GcPackArchive probeArchive;
  return probeArchive.parseIndex(data, static_cast<size_t>(indexBytes), size);
}

std::optional<GcPackArchive::Entry>
GcPackArchive::find(const std::string &name) const {
  std::lock_guard lock(mutex_);
  auto it = lookup_.find(normalizeKey(name));
  if (it == lookup_.end())
    return std::nullopt;
  return entries_[it->second];
}

bool GcPackArchive::contains(const std::string &name) const {
  std::lock_guard lock(mutex_);
  return lookup_.find(normalizeKey(name)) != lookup_.end();
}

std::vector<uint8_t> GcPackArchive::read(const std::string &name) const {
  std::lock_guard lock(mutex_);
  auto it = lookup_.find(normalizeKey(name));
  if (it == lookup_.end())
    return {};
  const Entry &e = entries_[it->second];
  if (e.size == 0)
    return {};

  if (!memory_.empty()) {
    if (e.offset + e.size > memory_.size())
      return {};
    return std::vector<uint8_t>(
        memory_.begin() + static_cast<ptrdiff_t>(e.offset),
        memory_.begin() + static_cast<ptrdiff_t>(e.offset + e.size));
  }

  if (!file_)
    return {};

  std::vector<uint8_t> out(static_cast<size_t>(e.size));
  if (file_->seek(static_cast<int64_t>(e.offset),
                  Kotonoha::Filesystem::SeekOrigin::Begin) < 0)
    return {};
  size_t totalRead = 0;
  while (totalRead < out.size()) {
    const size_t got =
        file_->read(out.data() + totalRead, out.size() - totalRead);
    if (got == 0)
      break;
    totalRead += got;
  }
  out.resize(totalRead);
  return out;
}

bool isLargePonPonSource(const Kotonoha::Filesystem::Filesystem &fs) {
  const std::string iniPath = findFileCaseInsensitive(fs, "", "MAINSYS.INI");
  if (iniPath.empty())
    return false;

  std::string packDir = findDirCaseInsensitive(fs, "", "PACK");
  std::string scriptPath;
  if (!packDir.empty())
    scriptPath = findFileCaseInsensitive(fs, packDir, "SCRIPT.DAT");
  if (scriptPath.empty())
    scriptPath = findFileCaseInsensitive(fs, "", "SCRIPT.DAT");
  if (scriptPath.empty())
    return false;

  try {
    auto handle = fs.open(scriptPath);
    return handle && GcPackArchive::probe(*handle);
  } catch (const std::exception &) {
    return false;
  }
}

bool isLargePonPonDirectory(const std::filesystem::path &directory) {
  if (directory.empty())
    return false;
  try {
    auto fs = Kotonoha::Filesystem::Filesystem::mount(directory.string());
    return fs && isLargePonPonSource(*fs);
  } catch (const std::exception &) {
    return false;
  }
}

} // namespace Kotonoha::Providers
