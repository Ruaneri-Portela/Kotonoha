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
  std::size_t budget_ = 64u << 20;
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
  // Manifest entries: {"entries":[{"path":"DATA/a.pak","size":123}, ...]}.
  // generator also retains the old "files" array for legacy WebAssetFetch.
  static std::shared_ptr<FetchAssetProvider>
  fromManifest(std::string baseUrl, const std::string &json,
               std::size_t cacheBudget = 64u << 20);

  std::string name() const override { return "Emscripten Fetch"; }
  void begin(std::size_t preloadLimit = 4u << 20);
  // Explicit asynchronous Range prefetch; up to 4 MiB per request.
  void prefetchRange(std::string_view id, std::uint64_t offset,
                     std::uint64_t length);
  bool ready(std::string_view id) const;
  bool started() const noexcept { return started_; }
  std::size_t pending() const noexcept { return queued_.size() + active_; }
  std::size_t cachedBytes() const noexcept { return cached_; }

  bool exists(std::string_view path) const override;
  FileInfo stat(std::string_view path) const override;
  std::vector<FileInfo> list(std::string_view directory) const override;
  std::unique_ptr<FileHandle> open(std::string_view path) const override;
  std::unique_ptr<Kotonoha::AssetStream>
  openStream(const std::string &id) const override;
};

} // namespace Kotonoha::Filesystem
