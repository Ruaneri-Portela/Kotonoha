#include <Kotonoha/workers/SubtitleFontRegistry.hpp>

#include <algorithm>

namespace Kotonoha {
namespace workers {

uint64_t SubtitleFontRegistry::digest(const SubtitleAttachedFont &font) {
  uint64_t hash = 1469598103934665603ull;
  const auto mix = [&hash](const void *data, size_t size) {
    const auto *bytes = static_cast<const Uint8 *>(data);
    for (size_t i = 0; i < size; ++i) {
      hash ^= bytes[i];
      hash *= 1099511628211ull;
    }
  };
  mix(font.name.data(), font.name.size());
  mix(font.bytes.data(), font.bytes.size());
  return hash;
}

bool SubtitleFontRegistry::add(const SubtitleAttachedFont &font) {
  if (font.name.empty() || font.bytes.empty() ||
      font.bytes.size() > maxFontBytes || fonts_.size() >= maxFonts ||
      totalBytes_ > maxTotalBytes - font.bytes.size())
    return false;
  const uint64_t value = digest(font);
  if (std::find(digests_.begin(), digests_.end(), value) != digests_.end())
    return false;
  fonts_.push_back(font);
  digests_.push_back(value);
  totalBytes_ += font.bytes.size();
  return true;
}

size_t
SubtitleFontRegistry::replace(const std::vector<SubtitleAttachedFont> &fonts) {
  clear();
  size_t accepted = 0;
  for (const auto &font : fonts)
    if (add(font))
      ++accepted;
  return accepted;
}

void SubtitleFontRegistry::clear() {
  fonts_.clear();
  digests_.clear();
  totalBytes_ = 0;
}

} // namespace workers
} // namespace Kotonoha
