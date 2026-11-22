#pragma once

#include <Kotonoha/Filesystem/IMount.hpp>
#include <Kotonoha/core/AssetProvider.hpp>

#include <cstddef>
#include <cstdint>
#include <deque>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <string_view>

#ifndef KOTONOHA_WASM_FETCH_CACHE_MIB
#define KOTONOHA_WASM_FETCH_CACHE_MIB 64
#endif


namespace Kotonoha::Filesystem {

// Remote directory with an explicit size-aware JSON manifest.
// Emscripten Fetch runs asynchronously; open()/read() are synchronous over
// already cached byte ranges and throw WouldBlock if a range is not yet ready.
// No browser main-thread waits, MEMFS staging or asset URL leakage.
class FetchAssetProvider final
    : public Kotonoha::AssetProvider,
      public IMount,
      public std::enable_shared_from_this<FetchAssetProvider> {
  struct Slot;
  struct Job {
    std::string key;
    std::uint64_t start = 0;
    std::uint64_t length = 0;
    bool full = false;
  };
  std::string base_;
  std::map<std::string, FileInfo> nodes_;
  std::map<std::string, std::shared_ptr<Slot>> files_;
  std::deque<Job> queued_;
  std::set<std::string> requested_;
  std::size_t active_ = 0, cached_ = 0;
  std::uint64_t accessClock_ = 0; // LRU access age for cached byte ranges
  std::size_t completed_ = 0, failures_ = 0, requestedCount_ = 0;
  std::size_t budget_ = static_cast<std::size_t>(KOTONOHA_WASM_FETCH_CACHE_MIB) << 20;
  bool started_ = false;

  FetchAssetProvider(std::string base, std::size_t budget);
  void enqueue(Job job);
  void pump();
  void complete(const Job &job, unsigned status, const char *body,
                std::uint64_t bytes, const std::string &responseHeaders);
  std::shared_ptr<Slot> slot(std::string_view id) const;

protected:
  bool containsAsset(const std::string &id) const override;
  std::vector<Uint8> readAsset(const std::string &id) const override;
  std::vector<std::string>
  listAsset(const std::string &prefix = {}) const override;

public:
  // Only indexed HTTP files are accessible. Unlisted paths never probe HTTP.
  bool hasManifestIndex() const noexcept { return true; }

  static std::shared_ptr<FetchAssetProvider>
  fromManifest(std::string baseUrl, const std::string &json,
               std::size_t cacheBudget = static_cast<std::size_t>(KOTONOHA_WASM_FETCH_CACHE_MIB) << 20);

  std::string name() const override { return "Emscripten Fetch"; }
  void begin(std::size_t preloadLimit = 4u << 20);
  // Explicit asynchronous Range prefetch; up to 1 MiB per request.
  void prefetchRange(std::string_view id, std::uint64_t offset,
                     std::uint64_t length);
  bool ready(std::string_view id) const;
  bool started() const noexcept { return started_; }
  std::size_t pending() const noexcept { return queued_.size() + active_; }
  // Completion callback for JavaScript-backed HTTP/IndexedDB requests.
  // Runs on the browser's ordinary callback turn; never suspends Asyncify.
#ifdef __EMSCRIPTEN__
  static void completeBrowserJob(void *opaque, unsigned status,
                                 const char *body, std::uint64_t count,
                                 const std::string &headers) noexcept;
#endif
  std::size_t cachedBytes() const noexcept { return cached_; }
  std::size_t fileCount() const noexcept { return files_.size(); }
  std::size_t totalRequests() const noexcept { return requestedCount_; }
  std::size_t completedRequests() const noexcept { return completed_; }
  std::size_t failures() const noexcept { return failures_; }
  bool complete() const noexcept { return started_ && pending() == 0; }
  bool allFilesReady() const;
  const std::string &baseUrl() const noexcept { return base_; }

  // Neither of these metadata queries starts a request or suspends Asyncify.
  bool isKnownRegularFile(std::string_view path) const override;
  bool isKnownDirectory(std::string_view path) const override;
  bool hasIndexedDirectory(std::string_view path) const;
  bool exists(std::string_view path) const override;
  FileInfo stat(std::string_view path) const override;
  std::vector<FileInfo> list(std::string_view directory) const override;
  std::unique_ptr<FileHandle> open(std::string_view path) const override;
  std::unique_ptr<Kotonoha::AssetStream>
  openStream(const std::string &id) const override;
};

} // namespace Kotonoha::Filesystem
