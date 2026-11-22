#pragma once
// Public format API of Kotonoha::ProviderCMP.
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
using Bytes = std::vector<uint8_t>;
enum class CmpKeyProfile {
  Auto,
  SummerRadish1,
  SummerRadish2,
  MeoZeroKey,
  LostM,
};
uint32_t word(const Bytes &, size_t);
Bytes decompress(const Bytes &, size_t size, size_t offset = 0);
Bytes decodeStackWork(const Bytes &);
std::string utf16(const Bytes &, size_t offset, size_t units);
std::string canonical(std::string);
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
struct Image {
  int width = 0, height = 0, x = 0, y = 0;
  // Conventional straight-alpha RGBA for generic engine consumers.
  Bytes rgba;
  // CHR/TEXB retain premultiplied source RGB and original 0..128 coverage.
  // Coverage >=128 copies the source; lower values use the native /256 blend.
  // Rows and channels use the same top-down RGBA layout as rgba.
  Bytes nativePixels;
};
Image decodeImage(const Bytes &, const std::string &extension);
struct CharacterAnimation {
  int actor = 0;
  int selector = 0;
  size_t currentFrame = 0;
  std::vector<Image> frames;
};
// SubAnime records following the CHR base stream. Frame selection and timing
// belong to the caller; each frame retains its absolute native x/y position.
std::vector<CharacterAnimation> decodeCharacterAnimations(const Bytes &);
// Native SubAnime selection updates every record matching both keys. Invalid
// indices preserve its previous frame; unmatched records are unchanged.
bool selectCharacterAnimationFrame(std::vector<CharacterAnimation> &, int actor,
                                   int selector, int frame);
void compositeCharacterAnimations(Image &destination,
                                  const std::vector<CharacterAnimation> &,
                                  int offsetX = 0, int offsetY = 0);
// Exact default-colour native compositing onto an opaque image. Coordinates
// are the absolute destination top-left; Image::x/y are not added implicitly.
void compositeImage(Image &destination, const Image &source, int x, int y);
// Crop a source rectangle before the same exact blend. Source rectangles must
// be inside the source image; destination clipping is automatic.
void compositeImageRegion(Image &destination, const Image &source, int x, int y,
                          int sourceX, int sourceY, int width, int height);
// Emotion-icon animation container (.MANP) used by Imouto de Ikou!
struct ManpuAnimation {
  std::map<int32_t, Image> frames;
  std::map<int32_t, std::vector<uint32_t>> sequences;
};
ManpuAnimation decodeManpu(const Bytes &);
// 8-bit clickable hitmap (.CMAP) used by Imouto de Ikou!
struct ClickableMap {
  int width = 0, height = 0;
  Bytes hitIds;
  uint8_t hitAt(int x, int y) const {
    if (x < 0 || y < 0 || x >= width || y >= height)
      return 0;
    const size_t idx = size_t(y) * size_t(width) + size_t(x);
    return idx < hitIds.size() ? hitIds[idx] : 0;
  }
};
ClickableMap decodeClickableMap(const Bytes &);
// Apply Imouto de Ikou! Ldbg lighting tint to a decoded character sprite:
// 0 = normal (1.0, 1.0, 1.0), 1 = evening (1.0, 0.9, 0.9),
// 2 = night (0.7, 0.7, 1.0).
void applyLightingTint(Image &image, int tintMode);
} // namespace SummerRadish
