#include <Kotonoha/Filesystem/AtomicFile.hpp>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <climits>
#include <cerrno>
#include <cstdint>
#include <filesystem>
#include <limits>
#include <mutex>
#include <string>
#include <system_error>

#if defined(__EMSCRIPTEN__)
#include <SDL3/SDL.h>
#elif defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <fcntl.h>
#include <signal.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#endif

namespace Kotonoha::Filesystem {
namespace {

namespace fs = std::filesystem;
constexpr auto orphanMinimumAge = std::chrono::hours(24);
std::atomic<std::uint64_t> nextTemporary{0};
std::mutex atomicPublicationMutex;

bool fail(std::string *error, const std::string &reason) {
  if (error)
    *error = reason;
  return false;
}

std::uint64_t processId() {
#if defined(__EMSCRIPTEN__)
  return 0; // No host-process liveness API: Web orphans are never scavenged.
#elif defined(_WIN32)
  return static_cast<std::uint64_t>(GetCurrentProcessId());
#else
  return static_cast<std::uint64_t>(::getpid());
#endif
}

fs::path uniqueTemporary(const fs::path &path) {
  fs::path temporary = path;
  const auto tick = std::chrono::steady_clock::now()
                        .time_since_epoch().count();
  const auto sequence = nextTemporary.fetch_add(1, std::memory_order_relaxed);
  temporary += ".pending." + std::to_string(processId()) + "." +
               std::to_string(tick) + "." + std::to_string(sequence);
  return temporary;
}

bool decimal(const std::string &token, std::uint64_t *result) {
  if (token.empty() || !result)
    return false;
  std::uint64_t number = 0;
  for (const auto ch : token) {
    if (ch < '0' || ch > '9')
      return false;
    const std::uint64_t digit = static_cast<std::uint64_t>(ch - '0');
    if (number >
        (std::numeric_limits<std::uint64_t>::max() - digit) / 10)
      return false;
    number = number * 10 + digit;
  }
  *result = number;
  return true;
}

bool staleTempOwner(const fs::path &candidate, const fs::path &target,
                    std::uint64_t *pid) {
  const auto prefix = target.filename().u8string() + ".pending.";
  const auto name = candidate.filename().u8string();
  if (name.compare(0, prefix.size(), prefix) != 0)
    return false;
  const auto suffix = name.substr(prefix.size());
  const auto first = suffix.find('.');
  if (first == std::string::npos)
    return false;
  const auto second = suffix.find('.', first + 1);
  if (second == std::string::npos ||
      suffix.find('.', second + 1) != std::string::npos)
    return false;
  std::uint64_t tick = 0;
  std::uint64_t sequence = 0;
  return decimal(suffix.substr(0, first), pid) &&
         decimal(suffix.substr(first + 1, second - first - 1), &tick) &&
         decimal(suffix.substr(second + 1), &sequence);
}

bool ownerDefinitelyDead(std::uint64_t pid) {
  if (pid == 0)
    return false;
#if defined(__EMSCRIPTEN__)
  (void)pid;
  return false;
#elif defined(_WIN32)
  if (pid > static_cast<std::uint64_t>(MAXDWORD))
    return true;
  HANDLE process =
      OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE,
                  static_cast<DWORD>(pid));
  if (process == nullptr)
    return GetLastError() == ERROR_INVALID_PARAMETER;
  DWORD exitCode = STILL_ACTIVE;
  const bool exited = GetExitCodeProcess(process, &exitCode) != 0 &&
                      exitCode != STILL_ACTIVE;
  CloseHandle(process);
  return exited;
#else
  if (pid > static_cast<std::uint64_t>(INT_MAX))
    return true;
  return ::kill(static_cast<pid_t>(pid), 0) == -1 && errno == ESRCH;
#endif
}

void pruneOldOrphans(const fs::path &path, const fs::path &parent) {
  std::error_code ec;
  fs::directory_iterator iter(
      parent, fs::directory_options::skip_permission_denied, ec);
  const fs::directory_iterator end;
  const auto now = fs::file_time_type::clock::now();
  while (!ec && iter != end) {
    const auto candidate = iter->path();
    iter.increment(ec);
    std::uint64_t pid = 0;
    if (!staleTempOwner(candidate, path, &pid))
      continue;
    std::error_code fileError;
    const auto status = fs::symlink_status(candidate, fileError);
    if (fileError || !fs::is_regular_file(status))
      continue; // In particular, never follow an untrusted symlink.
    const auto modified = fs::last_write_time(candidate, fileError);
    if (fileError || modified > now ||
        now - modified < orphanMinimumAge)
      continue;
    // No age-only scavenging: a suspended writer can still own its temp.
    if (!ownerDefinitelyDead(pid))
      continue;
    (void)fs::remove(candidate, fileError);
  }
}

// Guard only a temporary file for which exclusive creation succeeded. Never
// unlink a pre-existing collision from another process.
struct PendingFile {
  fs::path path;
  bool owned = false;
  bool retain = false;
  ~PendingFile() {
    if (!owned || retain)
      return;
#if defined(__EMSCRIPTEN__)
    const std::string utf8 = path.u8string();
    (void)SDL_RemovePath(utf8.c_str());
#else
    std::error_code ignored;
    (void)fs::remove(path, ignored);
#endif
  }
};

bool destinationSafe(const fs::path &path, const fs::path &parent,
                     std::string *error) {
  if (path.empty() || path.filename().empty())
    return fail(error, "Atomic file path is empty");
  std::error_code ec;
  const auto directory = fs::symlink_status(parent, ec);
  if (ec || !fs::is_directory(directory) || fs::is_symlink(directory))
    return fail(error, "Atomic file parent must be an existing directory");
  ec.clear();
  const auto current = fs::symlink_status(path, ec);
  if (ec && ec != std::errc::no_such_file_or_directory)
    return fail(error, "Cannot inspect atomic file destination");
  if (!ec && (fs::is_symlink(current) ||
              (fs::exists(current) && !fs::is_regular_file(current))))
    return fail(error, "Atomic file destination is not a regular file");
#if defined(_WIN32) && !defined(__EMSCRIPTEN__)
  // Do not silently replace a pre-existing read-only file. MoveFileExW
  // behavior can vary by volume; fail closed before creating a temp.
  const DWORD attributes = GetFileAttributesW(path.c_str());
  if (attributes != INVALID_FILE_ATTRIBUTES &&
      (attributes & (FILE_ATTRIBUTE_READONLY |
                     FILE_ATTRIBUTE_REPARSE_POINT)) != 0)
    return fail(error, "Atomic destination is read-only or a reparse point");
#endif
  return true;
}

#if !defined(_WIN32) && !defined(__EMSCRIPTEN__)
bool durableFileSync(int fd) {
#if defined(__APPLE__) && defined(F_FULLFSYNC)
  for (;;) {
    if (::fcntl(fd, F_FULLFSYNC) == 0)
      return true;
    if (errno == EINTR)
      continue;
    if (errno != ENOTSUP && errno != EINVAL)
      return false;
    break;
  }
#endif
  for (;;) {
    if (::fsync(fd) == 0)
      return true;
    if (errno != EINTR)
      return false;
  }
}

bool syncDirectory(int fd) {
  for (;;) {
    if (::fsync(fd) == 0)
      return true;
    if (errno != EINTR)
      return false;
  }
}
#endif

} // namespace

bool writeBinaryFileAtomically(const fs::path &path,
                               const std::vector<std::uint8_t> &bytes,
                               std::string *error, AtomicFileFault fault) {
  if (error)
    error->clear();
  std::lock_guard<std::mutex> lock(atomicPublicationMutex);
  const auto parent = path.parent_path().empty() ? fs::path(".")
                                                : path.parent_path();
  if (!destinationSafe(path, parent, error))
    return false;
  pruneOldOrphans(path, parent);

  std::string lastError = "Atomic temporary creation failed";
  for (unsigned creationAttempt = 0; creationAttempt < 3; ++creationAttempt) {
    PendingFile pending{uniqueTemporary(path)};

#if defined(__EMSCRIPTEN__)
    // IndexedDB/IDBFS is a logical-atomic fallback; SDL_FlushIO is not
    // hardware-power-loss durability and cannot fsync a real parent directory.
    const auto utf8Temp = pending.path.u8string();
    SDL_IOStream *output = SDL_IOFromFile(utf8Temp.c_str(), "wxb");
    if (output == nullptr) {
      lastError = std::string("Atomic SDL create failed: ") + SDL_GetError();
      continue;
    }
    pending.owned = true;
    bool written = true;
    if (fault == AtomicFileFault::Write) {
      if (!bytes.empty())
        (void)SDL_WriteIO(output, bytes.data(), 1);
      written = false;
    } else {
      std::size_t offset = 0;
      while (offset < bytes.size()) {
        const auto count = SDL_WriteIO(output, bytes.data() + offset,
                                       bytes.size() - offset);
        if (!count) {
          written = false;
          break;
        }
        offset += count;
      }
    }
    if (written && fault != AtomicFileFault::Flush)
      written = SDL_FlushIO(output);
    else
      written = false;
    const bool closed = SDL_CloseIO(output);
    if (!written || !closed)
      return fail(error, "Atomic SDL write/flush/close failed");
    if (fault == AtomicFileFault::SimulatedInterruption) {
      pending.retain = true;
      return fail(error, "Simulated interruption before atomic publish");
    }
    if (fault == AtomicFileFault::Publish ||
        fault == AtomicFileFault::DirectoryFlushBefore)
      return fail(error, "Simulated failure before atomic publish");
    bool published = false;
    const auto utf8Target = path.u8string();
    for (int attempt = 0; attempt < 3; ++attempt) {
      if (SDL_RenamePath(utf8Temp.c_str(), utf8Target.c_str())) {
        published = true;
        break;
      }
      if (attempt != 2)
        SDL_Delay(static_cast<Uint32>(40 * (attempt + 1)));
    }
    if (!published)
      return fail(error, std::string("Atomic SDL rename failed: ") +
                            SDL_GetError());
    pending.owned = false;
    if (fault == AtomicFileFault::DirectoryFlushAfter)
      return fail(error, "Atomic file published; durability is uncertain");
    return true;

#elif defined(_WIN32)
    HANDLE handle =
        CreateFileW(pending.path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW,
                    FILE_ATTRIBUTE_NORMAL | FILE_FLAG_WRITE_THROUGH, nullptr);
    if (handle == INVALID_HANDLE_VALUE) {
      const auto code = GetLastError();
      lastError = "Atomic CreateFileW failed (Win32 " +
                  std::to_string(code) + ")";
      if (code == ERROR_FILE_EXISTS || code == ERROR_ALREADY_EXISTS)
        continue;
      return fail(error, lastError);
    }
    pending.owned = true;
    if (fault == AtomicFileFault::Write) {
      DWORD ignored = 0;
      if (!bytes.empty())
        (void)WriteFile(handle, bytes.data(), 1, &ignored, nullptr);
      (void)CloseHandle(handle);
      return fail(error, "Simulated atomic WriteFile failure");
    }
    std::size_t offset = 0;
    while (offset < bytes.size()) {
      const auto size = static_cast<DWORD>(
          std::min<std::size_t>(bytes.size() - offset, 1024u * 1024u));
      DWORD count = 0;
      if (!WriteFile(handle, bytes.data() + offset, size, &count, nullptr) ||
          count == 0) {
        const auto code = GetLastError();
        (void)CloseHandle(handle);
        return fail(error, "Atomic WriteFile failed (Win32 " +
                               std::to_string(code) + ")");
      }
      offset += count;
    }
    if (fault == AtomicFileFault::Flush || !FlushFileBuffers(handle)) {
      const auto code = GetLastError();
      (void)CloseHandle(handle);
      return fail(error, fault == AtomicFileFault::Flush
                             ? "Simulated atomic flush failure"
                             : "Atomic FlushFileBuffers failed (Win32 " +
                                   std::to_string(code) + ")");
    }
    if (!CloseHandle(handle))
      return fail(error, "Atomic CloseHandle failed");
    if (fault == AtomicFileFault::SimulatedInterruption) {
      pending.retain = true;
      return fail(error, "Simulated interruption before atomic publish");
    }
    if (fault == AtomicFileFault::Publish ||
        fault == AtomicFileFault::DirectoryFlushBefore)
      return fail(error, "Simulated failure before atomic publish");
    DWORD last = ERROR_SUCCESS;
    for (int attempt = 0; attempt < 3; ++attempt) {
      if (MoveFileExW(pending.path.c_str(), path.c_str(),
                      MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        pending.owned = false;
        if (fault == AtomicFileFault::DirectoryFlushAfter)
          return fail(error,
                      "Atomic file published; simulated durability uncertainty");
        return true;
      }
      last = GetLastError();
      if (attempt == 2 ||
          (last != ERROR_SHARING_VIOLATION && last != ERROR_LOCK_VIOLATION &&
           last != ERROR_ACCESS_DENIED))
        break;
      Sleep(static_cast<DWORD>(40 * (attempt + 1)));
    }
    return fail(error, "Atomic MoveFileExW failed (Win32 " +
                           std::to_string(last) +
                           "); previous destination preserved");

#else
    const int fd =
        ::open(pending.path.c_str(),
               O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC, 0600);
    if (fd < 0) {
      const int code = errno;
      lastError = "Atomic exclusive open failed (errno " +
                  std::to_string(code) + ")";
      if (code == EEXIST)
        continue;
      return fail(error, lastError);
    }
    pending.owned = true;
    if (fault == AtomicFileFault::Write) {
      if (!bytes.empty())
        (void)::write(fd, bytes.data(), 1);
      (void)::close(fd);
      return fail(error, "Simulated atomic write failure");
    }
    std::size_t offset = 0;
    while (offset < bytes.size()) {
      const auto count = ::write(
          fd, bytes.data() + offset,
          std::min<std::size_t>(bytes.size() - offset, 1024u * 1024u));
      if (count < 0 && errno == EINTR)
        continue;
      if (count <= 0) {
        const auto code = errno;
        (void)::close(fd);
        return fail(error, "Atomic write failed (errno " +
                               std::to_string(code) + ")");
      }
      offset += static_cast<std::size_t>(count);
    }
    if (fault == AtomicFileFault::Flush || !durableFileSync(fd)) {
      const auto code = errno;
      (void)::close(fd);
      return fail(error, fault == AtomicFileFault::Flush
                             ? "Simulated atomic fsync failure"
                             : "Atomic file fsync failed (errno " +
                                   std::to_string(code) + ")");
    }
    if (::close(fd) != 0)
      return fail(error, "Atomic close failed");
    if (fault == AtomicFileFault::SimulatedInterruption) {
      pending.retain = true;
      return fail(error, "Simulated interruption before atomic publish");
    }
    if (fault == AtomicFileFault::Publish)
      return fail(error, "Simulated failure before atomic publish");

    // Same-directory rename is atomic; pre-publish directory fsync commits
    // temp directory metadata before replacing the previous inode.
    const int dirfd =
        ::open(parent.c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    if (dirfd < 0)
      return fail(error, "Atomic parent directory open failed");
    if (fault == AtomicFileFault::DirectoryFlushBefore ||
        !syncDirectory(dirfd)) {
      (void)::close(dirfd);
      return fail(error, "Atomic pre-publication directory fsync failed");
    }
    int renameError = 0;
    for (int attempt = 0; attempt < 3; ++attempt) {
      if (::rename(pending.path.c_str(), path.c_str()) == 0) {
        renameError = 0;
        break;
      }
      renameError = errno;
      if (attempt == 2 ||
          (renameError != EACCES && renameError != EBUSY))
        break;
      ::usleep(static_cast<useconds_t>(40000 * (attempt + 1)));
    }
    if (renameError != 0) {
      (void)::close(dirfd);
      return fail(error, "Atomic rename failed (errno " +
                             std::to_string(renameError) + ")");
    }
    pending.owned = false; // Published: never roll back or unlink the target.
    if (fault == AtomicFileFault::DirectoryFlushAfter ||
        !syncDirectory(dirfd)) {
      (void)::close(dirfd);
      return fail(error,
                  "Atomic file published; parent directory durability uncertain");
    }
    if (::close(dirfd) != 0)
      return fail(error,
                  "Atomic file published; parent directory close uncertain");
    return true;
#endif
  }
  return fail(error, lastError);
}

} // namespace Kotonoha::Filesystem
