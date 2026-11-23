#include <Kotonoha/Filesystem/Filesystem.hpp>
#include <Kotonoha/Filesystem/Path.hpp>
#include <Kotonoha/Filesystem/Sources/FetchAssetProvider.hpp>

#include <SDL3/SDL.h>
#include <json-c/json.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <climits>
#include <cstring>
#include <functional>
#include <stdexcept>
#include <utility>
#include <vector>

#ifdef __EMSCRIPTEN__
#include <emscripten/emscripten.h>
#include <emscripten/fetch.h>
#endif

namespace Kotonoha::Filesystem {

struct FetchAssetProvider::Slot {
  FileInfo info;
  // Physical spelling on the case-sensitive HTTP server. The VFS key stays
  // normalized/case-insensitive, while the URL may need a lowercase retry.
  std::string remotePath;
  // Immutable byte slices indexed by their absolute origin in the file.
  std::map<std::uint64_t, std::shared_ptr<const std::vector<Uint8>>> chunks;
  std::map<std::uint64_t, std::uint64_t> lastUse;
  bool failed = false;
  ErrorCode failure = ErrorCode::NetworkError;
  std::string message;
};

namespace {
std::string parent(const std::string &path) {
  auto slash = path.rfind('/');
  return slash == std::string::npos ? "" : path.substr(0, slash);
}

std::string encodePath(const std::string &path) {
  constexpr char hex[] = "0123456789ABCDEF";
  std::string encoded;
  for (unsigned char c : path) {
    if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
        (c >= '0' && c <= '9') || c == '/' || c == '.' || c == '_' ||
        c == '-' || c == '~')
      encoded.push_back(static_cast<char>(c));
    else {
      encoded.push_back('%');
      encoded.push_back(hex[c >> 4]);
      encoded.push_back(hex[c & 15]);
    }
  }
  return encoded;
}

// Reusable callback-independent byte slice cursor.
class CachedHandle final : public FileHandle {
  struct Lookup {
    std::function<std::int64_t(void *, std::size_t, std::uint64_t)> read;
    std::int64_t length;
  };
  Lookup lookup_;
  std::int64_t offset_ = 0;

public:
  explicit CachedHandle(
      std::int64_t length,
      std::function<std::int64_t(void *, std::size_t, std::uint64_t)> read)
      : lookup_{std::move(read), length} {}
  std::int64_t read(void *dst, std::size_t amount) override {
    if (!amount || offset_ == lookup_.length)
      return 0;
    if (!dst)
      return -1;
    const auto count =
        lookup_.read(dst,
                     static_cast<std::size_t>(std::min<std::uint64_t>(
                         amount, lookup_.length - offset_)),
                     static_cast<std::uint64_t>(offset_));
    if (count > 0)
      offset_ += count;
    return count;
  }
  std::int64_t seek(std::int64_t delta, SeekOrigin origin) override {
    const auto anchor = origin == SeekOrigin::Begin     ? 0
                        : origin == SeekOrigin::Current ? offset_
                                                        : lookup_.length;
    if (delta >= 0 ? delta > lookup_.length - anchor : delta < -anchor)
      return -1;
    offset_ = anchor + delta;
    return offset_;
  }
  std::int64_t tell() const override { return offset_; }
  std::int64_t size() const override { return lookup_.length; }
};

class FetchStream final : public Kotonoha::AssetStream {
  std::unique_ptr<FileHandle> file_;

public:
  explicit FetchStream(std::unique_ptr<FileHandle> file)
      : file_(std::move(file)) {}
  std::uint64_t size() const override {
    return static_cast<std::uint64_t>(file_->size());
  }
  std::uint64_t tell() const override {
    return static_cast<std::uint64_t>(file_->tell());
  }
  std::size_t read(Uint8 *data, std::size_t count) override {
    const auto n = file_->read(data, count);
    if (n < 0)
      throw FilesystemError(ErrorCode::IOError, "Fetch stream read failed");
    return static_cast<std::size_t>(n);
  }
  bool seek(std::uint64_t offset) override {
    return offset <= INT64_MAX &&
           file_->seek(static_cast<std::int64_t>(offset), SeekOrigin::Begin) ==
               static_cast<std::int64_t>(offset);
  }
};
} // namespace

FetchAssetProvider::FetchAssetProvider(std::string base, std::size_t budget)
    : base_(std::move(base)), budget_(budget) {
  if ((base_.compare(0, 7, "http://") != 0 &&
       base_.compare(0, 8, "https://") != 0) ||
      base_.find('#') != std::string::npos ||
      base_.find('?') != std::string::npos ||
      base_.find('@', base_.find("://") + 3) != std::string::npos || !budget)
    throw FilesystemError(ErrorCode::InvalidPath,
                          "Fetch source must be an HTTP(S) base URL");
  if (base_.back() != '/')
    base_ += '/';
  nodes_.emplace("", FileInfo{"", FileType::Directory, 0});
}

std::shared_ptr<FetchAssetProvider> FetchAssetProvider::fromManifest(
    std::string baseUrl, const std::string &manifest, std::size_t cacheBudget) {
  if (manifest.size() > 4 * 1024 * 1024)
    throw FilesystemError(ErrorCode::InvalidPath, "Oversized Fetch manifest");
  json_tokener *tok = json_tokener_new();
  if (!tok)
    throw std::bad_alloc();
  json_object *parsed = json_tokener_parse_ex(
      tok, manifest.c_str(), static_cast<int>(manifest.size()));
  const auto error = json_tokener_get_error(tok);
  json_tokener_free(tok);
  std::unique_ptr<json_object, decltype(&json_object_put)> root(
      parsed, json_object_put);
  if (error != json_tokener_success || !root ||
      !json_object_is_type(root.get(), json_type_object))
    throw FilesystemError(ErrorCode::InvalidPath,
                          "Invalid Fetch JSON manifest");

  json_object *entries = nullptr;
  if (!json_object_object_get_ex(root.get(), "entries", &entries) ||
      !json_object_is_type(entries, json_type_array))
    throw FilesystemError(ErrorCode::UnsupportedMount,
                          "Fetch manifest requires size-aware 'entries' array");

  auto provider = std::shared_ptr<FetchAssetProvider>(
      new FetchAssetProvider(std::move(baseUrl), cacheBudget));
  const auto count = json_object_array_length(entries);
  if (count == 0 || count > 100000)
    throw FilesystemError(ErrorCode::InvalidPath,
                          "HTTP manifest must contain 1..100000 entries");

  for (std::size_t i = 0; i < count; ++i) {
    json_object *entry = json_object_array_get_idx(entries, i);
    json_object *id = nullptr, *length = nullptr;
    if (!entry || !json_object_is_type(entry, json_type_object) ||
        !json_object_object_get_ex(entry, "path", &id) ||
        !json_object_is_type(id, json_type_string) ||
        !json_object_object_get_ex(entry, "size", &length) ||
        !json_object_is_type(length, json_type_int) ||
        json_object_get_int64(length) < 0)
      throw FilesystemError(ErrorCode::InvalidPath,
                            "Invalid remote asset path/size");
    const auto name = normalizePath(json_object_get_string(id));
    const auto key = lookupKey(name);
    const auto size = json_object_get_int64(length);
    auto slot = std::make_shared<Slot>();
    slot->info = FileInfo{name, FileType::Regular, size};
    if (!provider->files_.emplace(key, slot).second)
      throw FilesystemError(ErrorCode::InvalidPath,
                            "Duplicate remote asset: " + name);

    for (auto path = parent(name); !path.empty(); path = parent(path)) {
      const auto k = lookupKey(path);
      const auto found = provider->nodes_.find(k);
      if (found == provider->nodes_.end())
        provider->nodes_.emplace(k, FileInfo{path, FileType::Directory, 0});
      else if (found->second.type != FileType::Directory)
        throw FilesystemError(ErrorCode::InvalidPath,
                              "Remote file/directory collision");
    }
    if (!provider->nodes_.emplace(key, slot->info).second)
      throw FilesystemError(ErrorCode::InvalidPath,
                            "Remote file/directory collision: " + name);
  }
  return provider;
}

std::shared_ptr<FetchAssetProvider::Slot>
FetchAssetProvider::slot(std::string_view id) const {
  const auto key = lookupKey(id);
  const auto it = files_.find(key);
  if (it == files_.end())
    throw FilesystemError(ErrorCode::NotFound,
                          "Remote asset not in manifest: " + std::string(id));
  return it->second;
}

void FetchAssetProvider::enqueue(Job job) {
  const auto dedup = job.key + ":" + std::to_string(job.start) + ":" +
                     std::to_string(job.length);
  if (requested_.insert(dedup).second) {
    ++requestedCount_;
    queued_.push_back(std::move(job));
  }
}

void FetchAssetProvider::begin(std::size_t preloadLimit) {
  if (started_)
    return;
  started_ = true;
  // A per-file threshold alone can enqueue hundreds of megabytes.
  // Reserve only a bounded total amount for full preloads.
  std::size_t remaining = std::min(preloadLimit, budget_);
  for (const auto &[key, file] : files_) {
    if (file->info.size <= 0 ||
        static_cast<std::uint64_t>(file->info.size) > remaining)
      continue;
    const auto size = static_cast<std::uint64_t>(file->info.size);
    enqueue({key, 0, size, true});
    remaining -= static_cast<std::size_t>(size);
  }
  pump();
}

void FetchAssetProvider::prefetchRange(std::string_view id,
                                       std::uint64_t offset,
                                       std::uint64_t length) {
  const auto entry = slot(id);
  if (!length || length > (1u << 20) ||
      offset > static_cast<std::uint64_t>(entry->info.size) ||
      length > static_cast<std::uint64_t>(entry->info.size) - offset)
    throw FilesystemError(ErrorCode::InvalidPath,
                          "Fetch Range must be within the asset and <=4 MiB");
  enqueue({lookupKey(id), offset, length, false});
  if (started_)
    pump();
}

bool FetchAssetProvider::ready(std::string_view id) const {
  auto file = slot(id);
  if (file->info.size == 0)
    return true;
  std::uint64_t next = 0;
  for (const auto &[start, bytes] : file->chunks) {
    if (start > next)
      return false;
    next = std::max(next, start + bytes->size());
    if (next >= static_cast<std::uint64_t>(file->info.size))
      return true;
  }
  return false;
}

bool FetchAssetProvider::allFilesReady() const {
  if (!complete() || failures_)
    return false;
  for (const auto &[key, entry] : files_)
    if (!ready(key))
      return false;
  return true;
}

bool FetchAssetProvider::containsAsset(const std::string &id) const {
  return files_.count(lookupKey(id)) != 0;
}
std::vector<Uint8> FetchAssetProvider::readAsset(const std::string &id) const {
  auto file = open(id);
  return readAll(*file);
}
std::vector<std::string>
FetchAssetProvider::listAsset(const std::string &prefix) const {
  const auto key = lookupKey(prefix, true);
  std::vector<std::string> result;
  for (const auto &[name, file] : files_)
    if (name.compare(0, key.size(), key) == 0)
      result.push_back(file->info.path);
  return result;
}
bool FetchAssetProvider::isKnownRegularFile(std::string_view path) const {
  const auto it = nodes_.find(lookupKey(path, true));
  return it != nodes_.end() && it->second.type == FileType::Regular;
}
bool FetchAssetProvider::isKnownDirectory(std::string_view path) const {
  return hasIndexedDirectory(path);
}
bool FetchAssetProvider::hasIndexedDirectory(std::string_view path) const {
  const auto it = nodes_.find(lookupKey(path, true));
  return it != nodes_.end() && it->second.type == FileType::Directory;
}
bool FetchAssetProvider::exists(std::string_view path) const {
  return nodes_.count(lookupKey(path, true)) != 0;
}
FileInfo FetchAssetProvider::stat(std::string_view path) const {
  const auto key = lookupKey(path, true);
  const auto it = nodes_.find(key);
  if (it == nodes_.end())
    throw FilesystemError(ErrorCode::NotFound,
                          "Remote file metadata unavailable");
  return it->second;
}
std::vector<FileInfo>
FetchAssetProvider::list(std::string_view directory) const {
  const auto path = lookupKey(directory, true);
  if (stat(path).type != FileType::Directory)
    throw FilesystemError(ErrorCode::NotDirectory, "Not a remote directory");
  std::vector<FileInfo> result;
  for (const auto &[key, node] : nodes_)
    if (!key.empty() && key != path && parent(key) == path)
      result.push_back(node);
  return result;
}

std::unique_ptr<FileHandle>
FetchAssetProvider::open(std::string_view id) const {
  // slot() rejects unknown manifest files before scheduling HTTP.
  const auto data = slot(id);
  auto owner = const_cast<FetchAssetProvider *>(this)->shared_from_this();
  const auto key = lookupKey(id);
  return std::make_unique<CachedHandle>(
      data->info.size,
      [data, owner, key](void *output, std::size_t count,
                         std::uint64_t cursor) -> std::int64_t {
#ifdef __EMSCRIPTEN__
        if (data->info.size > 0) {
          auto cachedAt = [&]() {
            const auto after = data->chunks.upper_bound(cursor);
            return after != data->chunks.begin() &&
                   cursor - std::prev(after)->first <
                       std::prev(after)->second->size();
          };
          if (!cachedAt() && !data->failed) {
            const auto available =
                static_cast<std::uint64_t>(data->info.size) - cursor;
            owner->prefetchRange(key, cursor,
                                 std::min<std::uint64_t>(available, 1u << 20));
#ifdef __EMSCRIPTEN__
            const double deadline = emscripten_get_now() + 31000;
            while (!data->failed && !cachedAt() &&
                   emscripten_get_now() < deadline)
              emscripten_sleep(15);
#endif
          }
        }
#endif
        if (data->failed)
          throw FilesystemError(data->failure, data->message);
        const auto after = data->chunks.upper_bound(cursor);
        if (after == data->chunks.begin())
          throw FilesystemError(ErrorCode::WouldBlock,
                                "Remote range is not prefetched");
        const auto found = std::prev(after);
        const auto start = found->first;
        const auto &bytes = *found->second;
        if (cursor - start >= bytes.size())
          throw FilesystemError(ErrorCode::WouldBlock,
                                "Remote range is not prefetched");
        data->lastUse[start] = ++owner->accessClock_;
        const auto inChunk = static_cast<std::size_t>(cursor - start);
        const auto amount = std::min(count, bytes.size() - inChunk);
        std::memcpy(output, bytes.data() + inChunk, amount);
        return static_cast<std::int64_t>(amount);
      });
}

std::unique_ptr<Kotonoha::AssetStream>
FetchAssetProvider::openStream(const std::string &id) const {
  return std::make_unique<FetchStream>(open(id));
}

void FetchAssetProvider::complete(const Job &job, unsigned status,
                                  const char *body, std::uint64_t count,
                                  const std::string &headers) {
  const auto file = files_.at(job.key);
  // Simple HTTP servers may ignore Range. Accept their full 200 response
  // only if its length matches the entire object; store at offset zero.
  const bool wholeResponse =
      !job.full && status == 200u && file->info.size >= 0 &&
      count == static_cast<std::uint64_t>(file->info.size);
  if ((!wholeResponse && status != (job.full ? 200u : 206u)) ||
      (!wholeResponse && count != job.length) || (count && !body)) {
    file->failed = true;
    file->failure = status == 200 && !job.full ? ErrorCode::RangeUnsupported
                                               : ErrorCode::NetworkError;
    file->message = "Remote Fetch failed, status " + std::to_string(status);
    return;
  }
  if (!job.full && !wholeResponse) {
    // Reject wrong Content-Range, which might otherwise corrupt ISO/TAR bytes.
    const auto marker = std::string("bytes ") + std::to_string(job.start) +
                        "-" + std::to_string(job.start + job.length - 1) + "/" +
                        std::to_string(file->info.size);
    bool rangeMatches = false;
    std::size_t begin = 0;
    while (begin < headers.size()) {
      const auto end = headers.find('\n', begin);
      auto line = headers.substr(
          begin, end == std::string::npos ? std::string::npos : end - begin);
      if (!line.empty() && line.back() == '\r')
        line.pop_back();
      const auto colon = line.find(':');
      if (colon != std::string::npos) {
        auto key = line.substr(0, colon);
        std::transform(
            key.begin(), key.end(), key.begin(),
            [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        auto value = line.substr(colon + 1);
        value.erase(0, value.find_first_not_of(" \t"));
        rangeMatches =
            rangeMatches || (key == "content-range" && value == marker);
      }
      if (end == std::string::npos)
        break;
      begin = end + 1;
    }
    if (!rangeMatches) {
      file->failed = true;
      file->failure = ErrorCode::RangeUnsupported;
      file->message = "Missing or mismatched HTTP Content-Range";
      return;
    }
  }
  // HTTP 200 on a requested Range means this server returned the entire
  // object. Large GPKs cannot be buffered to emulate random-access reads.
  constexpr std::uint64_t kMaxFullObjectFallback = 8u << 20;
  if (!job.full && wholeResponse && count > kMaxFullObjectFallback) {
    file->failed = true;
    file->failure = ErrorCode::RangeUnsupported;
    file->message =
        "HTTP 200 returned an entire large archive instead of HTTP 206 "
        "Partial Content. Serve game-assets with HTTP Range support "
        "(e.g. python3 tools/serve_web.py)";
    return;
  }
  if (count > static_cast<std::uint64_t>(budget_)) {
    file->failed = true;
    file->failure = ErrorCode::RangeUnsupported;
    file->message = "HTTP response is too large; server must return "
                    "206 Partial Content for Range reads";
    return;
  }

  // Bounded LRU across all files. Files often need only index/footer
  // pages from many GPKs, followed by different payload pages during play.
  // Evict older cached ranges rather than permanently failing a GPK when
  // the cumulative page volume exceeds the memory budget.
  while (count > static_cast<std::uint64_t>(budget_ - cached_)) {
    std::shared_ptr<Slot> oldestFile;
    std::uint64_t oldestOffset = 0;
    std::uint64_t oldestStamp = UINT64_MAX;
    for (auto &[name, entry] : files_) {
      for (const auto &[offset, stamp] : entry->lastUse) {
        if (stamp < oldestStamp) {
          oldestStamp = stamp;
          oldestFile = entry;
          oldestOffset = offset;
        }
      }
    }
    if (!oldestFile) {
      file->failed = true;
      file->failure = ErrorCode::IOError;
      file->message = "Fetch cache accounting mismatch";
      return;
    }
    const auto it = oldestFile->chunks.find(oldestOffset);
    if (it == oldestFile->chunks.end()) {
      oldestFile->lastUse.erase(oldestOffset);
      continue;
    }
    cached_ -= it->second->size();
    oldestFile->chunks.erase(it);
    oldestFile->lastUse.erase(oldestOffset);
  }
  auto bytes = std::make_shared<std::vector<Uint8>>();
  if (count) {
    auto *first = reinterpret_cast<const Uint8 *>(body);
    bytes->assign(first, first + static_cast<std::size_t>(count));
  }
  const auto offset = wholeResponse ? 0 : job.start;
  if (auto previous = file->chunks.find(offset);
      previous != file->chunks.end()) {
    cached_ -= previous->second->size();
    file->chunks.erase(previous);
  }
  cached_ += static_cast<std::size_t>(count);
  file->chunks.emplace(offset, std::move(bytes));
  file->lastUse[offset] = ++accessClock_;
}

#ifdef __EMSCRIPTEN__
namespace {
struct FetchContext {
  std::shared_ptr<FetchAssetProvider> provider;
  // Copy the job payload to avoid referencing queue memory in callbacks.
  std::string path;
  std::uint64_t start, length;
  bool full;
  std::string range;
  std::array<const char *, 3> headers{};
};
} // namespace
#endif

void FetchAssetProvider::pump() {
#ifndef __EMSCRIPTEN__
  throw FilesystemError(ErrorCode::UnsupportedMount,
                        "HTTP Fetch is available only on Emscripten");
#else
  // Prefer JavaScript Fetch + IndexedDB when the HTTP source is configured.
  // Retain emscripten_fetch below as a compatibility fallback for old shells.
  if (EM_ASM_INT({
        // Emscripten stringizes this through the C preprocessor. JavaScript
        // '===' becomes two C tokens ('==' and '='), producing invalid '== ='
        // in the emitted JS. 'typeof' returns a string, so '==' is safe here.
        return window.KotonohaAssetCache &&
                       typeof window.KotonohaAssetCache.fetchJob == "function"
                   ? 1
                   : 0;
      })) {
    while (active_ < 4 && !queued_.empty()) {
      auto job = std::move(queued_.front());
      queued_.pop_front();
      auto *ctx = new FetchContext{
          shared_from_this(), job.key, job.start, job.length, job.full, {}, {}};
      const auto &file = files_.at(job.key);
      const auto &remotePath =
          file->remotePath.empty() ? file->info.path : file->remotePath;
      const std::string url = base_ + encodePath(remotePath);
      const auto fileSize = file->info.size;
      ++active_;
      // Explicit 32-bit halves avoid JS precision loss for i64 arguments.
      // All supported offsets are exact integers <= 2^53 - 1.
      EM_ASM(
          {
            const ctx = $0;
            const url = UTF8ToString($1);
            const path = UTF8ToString($2);
            const remotePath = path;
            const start = ($3 >>> 0) + ($4 >>> 0) * 4294967296;
            const length = ($5 >>> 0) + ($6 >>> 0) * 4294967296;
            const full = !!$7;
            const fileSize = ($8 >>> 0) + ($9 >>> 0) * 4294967296;
            let delivered = false;
            Promise.resolve()
                .then(function() {
                  return window.KotonohaAssetCache.fetchJob(
                      {url, path, remotePath, start, length, full, fileSize});
                })
                .then(function(result) {
                  const bytes = result.bytes || new Uint8Array(0);
                  let pointer = 0;
                  try {
                    if (bytes.byteLength) {
                      pointer = Module.ccall(
                          "KotonohaAllocateRemoteFetchBuffer",
                          "number", ["number"], [bytes.byteLength]);
                      if (!pointer)
                        throw new Error(
                            "Cannot allocate WASM HTTP response buffer");
                      const heap = Module.HEAPU8;
                      if (!heap || pointer + bytes.byteLength > heap.length)
                        throw new Error(
                            "Invalid WASM memory for HTTP response");
                      heap.set(bytes, pointer);
                    }
                    delivered = true;
                    Module.ccall(
                        "KotonohaCompleteBrowserFetch", null,
                        [ "number", "number", "number", "number", "string" ], [
                          ctx, result.status, pointer, bytes.byteLength,
                          result.headers || ""
                        ]);
                  } finally {
                    if (pointer)
                      Module.ccall("KotonohaFreeRemoteFetchBuffer",
                                   null, ["number"], [pointer]);
                  }
                })
                .catch(function(error) {
                  console.error("[Kotonoha] IndexedDB/HTTP asset request:",
                                error);
                  if (!delivered) {
                    delivered = true;
                    Module.ccall(
                        "KotonohaCompleteBrowserFetch", null,
                        [ "number", "number", "number", "number", "string" ],
                        [ ctx, 0, 0, 0, "" ]);
                  }
                });
          },
          static_cast<std::uintptr_t>(reinterpret_cast<std::uintptr_t>(ctx)),
          url.c_str(), remotePath.c_str(),
          static_cast<std::uint32_t>(job.start),
          static_cast<std::uint32_t>(job.start >> 32),
          static_cast<std::uint32_t>(job.length),
          static_cast<std::uint32_t>(job.length >> 32),
          static_cast<int>(job.full),
          static_cast<std::uint32_t>(static_cast<std::uint64_t>(fileSize)),
          static_cast<std::uint32_t>(static_cast<std::uint64_t>(fileSize) >>
                                     32));
    }
    return;
  }
  while (active_ < 4 && !queued_.empty()) {
    auto job = std::move(queued_.front());
    queued_.pop_front();
    auto *ctx = new FetchContext{
        shared_from_this(), job.key, job.start, job.length, job.full, {}, {}};
    const auto &file = files_.at(job.key);
    emscripten_fetch_attr_t attr;
    emscripten_fetch_attr_init(&attr);
    std::memcpy(attr.requestMethod, "GET", 4);
    attr.attributes = EMSCRIPTEN_FETCH_LOAD_TO_MEMORY;
    attr.timeoutMSecs = 30000;
    if (!job.full) {
      ctx->range = "bytes=" + std::to_string(job.start) + "-" +
                   std::to_string(job.start + job.length - 1);
      ctx->headers = {"Range", ctx->range.c_str(), nullptr};
      attr.requestHeaders = ctx->headers.data();
    }
    attr.userData = ctx;
    auto finish = +[](emscripten_fetch_t *fetch) {
      // Emscripten calls these from JavaScript's XHR/IndexedDB callbacks.
      // An exception crossing that boundary becomes an uncaught CppException
      // and can abort the whole SDL application during Asyncify rewind.
      std::unique_ptr<FetchContext> ctx(
          static_cast<FetchContext *>(fetch->userData));
      auto provider = ctx->provider;
      const auto failed = [&]() {
        const auto it = provider->files_.find(ctx->path);
        if (it != provider->files_.end()) {
          it->second->failed = true;
          it->second->failure = ErrorCode::NetworkError;
          try {
            it->second->message = "Unhandled Fetch completion error";
          } catch (...) {
            // Do not throw again through the browser callback.
          }
        }
      };
      try {
        std::string headers;
        const auto count = emscripten_fetch_get_response_headers_length(fetch);
        if (count && count < 16384) {
          headers.resize(count + 1, '\0');
          emscripten_fetch_get_response_headers(fetch, headers.data(),
                                                headers.size());
        }
        provider->complete(Job{ctx->path, ctx->start, ctx->length, ctx->full},
                           fetch->status, fetch->data, fetch->numBytes,
                           headers);
      } catch (const std::exception &error) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                     "[Kotonoha] HTTP Fetch callback for %s failed: %s",
                     ctx->path.c_str(), error.what());
        failed();
      } catch (...) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                     "[Kotonoha] Unknown HTTP Fetch callback error: %s",
                     ctx->path.c_str());
        failed();
      }
      // Only deduplicate queued/in-flight jobs. An evicted page must be
      // eligible for a new request on the next read of that offset.
      provider->requested_.erase(ctx->path + ":" + std::to_string(ctx->start) +
                                 ":" + std::to_string(ctx->length));
      const auto it = provider->files_.find(ctx->path);
      ++provider->completed_;
      if (it != provider->files_.end() && it->second->failed) {
        ++provider->failures_;
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                    "[Kotonoha] HTTP asset unavailable: %s (HTTP %u): %s",
                    ctx->path.c_str(), fetch->status,
                    it->second->message.c_str());
      }
      emscripten_fetch_close(fetch);
      if (provider->active_ > 0)
        --provider->active_;
      try {
        provider->pump();
      } catch (const std::exception &error) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                     "[Kotonoha] Cannot dispatch next HTTP Fetch: %s",
                     error.what());
      } catch (...) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                     "[Kotonoha] Unknown error dispatching HTTP Fetch");
      }
    };
    attr.onsuccess = finish;
    attr.onerror = finish;
    const auto url =
        base_ + encodePath(file->remotePath.empty() ? file->info.path
                                                    : file->remotePath);
    ++active_;
    if (!emscripten_fetch(&attr, url.c_str())) {
      --active_;
      files_.at(job.key)->failed = true;
      files_.at(job.key)->message = "Cannot dispatch HTTP Fetch";
      ++completed_;
      ++failures_;
      delete ctx;
    }
  }
#endif
}

#ifdef __EMSCRIPTEN__
void FetchAssetProvider::completeBrowserJob(
    void *opaque, unsigned status, const char *body, std::uint64_t count,
    const std::string &headers) noexcept {
  std::unique_ptr<FetchContext> ctx(static_cast<FetchContext *>(opaque));
  if (!ctx)
    return;
  auto provider = ctx->provider;
  try {
    provider->complete(Job{ctx->path, ctx->start, ctx->length, ctx->full},
                       status, body, count, headers);
  } catch (const std::exception &error) {
    SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                 "[Kotonoha] Browser cache callback for %s failed: %s",
                 ctx->path.c_str(), error.what());
    auto found = provider->files_.find(ctx->path);
    if (found != provider->files_.end()) {
      found->second->failed = true;
      found->second->failure = ErrorCode::NetworkError;
      found->second->message = "Browser Fetch completion error";
    }
  } catch (...) {
    SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                 "[Kotonoha] Unknown browser cache callback for %s",
                 ctx->path.c_str());
    auto found = provider->files_.find(ctx->path);
    if (found != provider->files_.end()) {
      found->second->failed = true;
      found->second->failure = ErrorCode::NetworkError;
      found->second->message = "Browser Fetch completion error";
    }
  }
  provider->requested_.erase(ctx->path + ":" + std::to_string(ctx->start) +
                             ":" + std::to_string(ctx->length));
  ++provider->completed_;
  const auto found = provider->files_.find(ctx->path);
  if (found != provider->files_.end() && found->second->failed) {
    ++provider->failures_;
    SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                "[Kotonoha] HTTP asset unavailable: %s (HTTP %u): %s",
                ctx->path.c_str(), status, found->second->message.c_str());
  }
  if (provider->active_ > 0)
    --provider->active_;
  try {
    provider->pump();
  } catch (const std::exception &error) {
    SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                 "[Kotonoha] Cannot dispatch browser HTTP request: %s",
                 error.what());
  } catch (...) {
    SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                 "[Kotonoha] Unknown browser HTTP dispatch error");
  }
}

extern "C" EMSCRIPTEN_KEEPALIVE void
KotonohaCompleteBrowserFetch(std::uintptr_t context, int status,
                             const char *bytes, int count,
                             const char *headers) {
  FetchAssetProvider::completeBrowserJob(
      reinterpret_cast<void *>(context),
      status >= 0 ? static_cast<unsigned>(status) : 0u, bytes,
      count >= 0 ? static_cast<std::uint64_t>(count) : 0u,
      headers ? std::string(headers) : std::string{});
}
#endif

} // namespace Kotonoha::Filesystem
