#pragma once

#include <Kotonoha/Prefetch/Backend.hpp>
#include <Kotonoha/Prefetch/Types.hpp>

#include <cstddef>
#include <cstdint>
#include <memory>

namespace Kotonoha::Filesystem { class FetchAssetProvider; }

namespace Kotonoha::Prefetch {

// Web-only transport. Delegates small, bounded HTTP Range requests to the
// engine's existing FetchAssetProvider (JavaScript Fetch + IndexedDB, falling
// back to emscripten_fetch). It never stages archives in MEMFS: only a short
// byte window (default 4 KiB) of a predicted node is requested, and the
// provider enforces HTTP 206 Content-Range plus a 4-request per-origin cap.
class WasmBackend final : public Backend {
public:
  struct Config {
    std::uint64_t rangeBytes = kDefaultRangeBytes;
    std::size_t maxRequestsPerDispatch = 8;
  };

  WasmBackend();
  explicit WasmBackend(Config config);
  ~WasmBackend() override;

  // True only under __EMSCRIPTEN__; native builds see a disabled backend.
  bool enabled() const noexcept override;
  void beginDispatch() noexcept override;
  void prefetch(const NodeId &id) override;
  void endDispatch() noexcept override;

  std::size_t issued() const noexcept { return issued_; }

private:
  std::shared_ptr<Filesystem::FetchAssetProvider> provider_;
  Config config_;
  std::size_t issued_ = 0;
};

} // namespace Kotonoha::Prefetch
