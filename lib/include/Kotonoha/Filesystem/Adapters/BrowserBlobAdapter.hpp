#pragma once

#include <Kotonoha/Filesystem/FileHandle.hpp>

#include <cstddef>
#include <memory>
#include <string>
#include <vector>
#include <cstdint>

namespace Kotonoha::Filesystem {

// The browser gets only a "blob:" URL created from FileHandle bytes.
// The underlying source may be a local directory, ISO, TAR, or prefetched
// Fetch mount. No remote source URL is passed to HTMLVideoElement.
//
// This adapter buffers at most maxBytes and is intentionally unsupported
// outside Emscripten. The Blob URL is revoked on destruction (RAII).
class BrowserBlobAdapter final {
  int resourceId_ = 0;
  std::string url_;
  std::vector<std::uint8_t> bytes_;

public:
  explicit BrowserBlobAdapter(
      std::unique_ptr<FileHandle> source, std::string mime,
      std::size_t maxBytes = 64u * 1024u * 1024u);
  ~BrowserBlobAdapter();

  BrowserBlobAdapter(const BrowserBlobAdapter &) = delete;
  BrowserBlobAdapter &operator=(const BrowserBlobAdapter &) = delete;
  BrowserBlobAdapter(BrowserBlobAdapter &&other) noexcept;
  BrowserBlobAdapter &operator=(BrowserBlobAdapter &&other) noexcept;

  const std::string &url() const noexcept { return url_; }
  // Retained solely for the FFmpeg fallback if browser decoding fails.
  const std::vector<std::uint8_t> &bytes() const noexcept { return bytes_; }
  bool valid() const noexcept { return resourceId_ > 0; }
  void reset() noexcept;
};

} // namespace Kotonoha::Filesystem
