#include <Kotonoha/core/Log.hpp>
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

#ifdef __EMSCRIPTEN__
#include <emscripten/emscripten.h>
#include <emscripten/fetch.h>
#include <json-c/json.h>

namespace Kotonoha {
namespace {
constexpr const char *assetRoot = "/game-assets";
constexpr const char *assetUrlBase = "game-assets/";
constexpr std::size_t maximumParallelFetches = 4;

struct AssetFetchState {
  bool started = false, done = false, failed = false;
  std::size_t total = 0, loaded = 0, deferred = 0, next = 0, active = 0;
  std::vector<std::string> files;
};
AssetFetchState fetchState;

bool shouldDefer(const std::string &path) {
  return EM_ASM_INT(
             {
               var callback = globalThis["KotonohaShouldDeferAsset"];

               if (typeof callback != "function")
                 return 0;

               return callback(UTF8ToString($0)) ? 1 : 0;
             },
             path.c_str()) != 0;
}

std::string validateRelativePath(const std::string &path) {
  const std::filesystem::path relative(path);
  if (path.empty() || relative.is_absolute() || relative.has_root_name() ||
      path.find('\0') != std::string::npos)
    throw std::runtime_error("Invalid web asset path: " + path);
  const auto normalized = relative.lexically_normal();
  if (normalized.empty() || normalized == ".")
    throw std::runtime_error("Invalid web asset path: " + path);
  for (const auto &part : normalized)
    if (part == "..")
      throw std::runtime_error("Web asset escapes its root: " + path);
  return normalized.generic_string();
}

void writeAsset(const std::string &path, const char *data, std::size_t size) {
  const auto target =
      std::filesystem::path(assetRoot) / validateRelativePath(path);
  std::filesystem::create_directories(target.parent_path());
  std::ofstream output(target, std::ios::binary | std::ios::trunc);
  if (!output)
    throw std::runtime_error("Cannot create web asset: " + target.string());
  if (size)
    output.write(data, static_cast<std::streamsize>(size));
  output.close();
  if (!output)
    throw std::runtime_error("Cannot write web asset: " + target.string());
}

void pump();
void completeAsset(emscripten_fetch_t *fetch) {
  std::unique_ptr<std::string> path(
      static_cast<std::string *>(fetch->userData));
  try {
    if (fetch->status != 200 || !path)
      throw std::runtime_error("HTTP " + std::to_string(fetch->status) +
                               " for " + (path ? *path : "unknown asset"));
    writeAsset(*path, fetch->data, fetch->numBytes);
  } catch (const std::exception &error) {
    fetchState.failed = true;
    SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "[Kotonoha] %s", error.what());
  }
  if (fetchState.active)
    --fetchState.active;
  if (fetchState.loaded < fetchState.total)
    ++fetchState.loaded;
  emscripten_fetch_close(fetch);
  pump();
}

void startAsset(const std::string &path) {
  auto *context = new std::string(path);
  emscripten_fetch_attr_t attr;
  emscripten_fetch_attr_init(&attr);
  std::copy_n("GET", 4, attr.requestMethod);
  attr.attributes = EMSCRIPTEN_FETCH_LOAD_TO_MEMORY;
  attr.onsuccess = completeAsset;
  attr.onerror = completeAsset;
  attr.userData = context;
  ++fetchState.active;
  emscripten_fetch(&attr, (std::string(assetUrlBase) + path).c_str());
}

void pump() {
  while (fetchState.active < maximumParallelFetches &&
         fetchState.next < fetchState.files.size()) {
    const std::string path = fetchState.files[fetchState.next++];
    if (shouldDefer(path)) {
      ++fetchState.deferred;
      ++fetchState.loaded;
      try {
        // Keep a MEMFS entry so AssetProvider::localPath can resolve the ID.
        writeAsset(path, nullptr, 0);
      } catch (const std::exception &error) {
        fetchState.failed = true;
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "[Kotonoha] %s",
                     error.what());
      }
    } else {
      startAsset(path);
    }
  }
  if (fetchState.loaded >= fetchState.total && fetchState.active == 0)
    fetchState.done = true;
}

void completeManifest(emscripten_fetch_t *fetch) {
  try {
    if (fetch->status != 200)
      throw std::runtime_error("Asset manifest HTTP " +
                               std::to_string(fetch->status));
    const std::string data(fetch->data, fetch->numBytes);
    json_tokener *tok = json_tokener_new();
    if (!tok)
      throw std::bad_alloc();
    json_object *parsed =
        json_tokener_parse_ex(tok, data.data(), static_cast<int>(data.size()));
    const auto error = json_tokener_get_error(tok);
    json_tokener_free(tok);
    std::unique_ptr<json_object, decltype(&json_object_put)> root(
        parsed, json_object_put);
    if (error != json_tokener_success || !root ||
        !json_object_is_type(root.get(), json_type_object))
      throw std::runtime_error("Invalid asset manifest");
    json_object *files = nullptr;
    if (!json_object_object_get_ex(root.get(), "files", &files) ||
        !json_object_is_type(files, json_type_array))
      throw std::runtime_error("Manifest has no files array");
    const auto count = json_object_array_length(files);
    fetchState.files.reserve(count);
    for (std::size_t i = 0; i < count; ++i) {
      json_object *entry = json_object_array_get_idx(files, i);
      if (!json_object_is_type(entry, json_type_string))
        continue;
      std::string path(json_object_get_string(entry),
                       json_object_get_string_len(entry));
      if (path == "manifest.json")
        continue;
      fetchState.files.push_back(validateRelativePath(path));
    }
    fetchState.total = fetchState.files.size();
    SDL_Log("[Kotonoha] Manifest contains %zu assets", fetchState.total);
    pump();
  } catch (const std::exception &error) {
    fetchState.failed = true;
    SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "[Kotonoha] %s", error.what());
  }
  emscripten_fetch_close(fetch);
  if (fetchState.failed || fetchState.total == 0)
    fetchState.done = true;
}
} // namespace

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

EMSCRIPTEN_KEEPALIVE void KotonohaStartAssetFetch(const char *manifestUrl) {
  if (fetchState.started)
    return;
  fetchState.started = true;
  if (!manifestUrl || !*manifestUrl) {
    fetchState.failed = fetchState.done = true;
    return;
  }
  emscripten_fetch_attr_t attr;
  emscripten_fetch_attr_init(&attr);
  std::copy_n("GET", 4, attr.requestMethod);
  attr.attributes = EMSCRIPTEN_FETCH_LOAD_TO_MEMORY;
  attr.onsuccess = completeManifest;
  attr.onerror = completeManifest;
  emscripten_fetch(&attr, manifestUrl);
}
EMSCRIPTEN_KEEPALIVE int KotonohaAssetFetchTotal() {
  return static_cast<int>(fetchState.total);
}
EMSCRIPTEN_KEEPALIVE int KotonohaAssetFetchLoaded() {
  return static_cast<int>(fetchState.loaded);
}
EMSCRIPTEN_KEEPALIVE int KotonohaAssetFetchDeferred() {
  return static_cast<int>(fetchState.deferred);
}
EMSCRIPTEN_KEEPALIVE int KotonohaAssetFetchDone() {
  return fetchState.done ? 1 : 0;
}
EMSCRIPTEN_KEEPALIVE int KotonohaAssetFetchFailed() {
  return fetchState.failed ? 1 : 0;
}
} // extern "C"
} // namespace Kotonoha
#endif
