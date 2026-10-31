#include <Kotonoha/core/DiscImage.hpp>

#include <algorithm>
#include <cctype>
#include <cstring>
#include <stdexcept>

namespace Kotonoha {
namespace {

constexpr Uint32 logicalSector = 2048;

Uint32 le32(const Uint8 *p) {
  return Uint32(p[0]) | Uint32(p[1]) << 8 | Uint32(p[2]) << 16 |
         Uint32(p[3]) << 24;
}

std::string normalize(std::string_view path) {
  while (!path.empty() && (path.front() == '/' || path.front() == '\\'))
    path.remove_prefix(1);
  std::string out(path);
  for (char &c : out)
    c = c == '\\' ? '/' : char(std::toupper(static_cast<unsigned char>(c)));
  if (const auto version = out.find(';'); version != std::string::npos)
    out.resize(version);
  return out;
}

} // namespace

struct DiscImage::Stream {
  SDL_IOStream *io = nullptr;
  ~Stream() {
    if (io)
      SDL_CloseIO(io);
  }
};

DiscImage::DiscImage() = default;
DiscImage::DiscImage(const std::filesystem::path &file) { open(file); }
DiscImage::~DiscImage() = default;

void DiscImage::open(const std::filesystem::path &file) {
  std::string error;
  if (!tryOpen(file, &error))
    throw std::runtime_error("Cannot open disc image " + file.string() + ": " +
                             error);
}

bool DiscImage::tryOpen(const std::filesystem::path &file, std::string *error) {
  close();
  auto next = std::make_unique<Stream>();
  next->io = SDL_IOFromFile(file.string().c_str(), "rb");
  if (!next->io) {
    if (error)
      *error = SDL_GetError();
    return false;
  }
  stream = std::move(next);
  struct Layout {
    Uint32 sector, offset;
  };
  // Cooked ISO, raw MODE1 (16-byte sync + header), raw MODE2 form 1 (24-byte
  // sync + header + subheader) and MODE2 without the sync pattern.
  for (const Layout layout : {Layout{2048, 0}, Layout{2352, 16},
                              Layout{2352, 24}, Layout{2336, 8}}) {
    sector = layout.sector;
    dataOffset = layout.offset;
    std::vector<Uint8> pvd;
    if (!readSectors(16, 1, pvd) || pvd[0] != 1 ||
        std::memcmp(&pvd[1], "CD001", 5) != 0)
      continue;
    label.assign(reinterpret_cast<const char *>(&pvd[40]), 32);
    while (!label.empty() && (label.back() == ' ' || label.back() == '\0'))
      label.pop_back();
    try {
      walk(le32(&pvd[156 + 2]), le32(&pvd[156 + 10]), {}, 0);
    } catch (const std::exception &failure) {
      if (error)
        *error = failure.what();
      close();
      return false;
    }
    path = file;
    return true;
  }
  if (error)
    *error = "no ISO9660 volume found";
  close();
  return false;
}

bool DiscImage::isOpen() const { return stream != nullptr; }

void DiscImage::close() {
  std::lock_guard lock(mutex);
  stream.reset();
  entries.clear();
  byName.clear();
  label.clear();
  path.clear();
  sector = logicalSector;
  dataOffset = 0;
}

const std::filesystem::path &DiscImage::getPath() const { return path; }
const std::string &DiscImage::volumeLabel() const { return label; }
Uint32 DiscImage::sectorSize() const { return sector; }
const std::vector<DiscImage::Entry> &DiscImage::files() const {
  return entries;
}

const DiscImage::Entry *DiscImage::find(std::string_view name) const {
  const auto found = byName.find(normalize(name));
  return found == byName.end() ? nullptr : &entries[found->second];
}

bool DiscImage::readSectors(Uint32 lba, Uint32 count,
                            std::vector<Uint8> &out) const {
  std::lock_guard lock(mutex);
  if (!stream)
    return false;
  out.assign(std::size_t(count) * logicalSector, 0);
  for (Uint32 i = 0; i < count; ++i) {
    const Sint64 at = Sint64(lba + i) * sector + dataOffset;
    if (SDL_SeekIO(stream->io, at, SDL_IO_SEEK_SET) < 0 ||
        SDL_ReadIO(stream->io, out.data() + std::size_t(i) * logicalSector,
                   logicalSector) != logicalSector)
      return false;
  }
  return true;
}

void DiscImage::walk(Uint32 lba, Uint64 size, const std::string &prefix,
                     int depth) {
  if (depth > 32)
    throw std::runtime_error("directory tree too deep");
  std::vector<Uint8> data;
  if (!readSectors(lba, Uint32((size + logicalSector - 1) / logicalSector),
                   data))
    throw std::runtime_error("cannot read directory " + prefix);
  std::size_t i = 0;
  while (i < size && i < data.size()) {
    const Uint8 length = data[i];
    if (length == 0) {
      i = (i / logicalSector + 1) * logicalSector;
      continue;
    }
    if (length < 34 || i + length > data.size())
      break;
    const Uint8 *record = &data[i];
    i += length;
    const Uint8 nameLength = record[32];
    if (33u + nameLength > length)
      break;
    std::string name(reinterpret_cast<const char *>(record + 33), nameLength);
    if (nameLength == 1 && (name[0] == '\0' || name[0] == '\1'))
      continue;
    if (const auto version = name.find(';'); version != std::string::npos)
      name.resize(version);
    const Uint32 extent = le32(record + 2), extentSize = le32(record + 10);
    if (record[25] & 2) {
      walk(extent, extentSize, prefix + name + "/", depth + 1);
    } else {
      byName.emplace(normalize(prefix + name), entries.size());
      entries.push_back({prefix + name, extent, extentSize});
    }
  }
}

bool DiscImage::readInto(const Entry &entry, Uint64 offset, Uint64 size,
                         std::vector<Uint8> &out) const {
  if (offset > entry.size || size > entry.size - offset)
    return false;
  if (size == 0) {
    out.clear();
    return true;
  }
  const Uint32 first = Uint32(offset / logicalSector);
  const Uint32 last = Uint32((offset + size + logicalSector - 1) / logicalSector);
  std::vector<Uint8> sectors;
  if (!readSectors(entry.lba + first, last - first, sectors))
    return false;
  const auto skip = std::size_t(offset - Uint64(first) * logicalSector);
  out.assign(sectors.begin() + skip, sectors.begin() + skip + size);
  return true;
}

std::vector<Uint8> DiscImage::read(const Entry &entry, Uint64 offset,
                                   Uint64 size) const {
  std::vector<Uint8> out;
  if (!readInto(entry, offset, size, out))
    throw std::runtime_error("Cannot read " + entry.path + " from the disc");
  return out;
}

std::vector<Uint8> DiscImage::read(const Entry &entry) const {
  return read(entry, 0, entry.size);
}

std::vector<Uint8> DiscImage::read(std::string_view name) const {
  const Entry *entry = find(name);
  if (!entry)
    throw std::runtime_error("File not found on the disc: " +
                             std::string(name));
  return read(*entry);
}

DiscImageAssetProvider::DiscImageAssetProvider(
    const std::filesystem::path &file)
    : disc(std::make_shared<DiscImage>(file)) {}

DiscImageAssetProvider::DiscImageAssetProvider(
    std::shared_ptr<const DiscImage> image)
    : disc(std::move(image)) {
  if (!disc || !disc->isOpen())
    throw std::invalid_argument("DiscImageAssetProvider needs an open disc");
}

const DiscImage &DiscImageAssetProvider::getDisc() const { return *disc; }

std::shared_ptr<const DiscImage> DiscImageAssetProvider::shareDisc() const {
  return disc;
}

std::string DiscImageAssetProvider::name() const {
  return "Disc (" +
         (disc->volumeLabel().empty() ? disc->getPath().filename().string()
                                      : disc->volumeLabel()) +
         ")";
}

bool DiscImageAssetProvider::contains(const std::string &id) const {
  return disc->find(id) != nullptr;
}

std::vector<Uint8> DiscImageAssetProvider::read(const std::string &id) const {
  return disc->read(id);
}

std::vector<std::string>
DiscImageAssetProvider::list(const std::string &prefix) const {
  const std::string wanted = normalize(prefix);
  std::vector<std::string> ids;
  for (const auto &entry : disc->files())
    if (normalize(entry.path).compare(0, wanted.size(), wanted) == 0)
      ids.push_back(entry.path);
  return ids;
}

} // namespace Kotonoha
