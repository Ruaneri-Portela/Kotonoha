// Copyright 2026 Kotonoha contributors. SPDX-License-Identifier: MIT
#include <LucaSystem/CzImage.hpp>
#include <LucaSystem/LucaSystemAssets.hpp>
// PAK asset provider module.

#include <Kotonoha/Filesystem/Adapters/StreamAdapter.hpp>
#include <Kotonoha/Filesystem/StoragePaths.hpp>
#include <Kotonoha/core/Log.hpp>
#include <algorithm>
#include <array>
#include <cctype>
#include <charconv>
#include <cstring>
#include <filesystem>
#include <limits>
#include <optional>
#include <stdexcept>

namespace Kotonoha::Providers {
using namespace LucaSystem;

namespace {
class VfsAssetStream final : public Kotonoha::AssetStream {
public:
  explicit VfsAssetStream(
      std::unique_ptr<Kotonoha::Filesystem::FileHandle> handle)
      : handle_(std::move(handle)) {}

  std::uint64_t size() const override {
    const auto s = handle_->size();
    return s > 0 ? static_cast<std::uint64_t>(s) : 0;
  }
  std::uint64_t tell() const override {
    const auto t = handle_->tell();
    return t > 0 ? static_cast<std::uint64_t>(t) : 0;
  }
  std::size_t read(Uint8 *buffer, std::size_t count) override {
    const auto got = handle_->read(buffer, count);
    return got > 0 ? static_cast<std::size_t>(got) : 0;
  }
  bool seek(std::uint64_t position) override {
    return handle_->seek(static_cast<std::int64_t>(position),
                         Kotonoha::Filesystem::SeekOrigin::Begin) >= 0;
  }

private:
  std::unique_ptr<Kotonoha::Filesystem::FileHandle> handle_;
};
} // namespace

Kotonoha::SurfacePtr
LucaSystemAssets::loadSurfaceAsset(const std::string &id) const {
  const auto bytes = read(id);
  if (bytes.size() < 3 || bytes[0] != 'C' || bytes[1] != 'Z')
    return AssetProvider::loadSurfaceAsset(id);
  const auto image = DecodeCzImage(bytes);
  Kotonoha::SurfacePtr surface(
      SDL_CreateSurface(image.width, image.height, SDL_PIXELFORMAT_RGBA32));
  if (!surface)
    throw std::runtime_error(SDL_GetError());
  for (int y = 0; y < image.height; ++y)
    std::memcpy(static_cast<Uint8 *>(surface->pixels) +
                    size_t(y) * surface->pitch,
                image.rgba.data() + size_t(y) * image.width * 4,
                size_t(image.width) * 4);
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

struct VoiceId {
  uint32_t entry = 0;
  std::optional<uint32_t> clip;
};

bool parseAssetNumber(std::string_view text, uint32_t &value) {
  if (text.empty())
    return false;
  const auto end = text.data() + text.size();
  const auto parsed = std::from_chars(text.data(), end, value);
  return parsed.ec == std::errc{} && parsed.ptr == end;
}

std::optional<VoiceId> parseVoiceId(std::string_view suffix) {
  VoiceId id;
  const auto colon = suffix.find(':');
  if (!parseAssetNumber(suffix.substr(0, colon), id.entry))
    return std::nullopt;
  if (colon != std::string_view::npos &&
      !parseAssetNumber(suffix.substr(colon + 1), id.clip.emplace()))
    return std::nullopt;
  return id;
}

bool hasOggCapture(const std::vector<std::uint8_t> &bytes, size_t offset) {
  return offset <= bytes.size() && bytes.size() - offset >= 4 &&
         std::memcmp(bytes.data() + offset, "OggS", 4) == 0;
}

std::vector<std::uint8_t> selectVoiceClip(std::vector<std::uint8_t> bytes,
                                          uint32_t clipIndex) {
  // LuckSystem's voice/oggpak.go skips seven header bytes, then reads
  // repeated LE (sample rate, byte length, Ogg data) records until EOF.
  // It leaves the header opaque. Detect a named OggPak header or an Ogg
  // capture at the first record's payload offset for other header contents.
  constexpr size_t headerSize = 7;
  constexpr size_t recordSize = 8;
  constexpr std::string_view magic = "oggpak";
  const bool knownHeader =
      bytes.size() >= magic.size() &&
      std::equal(magic.begin(), magic.end(), bytes.begin(),
                 [](char c, std::uint8_t b) { return c == std::tolower(b); });
  if (hasOggCapture(bytes, 0) ||
      (!knownHeader && !hasOggCapture(bytes, headerSize + recordSize))) {
    if (clipIndex != 0)
      throw std::runtime_error("LucaSystem: voice clip index out of range");
    return bytes;
  }

  size_t selected = 0;
  size_t selectedLength = 0;
  size_t index = 0;
  size_t offset = headerSize;
  if (bytes.size() <= offset)
    throw std::runtime_error("OggPak: missing clip records");
  while (offset < bytes.size()) {
    if (bytes.size() - offset < recordSize)
      throw std::runtime_error("OggPak: truncated clip header");
    // The engine's decoder gets the sample rate from the Ogg stream.
    const auto length = readU32(bytes, offset + 4);
    offset += recordSize;
    if (length > bytes.size() - offset)
      throw std::runtime_error("OggPak: truncated clip payload");
    if (length < 4 || !hasOggCapture(bytes, offset))
      throw std::runtime_error("OggPak: invalid Ogg capture");
    if (index++ == clipIndex) {
      selected = offset;
      selectedLength = length;
    }
    offset += length;
  }
  if (selectedLength == 0)
    throw std::runtime_error("OggPak: voice clip index out of range");
  return {bytes.begin() + selected, bytes.begin() + selected + selectedLength};
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

PakArchive::PakArchive(std::filesystem::path path)
    : PakArchive(Kotonoha::Filesystem::Filesystem::mount(
                     (path.has_parent_path() ? path.parent_path()
                                             : std::filesystem::path("."))
                         .string()),
                 path.filename().string()) {
  path_ = std::move(path);
}

PakArchive::PakArchive(std::shared_ptr<Kotonoha::Filesystem::Filesystem> source,
                       std::string sourceId)
    : path_(sourceId), source_(std::move(source)),
      sourceId_(std::move(sourceId)) {
  if (!source_)
    throw std::invalid_argument("PAK archive must have a mounted source");
  load();
}

void PakArchive::load() {
  auto handle = source_->open(sourceId_);
  const auto length = handle->size();
  if (length < 0)
    throw std::runtime_error("PAK archive size unavailable: " + sourceId_);
  const auto fileSize = static_cast<std::uint64_t>(length);
  Kotonoha::Filesystem::InputFile file(std::move(handle));
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

  const uint32_t target = header_.headerLength / header_.blockSize;
  offsetPos = 32;
  while (offsetPos + 4 <= headerData_.size() &&
         readU32(headerData_, offsetPos) != target)
    offsetPos += 4;
  if (offsetPos + 4 > headerData_.size())
    throw std::runtime_error("PAK offset table not found: " + path_.string());

  const size_t tableEnd = offsetPos + size_t(header_.fileCount) * 8;
  if (tableEnd > headerData_.size())
    throw std::runtime_error("PAK offset table exceeds header: " +
                             path_.string());
  entries_.reserve(header_.fileCount);
  for (uint32_t i = 0; i < header_.fileCount; ++i) {
    const size_t base = offsetPos + size_t(i) * 8;
    uint32_t offsetBlocks = readU32(headerData_, base);
    uint32_t entryLength = readU32(headerData_, base + 4);
    PakEntry entry;
    entry.index = i;
    entry.id = header_.idStart + i;
    entry.offset = uint64_t(offsetBlocks) * header_.blockSize;
    entry.length = entryLength;
    entries_.push_back(std::move(entry));
    byId_[entry.id] = entries_.size() - 1;
  }

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
  auto handle = source_->open(sourceId_);
  const auto size = handle->size();
  if (size < 0 || entry.offset > static_cast<std::uint64_t>(size) ||
      entry.length > static_cast<std::uint64_t>(size) - entry.offset)
    throw std::runtime_error("PAK entry data exceeds file: " + entry.name);
  Kotonoha::Filesystem::InputFile file(std::move(handle));
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

void PakArchive::repack(
    std::ostream &output,
    const std::map<uint32_t, std::filesystem::path> &replacements) const {
  const uint64_t block = header_.blockSize;
  if (header_.headerLength % block != 0)
    throw std::runtime_error("PAK: cannot repack an unaligned header");
  if (named_ && offsetPos < 40)
    throw std::runtime_error("PAK: invalid name table pointer field");
  for (const auto &[id, path] : replacements) {
    if (!findById(id))
      throw std::runtime_error("PAK: unknown replacement ID " +
                               std::to_string(id));
    if (!std::filesystem::is_regular_file(path))
      throw std::runtime_error("PAK: not a replacement file: " + path.string());
  }

  auto source = source_->open(sourceId_);
  const auto sourceSize = source->size();
  if (sourceSize < 0)
    throw std::runtime_error("PAK: archive size unavailable");
  Kotonoha::Filesystem::InputFile input(std::move(source));
  auto repackedHeader = headerData_;
  const auto putU32 = [&](size_t pos, uint64_t value) {
    if (value > std::numeric_limits<uint32_t>::max())
      throw std::runtime_error("PAK: repacked offset or length exceeds format");
    for (size_t shift = 0; shift < 32; shift += 8)
      repackedHeader.at(pos++) = static_cast<uint8_t>(value >> shift);
  };
  const auto padding = [block](uint64_t end) {
    return (block - end % block) % block;
  };
  const bool externalNames =
      named_ && readU32(headerData_, offsetPos - 4) >= headerData_.size();
  uint64_t offset = header_.headerLength;
  std::vector<uint32_t> lengths;
  lengths.reserve(entries_.size());
  for (const auto &entry : entries_) {
    uint64_t length = entry.length;
    const auto replacement = replacements.find(entry.id);
    if (replacement != replacements.end()) {
      length = std::filesystem::file_size(replacement->second);
    } else if (entry.offset > uint64_t(sourceSize) ||
               length > uint64_t(sourceSize) - entry.offset) {
      throw std::runtime_error("PAK: original entry exceeds archive");
    }
    const size_t row = offsetPos + size_t(entry.index) * 8;
    putU32(row, offset / block);
    putU32(row + 4, length);
    lengths.push_back(static_cast<uint32_t>(length));
    offset += length;
    offset += padding(offset);
  }
  if (externalNames)
    putU32(offsetPos - 4, offset);
  // The reader discovers the start of the table by scanning for the first
  // entry's block offset. An external name pointer must not shadow it.
  for (size_t pos = 32; pos < offsetPos; pos += 4) {
    if (readU32(repackedHeader, pos) == header_.headerLength / block)
      throw std::runtime_error("PAK: repacked header would hide offset table");
  }

  const auto write = [&](const char *data, std::streamsize count) {
    if (!output.write(data, count))
      throw std::runtime_error("PAK: cannot write repacked archive");
  };
  write(reinterpret_cast<const char *>(repackedHeader.data()),
        static_cast<std::streamsize>(repackedHeader.size()));
  std::array<char, 64 * 1024> buffer{};
  const auto copy = [&](std::istream &file, uint64_t count) {
    while (count != 0) {
      const auto chunk = std::min<uint64_t>(count, buffer.size());
      if (!file.read(buffer.data(), static_cast<std::streamsize>(chunk)))
        throw std::runtime_error("PAK: cannot read complete entry payload");
      write(buffer.data(), static_cast<std::streamsize>(chunk));
      count -= chunk;
    }
  };
  const std::array<char, 4096> zeros{};
  for (const auto &entry : entries_) {
    const auto length = lengths[entry.index];
    const auto replacement = replacements.find(entry.id);
    if (replacement != replacements.end()) {
      Kotonoha::Filesystem::InputFile file(replacement->second);
      if (!file)
        throw std::runtime_error("PAK: cannot open replacement file");
      copy(file, length);
      if (file.peek() != std::char_traits<char>::eof() || file.bad())
        throw std::runtime_error("PAK: replacement changed while reading");
    } else {
      if (!input.seekg(static_cast<std::streamoff>(entry.offset)))
        throw std::runtime_error("PAK: cannot seek to original entry");
      copy(input, length);
    }
    for (auto remaining = padding(length); remaining != 0;) {
      const auto chunk = std::min<uint64_t>(remaining, zeros.size());
      write(zeros.data(), static_cast<std::streamsize>(chunk));
      remaining -= chunk;
    }
  }
  if (externalNames) {
    for (const auto &entry : entries_)
      write(entry.name.c_str(),
            static_cast<std::streamsize>(entry.name.size() + 1));
  }
}

} // namespace LucaSystem

namespace Kotonoha::Providers {
using namespace LucaSystem;

LucaSystemAssets::LucaSystemAssets(std::filesystem::path directory,
                                   std::filesystem::path overlayDirectory)
    : LucaSystemAssets(
          Kotonoha::Filesystem::Filesystem::mount(directory.string()),
          overlayDirectory) {
  directory_ = std::move(directory);
  if (overlayDirectory.empty())
    setOverlay(directory_);
}

LucaSystemAssets::LucaSystemAssets(
    std::shared_ptr<Kotonoha::Filesystem::Filesystem> source,
    std::filesystem::path overlayDirectory)
    : source_(std::move(source)) {
  if (!source_)
    throw std::invalid_argument("LucaSystem requires a mounted source");
  if (!overlayDirectory.empty()) {
    source_->addOverlay(overlayDirectory.string());
    setOverlay(overlayDirectory);
  }
  if (!source_->exists("SCRIPT.PAK") && source_->exists("files/SCRIPT.PAK"))
    prefix_ = "files/";
  const std::vector<std::string> core = {"PARAM.PAK", "SCRIPT.PAK",
                                         "SYSSE.PAK"};
  for (const auto &name : core) {
    const auto key = prefix_ + name;
    if (source_->exists(key) &&
        source_->stat(key).type == Kotonoha::Filesystem::FileType::Regular)
      mountPak(key);
  }

  std::vector<std::string> other;
  std::vector<std::string> dirs{prefix_.empty() ? "" : "files"};
  while (!dirs.empty()) {
    auto directory = std::move(dirs.back());
    dirs.pop_back();
    for (const auto &file : source_->list(directory)) {
      if (file.type == Kotonoha::Filesystem::FileType::Directory) {
        dirs.push_back(file.path);
        continue;
      }
      auto name = std::filesystem::path(file.path);
      auto ext = name.extension().string();
      std::transform(ext.begin(), ext.end(), ext.begin(),
                     [](unsigned char ch) { return char(std::tolower(ch)); });
      if (ext != ".pak")
        continue;
      auto fileUpper = name.filename().string();
      std::transform(fileUpper.begin(), fileUpper.end(), fileUpper.begin(),
                     [](unsigned char ch) { return char(std::toupper(ch)); });
      const bool corePak =
          std::find(core.begin(), core.end(), fileUpper) != core.end();
      std::string pathLower = file.path;
      std::transform(pathLower.begin(), pathLower.end(), pathLower.begin(),
                     [](unsigned char ch) { return char(std::tolower(ch)); });
      std::string prefixLower = prefix_;
      std::transform(prefixLower.begin(), prefixLower.end(),
                     prefixLower.begin(),
                     [](unsigned char ch) { return char(std::tolower(ch)); });
      if (pathLower.compare(0, prefixLower.size(), prefixLower) == 0 &&
          (pathLower.substr(prefixLower.size()).find('/') !=
               std::string::npos ||
           !corePak))
        other.push_back(file.path);
    }
  }
  std::sort(other.begin(), other.end());
  for (const auto &id : other)
    mountPak(id);
  SDL_Log("LucaSystem: mounted %zu PAK archives",
          archives_.size() + (paramPak_ ? 1 : 0) + (scriptPak_ ? 1 : 0) +
              (syssePak_ ? 1 : 0));
}

void LucaSystemAssets::mountPak(const std::string &path) {
  auto archive = std::make_unique<PakArchive>(source_, path);
  const PakArchive *raw = archive.get();
  const auto file = std::filesystem::path(path);
  std::string fileNameUpper = file.filename().string();
  std::transform(fileNameUpper.begin(), fileNameUpper.end(),
                 fileNameUpper.begin(),
                 [](unsigned char c) { return char(std::toupper(c)); });
  std::string parentLower = file.parent_path().filename().string();
  std::transform(parentLower.begin(), parentLower.end(), parentLower.begin(),
                 [](unsigned char c) { return char(std::tolower(c)); });

  const bool isImageArchive =
      parentLower == "image" || fileNameUpper.find("CG") != std::string::npos ||
      fileNameUpper.find("IMAGE") != std::string::npos ||
      fileNameUpper.find("PARTS") != std::string::npos ||
      fileNameUpper.find("MANUAL") != std::string::npos;
  const bool isVoiceArchive = parentLower == "voice" ||
                              fileNameUpper.find("VOICE") != std::string::npos;
  const bool isBgmArchive = parentLower == "music" ||
                            fileNameUpper.find("MUSIC") != std::string::npos ||
                            fileNameUpper.find("BGM") != std::string::npos;
  const bool isSeArchive =
      parentLower == "se" ||
      (fileNameUpper == "SE.PAK" || fileNameUpper == "SYSSE.PAK");

  for (const auto &entry : raw->entries()) {
    if (!entry.name.empty()) {
      idToPak_.emplace(entry.name, raw);
    }
    idToPak_.emplace(std::to_string(entry.id), raw);
    numericIdToPak_.emplace(entry.id, raw);
    if (isImageArchive)
      imageIdToPak_.emplace(entry.id, raw);
    if (isVoiceArchive)
      voiceIdToPak_.emplace(entry.id, raw);
    if (isBgmArchive)
      bgmIdToPak_.emplace(entry.id, raw);
    if (isSeArchive)
      seIdToPak_.emplace(entry.id, raw);
  }

  const char *role = isImageArchive   ? "image"
                     : isVoiceArchive ? "voice"
                     : isBgmArchive   ? "music"
                     : isSeArchive    ? "se"
                                      : "data";
  if (fileNameUpper == "PARAM.PAK") {
    paramPak_ = std::move(archive);
    role = "PARAM";
  } else if (fileNameUpper == "SCRIPT.PAK") {
    scriptPak_ = std::move(archive);
    role = "SCRIPT";
  } else if (fileNameUpper == "SYSSE.PAK") {
    syssePak_ = std::move(archive);
    role = "SYSSE";
  } else {
    archives_.push_back(std::move(archive));
  }
  SDL_Log("LucaSystem: mounted %s archive %s (%zu entries)", role,
          file.filename().string().c_str(), raw->entries().size());
}

std::string LucaSystemAssets::resolveLooseId(const std::string &id) const {
  if (!source_ || id.empty())
    return {};
  std::string norm = id;
  std::replace(norm.begin(), norm.end(), '\\', '/');
  while (!norm.empty() && norm.front() == '/')
    norm.erase(norm.begin());
  if (norm.empty())
    return {};
  if (source_->exists(norm) &&
      source_->stat(norm).type == Kotonoha::Filesystem::FileType::Regular)
    return norm;
  if (!prefix_.empty()) {
    const std::string prefixed = prefix_ + norm;
    if (source_->exists(prefixed) &&
        source_->stat(prefixed).type == Kotonoha::Filesystem::FileType::Regular)
      return prefixed;
  }
  return {};
}

bool LucaSystemAssets::containsAsset(const std::string &id) const {
  if (id.rfind("image:", 0) == 0) {
    uint32_t number = 0;
    return parseAssetNumber(std::string_view(id).substr(6), number) &&
           (imageIdToPak_.count(number) != 0 || number == 65000 ||
            number == 65001);
  }
  if (id.rfind("voice:", 0) == 0) {
    const auto voice = parseVoiceId(std::string_view(id).substr(6));
    if (!voice || !hasVoiceAsset(voice->entry))
      return false;
    if (!voice->clip)
      return true;
    try {
      (void)readVoiceAsset(voice->entry, *voice->clip);
      return true;
    } catch (const std::exception &) {
      return false;
    }
  }
  if (id.rfind("bgm:", 0) == 0) {
    try {
      return hasBgmAsset(static_cast<uint32_t>(std::stoul(id.substr(4))));
    } catch (...) {
      return false;
    }
  }
  if (id.rfind("se:", 0) == 0) {
    try {
      return hasSeAsset(static_cast<uint32_t>(std::stoul(id.substr(3))));
    } catch (...) {
      return false;
    }
  }
  if (idToPak_.count(id) > 0)
    return true;
  return !resolveLooseId(id).empty();
}

std::string LucaSystemAssets::overlayId(const std::string &id) const {
  if (id.rfind("image:", 0) == 0) {
    uint32_t number = 0;
    if (parseAssetNumber(std::string_view(id).substr(6), number))
      return "image/" + std::to_string(number) + ".cz";
  }
  if (id.rfind("voice:", 0) == 0) {
    if (const auto voice = parseVoiceId(std::string_view(id).substr(6))) {
      return "voice/" + std::to_string(voice->entry) + "/" +
             std::to_string(voice->clip.value_or(0)) + ".ogg";
    }
  }
  return id;
}

std::vector<std::uint8_t>
LucaSystemAssets::readAsset(const std::string &id) const {
  if (id.rfind("image:", 0) == 0) {
    uint32_t number = 0;
    if (!parseAssetNumber(std::string_view(id).substr(6), number) ||
        !containsAsset(id))
      throw std::runtime_error("LucaSystem: image asset not found: " + id);
    return readImageAsset(number);
  }
  if (id.rfind("voice:", 0) == 0) {
    const auto voice = parseVoiceId(std::string_view(id).substr(6));
    if (!voice)
      throw std::runtime_error("LucaSystem: invalid voice asset id: " + id);
    if (voice->clip && !hasVoiceAsset(voice->entry))
      throw std::runtime_error("LucaSystem: voice asset not found: " + id);
    return readVoiceAsset(voice->entry, voice->clip.value_or(0));
  }
  if (id.rfind("bgm:", 0) == 0)
    return readBgmAsset(static_cast<uint32_t>(std::stoul(id.substr(4))));
  if (id.rfind("se:", 0) == 0)
    return readSeAsset(static_cast<uint32_t>(std::stoul(id.substr(3))));
  auto it = idToPak_.find(id);
  if (it != idToPak_.end())
    return it->second->read(id);
  try {
    uint32_t numId = static_cast<uint32_t>(std::stoul(id));
    auto nit = numericIdToPak_.find(numId);
    if (nit != numericIdToPak_.end()) {
      const auto *entry = nit->second->findById(numId);
      if (entry)
        return nit->second->read(*entry);
    }
  } catch (...) {
  }
  const auto loose = resolveLooseId(id);
  if (!loose.empty()) {
    auto handle = source_->open(loose);
    if (handle)
      return Kotonoha::Filesystem::readAll(*handle);
  }
  throw std::runtime_error("LucaSystem: asset not found: " + id);
}

std::vector<std::uint8_t> LucaSystemAssets::readAsset(uint32_t id) const {
  auto it = numericIdToPak_.find(id);
  if (it != numericIdToPak_.end()) {
    const auto *entry = it->second->findById(id);
    if (entry)
      return it->second->read(*entry);
  }
  auto sit = idToPak_.find(std::to_string(id));
  if (sit != idToPak_.end())
    return sit->second->read(std::to_string(id));
  throw std::runtime_error("LucaSystem: numeric asset not found: " +
                           std::to_string(id));
}

std::vector<std::uint8_t> LucaSystemAssets::readImageAsset(uint32_t id) const {
  auto it = imageIdToPak_.find(id);
  if (it != imageIdToPak_.end()) {
    const auto *entry = it->second->findById(id);
    if (entry)
      return it->second->read(*entry);
  }
  // Virtual solid fills used by LucaSystem (65000 = white, 65001 = black).
  // Must check BEFORE numericIdToPak_ because SYSVOICE.PAK uses 65000..65012!
  if (id == 65000 || id == 65001) {
    const std::uint8_t c = (id == 65000) ? 255 : 0;
    // Synthesize a minimal CZ0 1x1 RGBA image; runtime scales 65000/65001 to
    // full 1280x720 canvas.
    std::vector<std::uint8_t> cz0 = {
        'C', 'Z', '0', 0,  16, 0, 0, 0, // magic + headerLen=16
        1,   0,   1,   0,  32, 0, 0, 0, // 1x1 32bpp
        c,   c,   c,   255};
    return cz0;
  }
  auto nit = numericIdToPak_.find(id);
  if (nit != numericIdToPak_.end()) {
    const auto *entry = nit->second->findById(id);
    if (entry)
      return nit->second->read(*entry);
  }
  auto sit = idToPak_.find(std::to_string(id));
  if (sit != idToPak_.end())
    return sit->second->read(std::to_string(id));
  throw std::runtime_error("LucaSystem: image asset not found: " +
                           std::to_string(id));
}

bool LucaSystemAssets::hasVoiceAsset(uint32_t id) const {
  return voiceIdToPak_.count(id) > 0;
}

bool LucaSystemAssets::hasBgmAsset(uint32_t id) const {
  return bgmIdToPak_.count(id) > 0;
}

bool LucaSystemAssets::hasSeAsset(uint32_t id) const {
  return seIdToPak_.count(id) > 0;
}

std::vector<std::uint8_t>
LucaSystemAssets::readVoiceAsset(uint32_t id, uint32_t clipIndex) const {
  auto it = voiceIdToPak_.find(id);
  if (it != voiceIdToPak_.end()) {
    const auto *entry = it->second->findById(id);
    if (entry)
      return selectVoiceClip(it->second->read(*entry), clipIndex);
  }
  if (clipIndex != 0)
    throw std::runtime_error("LucaSystem: voice asset not found: " +
                             std::to_string(id));
  return readAsset(id);
}

std::vector<std::uint8_t> LucaSystemAssets::readBgmAsset(uint32_t id) const {
  auto it = bgmIdToPak_.find(id);
  if (it != bgmIdToPak_.end()) {
    const auto *entry = it->second->findById(id);
    if (entry)
      return it->second->read(*entry);
  }
  return readAsset(id);
}

std::vector<std::uint8_t> LucaSystemAssets::readSeAsset(uint32_t id) const {
  auto it = seIdToPak_.find(id);
  if (it != seIdToPak_.end()) {
    const auto *entry = it->second->findById(id);
    if (entry)
      return it->second->read(*entry);
  }
  return readAsset(id);
}

std::unique_ptr<Kotonoha::AssetStream>
LucaSystemAssets::openStream(const std::string &id) const {
  if (idToPak_.count(id) > 0)
    return nullptr;
  const auto loose = resolveLooseId(id);
  if (!loose.empty()) {
    auto handle = source_->open(loose);
    if (handle)
      return std::make_unique<VfsAssetStream>(std::move(handle));
  }
  return nullptr;
}

std::vector<std::string>
LucaSystemAssets::listAsset(const std::string &prefix) const {
  std::vector<std::string> result;
  // Expose the exact logical resource IDs supported by read()/loadSurface()
  // and by the native VM, rather than only named archive entries. Image,
  // voice, BGM and SE archives often contain numeric-only entries.
  const auto append = [&](const std::string &id) {
    if (prefix.empty() || id.rfind(prefix, 0) == 0)
      result.push_back(id);
  };
  for (const auto &[name, _] : idToPak_)
    append(name);
  const auto numeric = [&](const auto &entries, const char *kind) {
    for (const auto &[id, pak] : entries)
      append(std::string(kind) + ":" + std::to_string(id));
  };
  numeric(imageIdToPak_, "image");
  numeric(voiceIdToPak_, "voice");
  numeric(bgmIdToPak_, "bgm");
  numeric(seIdToPak_, "se");
  append("image:65000"); // VM synthetic white fill
  append("image:65001"); // VM synthetic black fill
  std::sort(result.begin(), result.end());
  result.erase(std::unique(result.begin(), result.end()), result.end());
  return result;
}

std::string LucaSystemAssets::localPathAsset(const std::string &id) const {
  if (directory_.empty() || idToPak_.count(id) > 0)
    return {};
  const auto loose = resolveLooseId(id);
  if (loose.empty())
    return {};
  std::error_code ec;
  const auto candidate = directory_ / loose;
  if (std::filesystem::is_regular_file(candidate, ec))
    return candidate.string();
  return {};
}

} // namespace Kotonoha::Providers
