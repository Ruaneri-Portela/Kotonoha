#pragma once

#include <Kotonoha/workers/AttachedFont.hpp>

#include <SDL3/SDL_stdinc.h>

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace Kotonoha {
namespace workers {

// Shared with the media worker so a font attached to a video can be handed
// straight to libass without a conversion or a compatibility wrapper.
using SubtitleAttachedFont = AttachedFont;

// Deduplicating, bounded registry of attached fonts. The caller is responsible
// for registering accepted entries with the libass library; this class owns the
// bytes and the budget bookkeeping so a hostile container cannot exhaust memory
// or flood libass with duplicate fonts.
class SubtitleFontRegistry {
public:
  static constexpr size_t maxFontBytes = 8ull * 1024 * 1024;
  static constexpr size_t maxTotalBytes = 32ull * 1024 * 1024;
  static constexpr size_t maxFonts = 64;

  // False when the entry is an exact duplicate, oversized, unnamed, empty, or
  // exceeds a global budget.
  bool add(const SubtitleAttachedFont &font);
  size_t replace(const std::vector<SubtitleAttachedFont> &fonts);

  const std::vector<SubtitleAttachedFont> &fonts() const { return fonts_; }
  size_t totalBytes() const { return totalBytes_; }
  void clear();

  // FNV-1a over name and bytes; exposed so tests can assert dedup behavior.
  static uint64_t digest(const SubtitleAttachedFont &font);

private:
  std::vector<SubtitleAttachedFont> fonts_;
  std::vector<uint64_t> digests_;
  size_t totalBytes_ = 0;
};

} // namespace workers
} // namespace Kotonoha
