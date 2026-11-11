// Copyright 2026 Kotonoha contributors. SPDX-License-Identifier: MIT
#include <LucaSystem/LucaSystemAssets.hpp>
#include <LucaSystem/CzImage.hpp>
// PAK asset provider module.

#include <Kotonoha/core/FileSystem.hpp>
#include <Kotonoha/core/Log.hpp>

#include <algorithm>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <stdexcept>

namespace Kotonoha::Providers {
using namespace LucaSystem;

Kotonoha::SurfacePtr LucaSystemAssets::loadSurfaceAsset(const std::string &id) const {
  const auto bytes = read(id);
  if (bytes.size() < 3 || bytes[0] != 'C' || bytes[1] != 'Z')
    return AssetProvider::loadSurfaceAsset(id);
  const auto image = DecodeCzImage(bytes);
  Kotonoha::SurfacePtr surface(
      SDL_CreateSurface(image.width, image.height, SDL_PIXELFORMAT_RGBA32));
  if (!surface)
    throw std::runtime_error(SDL_GetError());
  for (int y = 0; y < image.height; ++y)
    std::memcpy(static_cast<Uint8 *>(surface->pixels) + size_t(y) * surface->pitch,
                image.rgba.data() + size_t(y) * image.width * 4, size_t(image.width) * 4);
  return surface;
}

} // namespace Kotonoha::Providers

namespace {

uint32_t readU32(const std::vector<std::uint8_t> &data, size_t off) {
  if (off + 4 > data.size())
    throw std::runtime_error("PAK: truncated uint32 at offset " +
                             std::to_string(off));
  return static_cast<uint32_t>(data[off]) |
         (static_cast<uint32_t>(data[off + 1]) << 8) |
         (static_cast<uint32_t>(data[off + 2]) << 16) |
         (static_cast<uint32_t>(data[off + 3]) << 24);
}

uint16_t readU16(const std::vector<std::uint8_t> &data, size_t off) {
  if (off + 2 > data.size())
    throw std::runtime_error("PAK: truncated uint16 at offset " +
                             std::to_string(off));
  return static_cast<uint16_t>(data[off]) |
         (static_cast<uint16_t>(data[off + 1]) << 8);
}

std::string readCString(const std::vector<std::uint8_t> &data, size_t &cursor) {
  size_t end = data.size();
  for (size_t i = cursor; i < data.size(); ++i) {
    if (data[i] == 0) {
      end = i;
      break;
    }
  }
  std::string result(data.begin() + cursor, data.begin() + end);
  cursor = (end < data.size()) ? end + 1 : data.size();
  return result;
}

} // namespace

namespace LucaSystem {

PakArchive::PakArchive(std::filesystem::path path) : path_(std::move(path)) {
  load();
}

void PakArchive::load() {
  std::ifstream file(path_, std::ios::binary);
  if (!file)
    throw std::runtime_error("Cannot open PAK archive: " + path_.string());
  std::uintmax_t fileSize = std::filesystem::file_size(path_);
  if (fileSize < 36)
    throw std::runtime_error("PAK too small for header: " + path_.string());

  headerData_.resize(36);
  file.read(reinterpret_cast<char *>(headerData_.data()), 36);
  if (!file)
    throw std::runtime_error("Cannot read PAK header: " + path_.string());

  header_.headerLength = readU32(headerData_, 0);
  header_.fileCount = readU32(headerData_, 4);
  header_.idStart = readU32(headerData_, 8);
  header_.blockSize = readU32(headerData_, 12);
  header_.unk2 = readU32(headerData_, 16);
  header_.unk3 = readU32(headerData_, 20);
  header_.unk4 = readU32(headerData_, 24);
  header_.unk5 = readU32(headerData_, 28);
  header_.flags = readU32(headerData_, 32);
  named_ = (header_.flags & 512u) != 0u;

  if (header_.blockSize == 0)
    throw std::runtime_error("PAK has zero block size: " + path_.string());
  if (header_.headerLength < 36 || header_.headerLength > fileSize)
    throw std::runtime_error("PAK has invalid header length: " +
                             path_.string());

  headerData_.resize(header_.headerLength);
  file.seekg(0);
  file.read(reinterpret_cast<char *>(headerData_.data()),
            static_cast<std::streamsize>(headerData_.size()));
  if (!file)
    throw std::runtime_error("Cannot read complete PAK header: " +
                             path_.string());

  // Scan forward from offset 32 until a uint32 equals headerLength/blockSize.
  const uint32_t target = header_.headerLength / header_.blockSize;
  offsetPos = 32;
  while (offsetPos + 4 <= headerData_.size() &&
         readU32(headerData_, offsetPos) != target)
    offsetPos += 4;
  if (offsetPos + 4 > headerData_.size())
    throw std::runtime_error("PAK offset table not found: " + path_.string());

  // Read fileCount * (offset, length) pairs. Offsets are in block units.
  const size_t tableEnd = offsetPos + size_t(header_.fileCount) * 8;
  if (tableEnd > headerData_.size())
    throw std::runtime_error("PAK offset table exceeds header: " +
                             path_.string());
  entries_.reserve(header_.fileCount);
  for (uint32_t i = 0; i < header_.fileCount; ++i) {
    const size_t base = offsetPos + size_t(i) * 8;
    uint32_t offsetBlocks = readU32(headerData_, base);
    uint32_t length = readU32(headerData_, base + 4);
    PakEntry entry;
    entry.index = i;
    entry.id = header_.idStart + i;
    entry.offset = uint64_t(offsetBlocks) * header_.blockSize;
    entry.length = length;
    entries_.push_back(std::move(entry));
    byId_[entry.id] = entries_.size() - 1;
  }

  // Read null-terminated file names, if present.
  if (named_) {
    const uint32_t nameBase = readU32(headerData_, offsetPos - 4);
    if (nameBase >= fileSize)
      throw std::runtime_error("PAK name table points outside file: " +
                               path_.string());
    if (nameBase < headerData_.size()) {
      size_t cursor = nameBase;
      for (auto &entry : entries_) {
        entry.name = readCString(headerData_, cursor);
        byName_[entry.name] = &entry - &entries_.front();
      }
    } else {
      file.clear();
      file.seekg(nameBase);
      for (auto &entry : entries_) {
        std::getline(file, entry.name, '\0');
        if (!file)
          throw std::runtime_error("PAK has truncated name table: " +
                                   path_.string());
        byName_[entry.name] = &entry - &entries_.front();
      }
    }
  } else {
    for (size_t i = 0; i < entries_.size(); ++i)
      entries_[i].name = std::to_string(entries_[i].id);
  }
}

const PakEntry *PakArchive::findByName(std::string_view name) const {
  auto it = byName_.find(std::string(name));
  return it == byName_.end() ? nullptr : &entries_[it->second];
}

const PakEntry *PakArchive::findById(uint32_t id) const {
  auto it = byId_.find(id);
  return it == byId_.end() ? nullptr : &entries_[it->second];
}

const PakEntry *PakArchive::findByIndex(uint32_t index) const {
  if (index >= entries_.size())
    return nullptr;
  return &entries_[index];
}

std::vector<std::uint8_t> PakArchive::read(const PakEntry &entry) const {
  const auto fileSize = std::filesystem::file_size(path_);
  if (entry.offset + entry.length > fileSize)
    throw std::runtime_error("PAK entry data exceeds file: " + entry.name);
  std::ifstream file(path_, std::ios::binary);
  file.seekg(static_cast<std::streamoff>(entry.offset));
  std::vector<std::uint8_t> result(entry.length);
  file.read(reinterpret_cast<char *>(result.data()),
            static_cast<std::streamsize>(result.size()));
  if (!file)
    throw std::runtime_error("Cannot read PAK entry: " + entry.name);
  return result;
}

std::vector<std::uint8_t> PakArchive::read(std::string_view name) const {
  const PakEntry *entry = findByName(name);
  if (!entry)
    throw std::runtime_error("PAK entry not found: " + std::string(name));
  return read(*entry);
}

// ---------------------------------------------------------------------------
// LucaSystemAssets
// ---------------------------------------------------------------------------

} // namespace LucaSystem

namespace Kotonoha::Providers {
using namespace LucaSystem;

LucaSystemAssets::LucaSystemAssets(std::filesystem::path directory,
                                   std::filesystem::path overlayDirectory)
    : AssetProvider(), directory_(std::move(directory)) {
  if (!std::filesystem::is_regular_file(directory_ / "SCRIPT.PAK") &&
      std::filesystem::is_regular_file(directory_ / "files" / "SCRIPT.PAK"))
    directory_ /= "files";
  setOverlay(directory_);

  const std::vector<std::string> pakOrder = {"PARAM.PAK", "SCRIPT.PAK",
                                             "SYSSE.PAK"};
  for (const auto &name : pakOrder) {
    auto file = directory_ / name;
    if (std::filesystem::is_regular_file(file))
      mountPak(file);
  }
  // Any remaining PAK archives mount after the core ones. LucaSystem titles
  // conventionally split these into image/music/se/voice/font subfolders.
  std::vector<std::filesystem::path> others;
  for (const auto &entry :
       std::filesystem::recursive_directory_iterator(directory_)) {
    if (!entry.is_regular_file())
      continue;
    auto ext = entry.path().extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(),
                   [](unsigned char c) { return std::tolower(c); });
    if (ext == ".pak") {
      auto name = entry.path().filename().string();
      if (entry.path().parent_path() != directory_ ||
          (name != "PARAM.PAK" && name != "SCRIPT.PAK" && name != "SYSSE.PAK"))
        others.push_back(entry.path());
    }
  }
  std::sort(others.begin(), others.end());
  for (const auto &file : others)
    mountPak(file);

  SDL_Log("LucaSystem: mounted %zu archives in %s",
          archives_.size() + (paramPak_ ? 1 : 0) + (scriptPak_ ? 1 : 0) +
              (syssePak_ ? 1 : 0),
          directory_.string().c_str());
}

void LucaSystemAssets::mountPak(const std::filesystem::path &file) {
  auto archive = std::make_unique<PakArchive>(file);
  const PakArchive *raw = archive.get();

  // Register every entry under its logical name, first mount wins.
  for (const auto &entry : raw->entries()) {
    idToPak_.emplace(entry.name, raw);
    numericIdToPak_.emplace(entry.id, raw);
    if (file.parent_path().filename() == "image")
      imageIdToPak_.emplace(entry.id, raw);
  }

  const char *role = "image";
  if (file.filename() == "PARAM.PAK") {
    paramPak_ = std::move(archive);
    role = "PARAM";
  } else if (file.filename() == "SCRIPT.PAK") {
    scriptPak_ = std::move(archive);
    role = "SCRIPT";
  } else if (file.filename() == "SYSSE.PAK") {
    syssePak_ = std::move(archive);
    role = "SYSSE";
  } else {
    archives_.push_back(std::move(archive));
  }
  SDL_Log("LucaSystem: mounted %s archive %s (%zu entries)", role,
          file.filename().string().c_str(), raw->entries().size());
}

bool LucaSystemAssets::containsAsset(const std::string &id) const {
  return idToPak_.count(id) > 0;
}

std::vector<std::uint8_t>
LucaSystemAssets::readAsset(const std::string &id) const {
  auto it = idToPak_.find(id);
  if (it == idToPak_.end())
    throw std::runtime_error("LucaSystem: asset not found: " + id);
  return it->second->read(id);
}

std::vector<std::uint8_t> LucaSystemAssets::readAsset(uint32_t id) const {
  auto it = numericIdToPak_.find(id);
  if (it == numericIdToPak_.end())
    throw std::runtime_error("LucaSystem: numeric asset not found: " +
                             std::to_string(id));
  const auto *entry = it->second->findById(id);
  if (!entry)
    throw std::runtime_error("LucaSystem: inconsistent numeric asset index");
  return it->second->read(*entry);
}

std::vector<std::uint8_t> LucaSystemAssets::readImageAsset(uint32_t id) const {
  auto it = imageIdToPak_.find(id);
  if (it == imageIdToPak_.end())
    throw std::runtime_error("LucaSystem: image asset not found: " +
                             std::to_string(id));
  const auto *entry = it->second->findById(id);
  if (!entry)
    throw std::runtime_error("LucaSystem: inconsistent image asset index");
  return it->second->read(*entry);
}

std::vector<std::string>
LucaSystemAssets::listAsset(const std::string &prefix) const {
  std::vector<std::string> result;
  for (const auto &[name, _] : idToPak_) {
    if (prefix.empty() || name.rfind(prefix, 0) == 0)
      result.push_back(name);
  }
  std::sort(result.begin(), result.end());
  return result;
}

std::string LucaSystemAssets::localPathAsset(const std::string &id) const {
  return {}; // archived assets have no loose filesystem path
}

} // namespace Kotonoha::Providers
