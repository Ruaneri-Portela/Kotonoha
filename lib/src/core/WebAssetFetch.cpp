// Browser bootstrap. Game assets no longer need a MEMFS staging copy:
// the manifest configures an actual IMount backed by Emscripten Fetch.
// IDBFS /save is deliberately separate from this read-only asset system.
#include <Kotonoha/Filesystem/RemoteAssets.hpp>
#include <Kotonoha/Filesystem/Sources/FetchAssetProvider.hpp>
#include <Kotonoha/core/Log.hpp>

#include <algorithm>
#include <cstdlib>
#include <memory>
#include <stdexcept>
#include <string>

#ifdef __EMSCRIPTEN__
#include <emscripten/emscripten.h>

namespace Kotonoha {
extern "C" {
EMSCRIPTEN_KEEPALIVE void KotonohaMountSaveStorage() {
  EM_ASM({
    if (Module["KotonohaSaveMounted"])
      return;
    Module["KotonohaSaveMounted"] = true;
    Module["KotonohaSaveReady"] = false;
    const dependency = "kotonoha_idbfs_save";
    addRunDependency(dependency);
    const finish = function(error) {
      if (error)
        console.error("[Kotonoha] IDBFS save mount failed:", error);
      Module["KotonohaSaveReady"] = true;
      removeRunDependency(dependency);
    };
    try {
      FS.mkdirTree("/save");
      if (!FS.isMountpoint("/save"))
        FS.mount(IDBFS, {autoPersist : true}, "/save");
      FS.syncfs(true, finish);
    } catch (error) {
      finish(error);
    }
  });
}

EMSCRIPTEN_KEEPALIVE void KotonohaSyncSaveStorage() {
  EM_ASM({
    Module["KotonohaSaveSyncPromise"] = new Promise(function(resolve) {
      FS.syncfs(
          false, function(error) {
            if (error)
              console.error("[Kotonoha] Cannot persist saves:", error);
            resolve(error || null);
          });
    });
  });
}

// The HTML shell fetches the manifest asynchronously before releasing its
// preRun dependency; C++ owns subsequent Fetch requests and their byte cache.
EMSCRIPTEN_KEEPALIVE int KotonohaConfigureRemoteAssetManifest(
    const char *baseUrl, const char *manifestJson, int preloadBytes) {
  try {
    if (!baseUrl || !*baseUrl || !manifestJson || !*manifestJson ||
        preloadBytes < 0)
      throw std::invalid_argument("Missing HTTP(S) asset origin/manifest");
    // Honor the configured KOTONOHA_WASM_FETCH_CACHE_MIB limit instead of
    // hardcoding a 256 MiB budget that silently overrode the build option.
    auto provider =
        Filesystem::FetchAssetProvider::fromManifest(baseUrl, manifestJson);
    if (!provider->fileCount())
      throw std::runtime_error("Remote asset manifest is empty");
    // Set the source before starting requests so consumers can use
    // mountGameAssets() immediately after the bootstrap barrier completes.
    Filesystem::setRemoteGameAssets(provider);
    provider->begin(
        static_cast<std::size_t>(std::min(preloadBytes, 256 * 1024 * 1024)));
    return 1;
  } catch (const std::exception &error) {
    Filesystem::setRemoteGameAssets({});
    SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                 "[Kotonoha] Remote asset manifest rejected: %s", error.what());
    return 0;
  }
}

// Transfer a large JSON manifest into WASM heap storage instead of the
// ccall "string" converter, which allocates its UTF-8 copy on the WASM stack.
// Typical asset indexes are much larger than the application's C stack.
EMSCRIPTEN_KEEPALIVE void *KotonohaAllocateRemoteManifest(int bytes) {
  constexpr int kMaxManifestTransfer = 4 * 1024 * 1024 + 1;
  return bytes > 0 && bytes <= kMaxManifestTransfer
             ? std::malloc(static_cast<std::size_t>(bytes))
             : nullptr;
}

EMSCRIPTEN_KEEPALIVE void KotonohaFreeRemoteManifest(void *memory) {
  std::free(memory);
}

// JS Fetch cache copies at most 8 MiB per completion into the WASM heap.
// Range reads normally use 1 MiB. Larger bodies are rejected before transfer.
EMSCRIPTEN_KEEPALIVE void *KotonohaAllocateRemoteFetchBuffer(int bytes) {
  return bytes > 0 && bytes <= 8 * 1024 * 1024
             ? std::malloc(static_cast<std::size_t>(bytes))
             : nullptr;
}
EMSCRIPTEN_KEEPALIVE void KotonohaFreeRemoteFetchBuffer(void *memory) {
  std::free(memory);
}

// A non-empty asset manifest is the authoritative HTTP allow-list.
EMSCRIPTEN_KEEPALIVE int
KotonohaConfigureRemoteAssetIndexedManifest(const char *baseUrl,
                                            const char *manifestJson) {
  if (KotonohaConfigureRemoteAssetManifest(baseUrl, manifestJson, 0) != 1)
    return 0;
  return Filesystem::remoteGameAssets() ? 1 : 0;
}

EMSCRIPTEN_KEEPALIVE int KotonohaRequestRemoteAsset(const char *path) {
  auto provider = Filesystem::remoteGameAssets();
  if (!provider || !path || !*path)
    return -1;
  try {
    return provider->exists(path) ? 1 : 0;
  } catch (const std::exception &) {
    return -1;
  }
}

EMSCRIPTEN_KEEPALIVE int KotonohaAssetFetchTotal() {
  const auto source = Filesystem::remoteGameAssets();
  return source ? static_cast<int>(source->totalRequests()) : 0;
}
EMSCRIPTEN_KEEPALIVE int KotonohaAssetFetchLoaded() {
  const auto source = Filesystem::remoteGameAssets();
  return source ? static_cast<int>(source->completedRequests()) : 0;
}
EMSCRIPTEN_KEEPALIVE int KotonohaAssetFetchDeferred() {
  const auto source = Filesystem::remoteGameAssets();
  return source ? static_cast<int>(source->fileCount() > source->totalRequests()
                                       ? source->fileCount() -
                                             source->totalRequests()
                                       : 0)
                : 0;
}
EMSCRIPTEN_KEEPALIVE int KotonohaAssetFetchDone() {
  const auto source = Filesystem::remoteGameAssets();
  return source && source->complete() ? 1 : 0;
}
EMSCRIPTEN_KEEPALIVE int KotonohaAssetFetchAllReady() {
  const auto source = Filesystem::remoteGameAssets();
  return source && source->allFilesReady() ? 1 : 0;
}
EMSCRIPTEN_KEEPALIVE int KotonohaAssetFetchFailed() {
  const auto source = Filesystem::remoteGameAssets();
  return !source || source->failures() ? 1 : 0;
}
} // extern "C"
} // namespace Kotonoha
#endif
