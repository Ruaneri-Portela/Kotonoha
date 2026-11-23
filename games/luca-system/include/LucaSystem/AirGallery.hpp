#pragma once

#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <vector>

namespace LucaSystem {

struct AirGallerySlot {
  std::vector<std::uint16_t> variants;
};

using AirGalleryCatalog = std::vector<std::vector<AirGallerySlot>>;

// The AIR Steam table starts with four category lists. Positive entries are
// image IDs; negative entries refer to the following one-based group table.
// Later sections in _cggallery describe other resource classes.
inline AirGalleryCatalog
ParseAirGallery(const std::vector<std::uint8_t> &bytes) {
  size_t cursor = 6;
  const auto word = [&]() -> unsigned {
    if (cursor + 2 > bytes.size())
      throw std::runtime_error("AIR gallery: truncated catalog");
    const unsigned value = bytes[cursor] | (unsigned(bytes[cursor + 1]) << 8);
    cursor += 2;
    return value;
  };
  const auto categories = word();
  const auto lists = word();
  if (categories != 4 || lists != categories)
    throw std::runtime_error("AIR gallery: unsupported category header");
  std::vector<std::vector<unsigned>> references(categories);
  for (auto &category : references) {
    const auto count = word();
    if (count == 0 || count > (bytes.size() - cursor) / 4)
      throw std::runtime_error("AIR gallery: invalid slot count");
    for (unsigned i = 0; i < count; ++i)
      category.push_back(word());
    cursor += count * 2; // secondary per-slot table, unused by the viewer
  }
  const auto groupCount = word();
  if (groupCount > (bytes.size() - cursor) / 4)
    throw std::runtime_error("AIR gallery: invalid variant group count");
  std::vector<AirGallerySlot> groups(groupCount);
  for (auto &group : groups) {
    const auto count = word();
    if (count == 0 || count > (bytes.size() - cursor) / 2)
      throw std::runtime_error("AIR gallery: invalid variant count");
    for (unsigned i = 0; i < count; ++i) {
      const auto id = word();
      if (id == 0 || id >= 0x8000)
        throw std::runtime_error("AIR gallery: invalid variant ID");
      group.variants.push_back(static_cast<std::uint16_t>(id));
    }
  }
  AirGalleryCatalog catalog(categories);
  for (size_t i = 0; i < catalog.size(); ++i) {
    for (const auto id : references[i]) {
      if (id == 0)
        throw std::runtime_error("AIR gallery: missing slot ID");
      if (id < 0x8000)
        catalog[i].push_back({{static_cast<std::uint16_t>(id)}});
      else {
        const auto group = 0x10000u - id;
        if (group > groups.size())
          throw std::runtime_error("AIR gallery: unknown variant group");
        catalog[i].push_back(groups[group - 1]);
      }
    }
  }
  return catalog;
}

} // namespace LucaSystem
