#include <ZeroNoTsukaima/Disc.hpp>

#include <SDL3/SDL_iostream.h>

#include <algorithm>
#include <cstring>

namespace ZeroNoTsukaima {

struct Disc::File {
  SDL_IOStream *io = nullptr;
  ~File() {
    if (io != nullptr) SDL_CloseIO(io);
  }
};

namespace {

uint32_t U32(const uint8_t *p) {
  return p[0] | (p[1] << 8) | (p[2] << 16) | (static_cast<uint32_t>(p[3]) << 24);
}

} // namespace

Disc::Disc() = default;
Disc::~Disc() = default;

bool Disc::ReadSectors(uint32_t lba, uint32_t count,
                       std::vector<uint8_t> *out) const {
  out->assign(static_cast<size_t>(count) * 2048, 0);
  for (uint32_t i = 0; i < count; ++i) {
    const Sint64 at = static_cast<Sint64>(lba + i) * sectorSize + dataOffset;
    if (SDL_SeekIO(file->io, at, SDL_IO_SEEK_SET) < 0) return false;
    if (SDL_ReadIO(file->io, out->data() + static_cast<size_t>(i) * 2048,
                   2048) != 2048) {
      return false;
    }
  }
  return true;
}

bool Disc::Walk(uint32_t lba, uint32_t size, const std::string &prefix,
                std::string *error) {
  std::vector<uint8_t> data;
  if (!ReadSectors(lba, (size + 2047) / 2048, &data)) {
    if (error) *error = "failed to read directory extent";
    return false;
  }
  size_t i = 0;
  while (i < size) {
    const uint8_t len = data[i];
    if (len == 0) {
      i = (i / 2048 + 1) * 2048;
      continue;
    }
    if (i + len > data.size() || len < 34) break;
    const uint8_t *e = &data[i];
    const uint8_t nameLen = e[32];
    std::string name(reinterpret_cast<const char *>(e + 33), nameLen);
    const bool dir = (e[25] & 2) != 0;
    const uint32_t extent = U32(e + 2), extentSize = U32(e + 10);
    i += len;
    if (nameLen == 1 && (name[0] == '\0' || name[0] == '\1')) continue;
    const auto version = name.find(';');
    if (version != std::string::npos) name.resize(version);
    if (dir) {
      if (!Walk(extent, extentSize, prefix + name + "/", error)) return false;
    } else {
      files.push_back({prefix + name, extent, extentSize});
    }
  }
  return true;
}

bool Disc::Open(const std::string &path, std::string *error) {
  file = std::make_unique<File>();
  file->io = SDL_IOFromFile(path.c_str(), "rb");
  if (file->io == nullptr) {
    if (error) *error = "cannot open " + path;
    return false;
  }
  files.clear();

  struct Layout {
    uint32_t sector, offset;
  };
  // Sector layouts: cooked 2048, raw 2352 MODE1 (16-byte sync/header), raw
  // 2352 MODE2 form1 (24-byte header).
  for (const Layout l : {Layout{2048, 0}, Layout{2352, 16}, Layout{2352, 24}}) {
    sectorSize = l.sector;
    dataOffset = l.offset;
    std::vector<uint8_t> pvd;
    if (!ReadSectors(16, 1, &pvd)) continue;
    if (pvd[0] != 1 || std::memcmp(&pvd[1], "CD001", 5) != 0) continue;
    label.assign(reinterpret_cast<const char *>(&pvd[40]), 32);
    while (!label.empty() && (label.back() == ' ' || label.back() == '\0')) {
      label.pop_back();
    }
    return Walk(U32(&pvd[156 + 2]), U32(&pvd[156 + 10]), "", error);
  }
  if (error) *error = "no ISO9660 volume found";
  return false;
}

const Disc::Entry *Disc::Find(const std::string &name) const {
  const auto it = std::find_if(files.begin(), files.end(),
                               [&](const Entry &e) { return e.name == name; });
  return it == files.end() ? nullptr : &*it;
}

bool Disc::ReadRange(const Entry &entry, uint64_t offset, uint64_t size,
                     std::vector<uint8_t> *out) const {
  if (offset + size > entry.size) return false;
  const uint32_t first = static_cast<uint32_t>(offset / 2048);
  const uint32_t last = static_cast<uint32_t>((offset + size + 2047) / 2048);
  std::vector<uint8_t> sectors;
  if (!ReadSectors(entry.lba + first, last - first, &sectors)) return false;
  const size_t skip = static_cast<size_t>(offset - uint64_t(first) * 2048);
  out->assign(sectors.begin() + skip, sectors.begin() + skip + size);
  return true;
}

bool Disc::Read(const Entry &entry, std::vector<uint8_t> *out) const {
  return ReadRange(entry, 0, entry.size, out);
}

std::vector<ArchiveEntry> ParseArchiveIndex(const std::vector<uint8_t> &hd) {
  std::vector<ArchiveEntry> entries;
  uint64_t offset = 0;
  for (size_t i = 0; i + 4 <= hd.size(); i += 4) {
    const uint32_t size = U32(&hd[i]);
    entries.push_back({offset, size});
    offset += (static_cast<uint64_t>(size) + 2047) / 2048 * 2048;
  }
  return entries;
}

} // namespace ZeroNoTsukaima
