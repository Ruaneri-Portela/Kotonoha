#include <Kotonoha/core/DiscImage.hpp>

#include <algorithm>
#include <cctype>
#include <cstring>
#include <Kotonoha/Filesystem/Adapters/StreamAdapter.hpp>
#include <map>
#include <stdexcept>
#include <tuple>

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
  IOStreamPtr io;
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
  next->io.reset(SDL_IOFromFile(file.string().c_str(), "rb"));
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
  for (const Layout layout :
       {Layout{2048, 0}, Layout{2352, 16}, Layout{2352, 24}, Layout{2336, 8}}) {
    sector = layout.sector;
    dataOffset = layout.offset;
    std::vector<Uint8> pvd;
    if (!readSectors(16, 1, pvd) || pvd[0] != 1 ||
        std::memcmp(&pvd[1], "CD001", 5) != 0)
      continue;
    label.assign(reinterpret_cast<const char *>(&pvd[40]), 32);
    while (!label.empty() && (label.back() == ' ' || label.back() == '\0'))
      label.pop_back();
    for (Uint32 descriptor = 17; descriptor < 80; ++descriptor) {
      std::vector<Uint8> volume;
      if (!readSectors(descriptor, 1, volume) ||
          std::memcmp(&volume[1], "CD001", 5) != 0 || volume[0] == 255)
        break;
      if (volume[0] == 2 && volume[88] == '%' && volume[89] == '/' &&
          (volume[90] == '@' || volume[90] == 'C' || volume[90] == 'E')) {
        pvd = std::move(volume);
        joliet = true;
        break;
      }
    }
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
  directoryEntries.clear();
  paths.clear();
  byName.clear();
  label.clear();
  path.clear();
  sector = logicalSector;
  dataOffset = 0;
  joliet = false;
}

const std::filesystem::path &DiscImage::getPath() const { return path; }
const std::string &DiscImage::volumeLabel() const { return label; }
Uint32 DiscImage::sectorSize() const { return sector; }
const std::vector<DiscImage::Entry> &DiscImage::files() const {
  return entries;
}
const std::vector<std::string> &DiscImage::directories() const {
  return directoryEntries;
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
    if (SDL_SeekIO(stream->io.get(), at, SDL_IO_SEEK_SET) < 0 ||
        SDL_ReadIO(stream->io.get(),
                   out.data() + std::size_t(i) * logicalSector,
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
    if (length < 34 || i + length > data.size() || i + length > size)
      throw std::runtime_error("invalid ISO directory record");
    const Uint8 *record = &data[i];
    i += length;
    const Uint8 nameLength = record[32];
    if (33u + nameLength > length || nameLength == 0)
      throw std::runtime_error("invalid ISO filename");
    std::string name(reinterpret_cast<const char *>(record + 33), nameLength);
    if (nameLength == 1 && (name[0] == '\0' || name[0] == '\1'))
      continue;
    if (joliet) {
      if (nameLength % 2)
        throw std::runtime_error("invalid Joliet filename");
      for (size_t c = 0; c < name.size(); c += 2)
        if (name[c] == '\0' && name[c + 1] == '\0')
          throw std::runtime_error("invalid Joliet filename");
      SDLMemoryPtr<char> utf8(
          SDL_iconv_string("UTF-8", "UTF-16BE", name.data(), name.size()));
      if (!utf8)
        throw std::runtime_error("cannot decode Joliet filename");
      name = utf8.get();
    }
    if (const auto version = name.find(';'); version != std::string::npos)
      name.resize(version);
    if (name.empty() || name == "." || name == ".." ||
        name.find_first_of("/\\:") != std::string::npos ||
        name.find('\0') != std::string::npos)
      throw std::runtime_error("unsafe ISO filename");
    const Uint32 extent = le32(record + 2), extentSize = le32(record + 10);
    if (!paths.insert(normalize(prefix + name)).second)
      throw std::runtime_error("duplicate ISO path: " + prefix + name);
    if (record[25] & 2) {
      directoryEntries.push_back(prefix + name);
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
  const Uint32 last =
      Uint32((offset + size + logicalSector - 1) / logicalSector);
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

bool DiscImageAssetProvider::containsAsset(const std::string &id) const {
  return disc->find(id) != nullptr;
}

std::vector<Uint8>
DiscImageAssetProvider::readAsset(const std::string &id) const {
  return disc->read(id);
}

std::vector<std::string>
DiscImageAssetProvider::listAsset(const std::string &prefix) const {
  const std::string wanted = normalize(prefix);
  std::vector<std::string> ids;
  for (const auto &entry : disc->files())
    if (normalize(entry.path).compare(0, wanted.size(), wanted) == 0)
      ids.push_back(entry.path);
  return ids;
}

bool isISOImage(const std::filesystem::path &path) {
  return normalize(path.extension().string()) == ".ISO" &&
         !std::filesystem::is_directory(path);
}

std::filesystem::path gameDataDirectory(const std::filesystem::path &source) {
  if (!isISOImage(source))
    return source;
  struct Mount {
    std::filesystem::path directory;
    ~Mount() {
      if (directory.empty())
        return;
      std::error_code error;
      std::filesystem::remove_all(directory, error);
      if (error)
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                    "Cannot remove temporary ISO data %s: %s",
                    directory.string().c_str(), error.message().c_str());
    }
  };
  static std::mutex mountMutex;
  using Key = std::tuple<std::filesystem::path, std::uintmax_t,
                         std::filesystem::file_time_type>;
  static std::map<Key, std::unique_ptr<Mount>> mounts;
  std::lock_guard lock(mountMutex);
  const auto canonical = std::filesystem::canonical(source);
  const Key key{canonical, std::filesystem::file_size(canonical),
                std::filesystem::last_write_time(canonical)};
  if (const auto found = mounts.find(key); found != mounts.end())
    return found->second->directory;
  DiscImage disc(canonical);
  auto mount = std::make_unique<Mount>();
  const auto base = std::filesystem::temp_directory_path();
  for (int attempt = 0; attempt < 100; ++attempt) {
    const auto candidate =
        base / ("kotonoha-iso-" + std::to_string(SDL_GetTicksNS()) + "-" +
                std::to_string(attempt));
    if (std::filesystem::create_directory(candidate)) {
      mount->directory = candidate;
      break;
    }
  }
  if (mount->directory.empty())
    throw std::runtime_error("Cannot create temporary ISO mount");
  std::filesystem::permissions(mount->directory,
                               std::filesystem::perms::owner_all,
                               std::filesystem::perm_options::replace);
  SDL_Log("Opening ISO internally: %s (%zu files) -> %s",
          canonical.string().c_str(), disc.files().size(),
          mount->directory.string().c_str());
  for (const auto &directory : disc.directories())
    std::filesystem::create_directories(mount->directory /
                                        std::filesystem::u8path(directory));
  for (const auto &entry : disc.files()) {
    const auto relative = std::filesystem::u8path(entry.path);
    if (relative.is_absolute() || relative.has_root_name() ||
        relative.lexically_normal() != relative)
      throw std::runtime_error("Unsafe ISO path: " + entry.path);
    const auto destination = mount->directory / relative;
    std::filesystem::create_directories(destination.parent_path());
    if (std::filesystem::exists(destination))
      throw std::runtime_error("Conflicting ISO path: " + entry.path);
    Kotonoha::Filesystem::OutputFile output(destination, std::ios::binary);
    if (!output)
      throw std::runtime_error("Cannot create ISO file: " + entry.path);
    for (Uint64 offset = 0; offset < entry.size;) {
      const auto bytes = disc.read(
          entry, offset, std::min<Uint64>(1024 * 1024, entry.size - offset));
      if (!output.write(reinterpret_cast<const char *>(bytes.data()),
                        std::streamsize(bytes.size())))
        throw std::runtime_error("Cannot write ISO file: " + entry.path);
      offset += bytes.size();
    }
    output.close();
    if (!output)
      throw std::runtime_error("Cannot finish ISO file: " + entry.path);
  }
  const auto directory = mount->directory;
  mounts.emplace(key, std::move(mount));
  return directory;
}

} // namespace Kotonoha
