// JavaScript-facing browser ABI belongs in the Emscripten MAIN_MODULE.
// libKotonoha.wasm owns the C++ Fetch provider; the per-game main module
// exports the stable callback name used by the Fetch/IndexedDB bridge.
#include <Kotonoha/Filesystem/Sources/FetchAssetProvider.hpp>

#ifdef __EMSCRIPTEN__
#include <emscripten/emscripten.h>

#include <cstdint>
#include <string>

extern "C" EMSCRIPTEN_KEEPALIVE void
KotonohaCompleteBrowserFetch(std::uintptr_t context, int status,
                             const char *bytes, int count,
                             const char *headers) {
  Kotonoha::Filesystem::FetchAssetProvider::completeBrowserJob(
      reinterpret_cast<void *>(context),
      status >= 0 ? static_cast<unsigned>(status) : 0u, bytes,
      count >= 0 ? static_cast<std::uint64_t>(count) : 0u,
      headers ? std::string(headers) : std::string{});
}
#endif
