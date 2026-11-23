#pragma once
// CMP archive container reader. Image/byte codecs live in modules/formats.
#include <SummerRadish/Codec.hpp>

#include <cstdint>
#include <filesystem>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace Kotonoha::Filesystem {
class Filesystem;
class FileHandle;
} // namespace Kotonoha::Filesystem

namespace SummerRadish {
enum class CmpKeyProfile {
  Auto,
  SummerRadish1,
  SummerRadish2,
  MeoZeroKey,
  LostM,
};
struct Entry {
  std::string name;
  uint32_t offset = 0, size = 0;
  bool compressed = false;
};
class Pack {
  std::shared_ptr<Kotonoha::Filesystem::Filesystem> filesystem;
  std::string sourceId;
  std::map<std::string, Entry> directory;
  CmpKeyProfile detectedProfile = CmpKeyProfile::Auto;

public:
  explicit Pack(
      const std::filesystem::path &,
      CmpKeyProfile profile = CmpKeyProfile::Auto); // legacy physical-path adapter
  Pack(std::shared_ptr<Kotonoha::Filesystem::Filesystem> filesystem,
       std::string sourceId, CmpKeyProfile profile = CmpKeyProfile::Auto);
  CmpKeyProfile keyProfile() const { return detectedProfile; }
  const std::map<std::string, Entry> &entries() const { return directory; }
  bool contains(const std::string &name) const;
  const Entry &entry(const std::string &name) const;
  Bytes read(const std::string &name) const;
  Bytes readRange(const std::string &name, size_t offset, size_t size) const;
  size_t readEntry(const Entry &entry, void *dst, size_t offset,
                   size_t size) const;
  std::unique_ptr<Kotonoha::Filesystem::FileHandle>
  openEntryStream(const std::string &name) const;
};
} // namespace SummerRadish
