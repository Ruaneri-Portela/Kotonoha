#include <ZeroNoTsukaima/Archive.hpp>

namespace ZeroNoTsukaima {

std::vector<ArchiveEntry> ParseArchiveIndex(const std::vector<uint8_t> &hd) {
  std::vector<ArchiveEntry> entries;
  uint64_t offset = 0;
  for (size_t i = 0; i + 4 <= hd.size(); i += 4) {
    const uint32_t size = hd[i] | (hd[i + 1] << 8) | (hd[i + 2] << 16) | (uint32_t(hd[i + 3]) << 24);
    entries.push_back({offset, size});
    offset += (uint64_t(size) + 2047) / 2048 * 2048;
  }
  return entries;
}

} // namespace ZeroNoTsukaima
