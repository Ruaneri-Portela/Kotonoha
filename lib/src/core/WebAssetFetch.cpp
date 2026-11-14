// Browser bootstrap. Game assets no longer need a MEMFS staging copy:
// the manifest configures an actual IMount backed by Emscripten Fetch.
// IDBFS /save is deliberately separate from this read-only asset system.
#include <Kotonoha/Filesystem/RemoteAssets.hpp>
#include <Kotonoha/Filesystem/Sources/FetchAssetProvider.hpp>
#include <Kotonoha/core/Log.hpp>

#include <algorithm>
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
    auto provider = Filesystem::FetchAssetProvider::fromManifest(
        baseUrl, manifestJson, 256u * 1024u * 1024u);
    if (!provider->fileCount())
      throw std::runtime_error("Remote asset manifest is empty");
    // Set the source before starting requests so consumers can use
    // mountGameAssets() immediately after the bootstrap barrier completes.
    Filesystem::setRemoteGameAssets(provider);
    provider->begin(static_cast<std::size_t>(
        std::min(preloadBytes, 256 * 1024 * 1024)));
    return 1;
  } catch (const std::exception &error) {
    Filesystem::setRemoteGameAssets({});
    SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                 "[Kotonoha] Remote asset manifest rejected: %s", error.what());
    return 0;
  }
}

// Optional direct HTTP(S) source: no manifest or directory listing required.
// The provider only knows a file after its asynchronous GET has completed.
EMSCRIPTEN_KEEPALIVE int KotonohaConfigureRemoteAssetUrl(const char *baseUrl) {
  try {
    if (!baseUrl || !*baseUrl)
      return 0;
    auto provider = Filesystem::FetchAssetProvider::fromUrl(
        baseUrl, 256u * 1024u * 1024u);
    Filesystem::setRemoteGameAssets(provider);
    provider->begin(0);
    return 1;
  } catch (const std::exception &error) {
    SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                 "[Kotonoha] Fetch URL rejected: %s", error.what());
    return 0;
  }
}

EMSCRIPTEN_KEEPALIVE int KotonohaRequestRemoteAsset(const char *path) {
  auto provider = Filesystem::remoteGameAssets();
  if (!provider || !path || !*path) return -1;
  try {
    if (provider->manifestFree())
      provider->requestAsset(path);
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
  return source ? static_cast<int>(
      source->fileCount() > source->totalRequests()
        ? source->fileCount() - source->totalRequests() : 0) : 0;
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
