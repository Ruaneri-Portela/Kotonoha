#include <Kotonoha/Prefetch/WasmBackend.hpp>

#include <Kotonoha/Filesystem/Sources/FetchAssetProvider.hpp>

#if defined(__EMSCRIPTEN__)
#include <Kotonoha/Filesystem/RemoteAssets.hpp>
#endif

#include <algorithm>

namespace Kotonoha::Prefetch {

WasmBackend::WasmBackend() : WasmBackend(Config()) {}

WasmBackend::WasmBackend(Config config) : config_(config) {
#if defined(__EMSCRIPTEN__)
  // Bootstrap registry filled by the browser's asynchronous manifest load.
  provider_ = Filesystem::remoteGameAssets();
#endif
}

WasmBackend::~WasmBackend() = default;

bool WasmBackend::enabled() const noexcept {
#if defined(__EMSCRIPTEN__)
  return true;
#else
  return false;
#endif
}

void WasmBackend::beginDispatch() noexcept { issued_ = 0; }

void WasmBackend::endDispatch() noexcept {}

void WasmBackend::prefetch(const NodeId &id) {
#if defined(__EMSCRIPTEN__)
  if (!provider_ || issued_ >= config_.maxRequestsPerDispatch)
    return;
  // The provider's pump() caps in-flight HTTP requests per origin at four;
  // respect that limit here so a burst cannot grow the queue unbounded.
  if (provider_->pending() >= kMaxConcurrentPerOrigin)
    return;

  std::uint64_t length = std::min(config_.rangeBytes, kMaxRangeBytes);
  try {
    // Only manifest-indexed regular files are ever requested from HTTP.
    const auto info = provider_->stat(id);
    if (info.size <= 0)
      return;
    length = std::min(length, static_cast<std::uint64_t>(info.size));
    provider_->prefetchRange(id, 0, length);
    ++issued_;
  } catch (...) {
    // A heuristic produced a miss (unknown/unavailable node); ignore it.
  }
#else
  (void)id;
#endif
}

} // namespace Kotonoha::Prefetch
