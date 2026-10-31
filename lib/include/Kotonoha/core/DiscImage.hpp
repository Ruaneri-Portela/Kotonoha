#pragma once

#include <Kotonoha/core/AssetProvider.hpp>

#include <cstdint>
#include <filesystem>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace Kotonoha {

// Read-only ISO9660 file system of a CD/DVD image: a plain .iso (2048-byte
// sectors) or a raw .bin track (2352-byte MODE1 / MODE2 form 1 sectors, or
// 2336-byte MODE2 sectors without sync). Lookups ignore case and the ";1"
// version suffix; subdirectories use '/' ("MOVIE/OPENING.PSS").
class DiscImage {
public:
  struct Entry {
    std::string path; // without the ";1" version suffix
    Uint32 lba = 0;
    Uint64 size = 0;
  };

  DiscImage();
  // Opens immediately; throws std::runtime_error on failure.
  explicit DiscImage(const std::filesystem::path &path);
  ~DiscImage();
  DiscImage(const DiscImage &) = delete;
  DiscImage &operator=(const DiscImage &) = delete;

  // Replaces the open image; throws std::runtime_error on failure.
  void open(const std::filesystem::path &path);
  // Non-throwing variant that reports the reason through `error`.
  bool tryOpen(const std::filesystem::path &path, std::string *error = nullptr);
  bool isOpen() const;
  void close();

  const std::filesystem::path &getPath() const;
  const std::string &volumeLabel() const;
  Uint32 sectorSize() const;
  const std::vector<Entry> &files() const;
  const Entry *find(std::string_view path) const;

  // Throw std::runtime_error when the entry is missing or the range is
  // outside it. Reads are serialized, so one image may be shared by threads.
  std::vector<Uint8> read(std::string_view path) const;
  std::vector<Uint8> read(const Entry &entry) const;
  std::vector<Uint8> read(const Entry &entry, Uint64 offset, Uint64 size) const;
  // Non-throwing range read for hot paths; false on any failure.
  bool readInto(const Entry &entry, Uint64 offset, Uint64 size,
                std::vector<Uint8> &out) const;

private:
  struct Stream;
  std::unique_ptr<Stream> stream;
  mutable std::mutex mutex;
  std::filesystem::path path;
  std::vector<Entry> entries;
  std::unordered_map<std::string, std::size_t> byName;
  std::string label;
  Uint32 sector = 2048;
  Uint32 dataOffset = 0;

  bool readSectors(Uint32 lba, Uint32 count, std::vector<Uint8> &out) const;
  void walk(Uint32 lba, Uint64 size, const std::string &prefix, int depth);
};

// Asset provider over a disc image: identifiers are ISO paths.
class DiscImageAssetProvider : public AssetProvider {
  std::shared_ptr<const DiscImage> disc;

public:
  explicit DiscImageAssetProvider(const std::filesystem::path &path);
  explicit DiscImageAssetProvider(std::shared_ptr<const DiscImage> disc);
  const DiscImage &getDisc() const;
  std::shared_ptr<const DiscImage> shareDisc() const;
  std::string name() const override;
  bool contains(const std::string &id) const override;
  std::vector<Uint8> read(const std::string &id) const override;
  std::vector<std::string> list(const std::string &prefix = {}) const override;
};

} // namespace Kotonoha
