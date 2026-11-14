#include <Kotonoha/Filesystem/Adapters/BrowserBlobAdapter.hpp>
#include <Kotonoha/Filesystem/Filesystem.hpp>

#include <climits>
#include <stdexcept>
#include <utility>
#include <vector>

#ifdef __EMSCRIPTEN__
#include <emscripten/emscripten.h>
#endif

namespace Kotonoha::Filesystem {

BrowserBlobAdapter::BrowserBlobAdapter(std::unique_ptr<FileHandle> source,
                                       std::string mime,
                                       std::size_t maxBytes) {
  if (!source)
    throw FilesystemError(ErrorCode::InvalidPath,
                          "Null file handle for browser video");
#ifdef __EMSCRIPTEN__
  // Do not silently buffer videos of unlimited size in WASM and JavaScript.
  if (maxBytes > static_cast<std::size_t>(INT_MAX))
    maxBytes = static_cast<std::size_t>(INT_MAX);
  auto bytes = readAll(*source, maxBytes);
  resourceId_ = EM_ASM_INT({
    try {
      const registry = globalThis.__kotonohaBlobRegistry ||
          (globalThis.__kotonohaBlobRegistry = {next: 1, urls: new Map()});
      const copy = HEAPU8.slice($0, $0 + $1);
      const blob = new Blob([copy], {type: UTF8ToString($2)});
      const url = URL.createObjectURL(blob);
      const id = registry.next++;
      registry.urls.set(id, url);
      return id;
    } catch (e) {
      return 0;
    }
  }, bytes.data(), static_cast<int>(bytes.size()), mime.c_str());

  if (resourceId_ <= 0)
    throw FilesystemError(ErrorCode::IOError,
                          "Browser could not create a media Blob");
  const auto count = EM_ASM_INT({
    const r = globalThis.__kotonohaBlobRegistry;
    const url = r && r.urls.get($0);
    return url ? lengthBytesUTF8(url) + 1 : 0;
  }, resourceId_);
  if (count < 2 || count > 4096) {
    reset();
    throw FilesystemError(ErrorCode::IOError,
                          "Browser produced an invalid Blob URL");
  }
  url_.resize(static_cast<std::size_t>(count));
  EM_ASM({
    const r = globalThis.__kotonohaBlobRegistry;
    stringToUTF8(r.urls.get($0), $1, $2);
  }, resourceId_, url_.data(), count);
  url_.resize(static_cast<std::size_t>(count - 1));
#else
  (void)mime;
  (void)maxBytes;
  throw FilesystemError(ErrorCode::UnsupportedMount,
                        "Browser Blob URLs are available only on Emscripten");
#endif
}

BrowserBlobAdapter::~BrowserBlobAdapter() { reset(); }

BrowserBlobAdapter::BrowserBlobAdapter(BrowserBlobAdapter &&other) noexcept
    : resourceId_(std::exchange(other.resourceId_, 0)),
      url_(std::move(other.url_)) {}

BrowserBlobAdapter &
BrowserBlobAdapter::operator=(BrowserBlobAdapter &&other) noexcept {
  if (this == &other)
    return *this;
  reset();
  resourceId_ = std::exchange(other.resourceId_, 0);
  url_ = std::move(other.url_);
  return *this;
}

void BrowserBlobAdapter::reset() noexcept {
#ifdef __EMSCRIPTEN__
  if (resourceId_ > 0) {
    EM_ASM({
      const registry = globalThis.__kotonohaBlobRegistry;
      if (registry) {
        const url = registry.urls.get($0);
        if (url) URL.revokeObjectURL(url);
        registry.urls.delete($0);
      }
    }, resourceId_);
  }
#endif
  resourceId_ = 0;
  url_.clear();
}

} // namespace Kotonoha::Filesystem
